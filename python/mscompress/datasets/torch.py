"""PyTorch DataLoaders for MSCompress datasets.

Designed for multi-worker ``DataLoader`` use over large multi-shard datasets:

- No per-spectrum index is materialized. A file is located from a global index
  by binary search over a per-file prefix sum (O(n_files) memory, not
  O(n_spectra)).
- Files are opened lazily and per-process. ``MSCompressDataset.__init__`` only
  reads each file's spectrum count (footer) and closes it again, so the parent
  process holds no open handles — each ``DataLoader`` worker opens its own when
  it first touches a shard.
- All shards opened within one worker share a single byte-budgeted
  ``BlockCache`` (see ``cache_bytes``), so the worker's decompressed-block
  memory is bounded regardless of how many shards it samples. Under N workers,
  size for ``RAM_target / N``.
"""
import bisect
import os
from pathlib import Path
from typing import Union, Optional, List, Dict, Any

import mscompress
from mscompress.mszx import MSZXFile
from mscompress.annotations.psms import BasePSMReader
from mscompress.types import AnnotationFormat

try:
    import torch
    from torch.utils.data import Dataset
except ImportError as e:
    raise ImportError(
        "PyTorch is required to use this module. Please install it via 'pip install torch'."
    ) from e

_SUFFIXES = {".msz", ".mszx", ".mzml"}


def _spectrum_to_tuple(spectrum, readers):
    mz = torch.from_numpy(spectrum.mz)
    intensity = torch.from_numpy(spectrum.intensity)
    if readers:
        annotations_dict: Dict[Any, Any] = {}
        scan_number = spectrum.scan
        for annotation_format, reader_list in readers.items():
            format_psms = []
            for reader in reader_list:
                format_psms.extend(reader.get_by_scan(scan_number))
            annotations_dict[annotation_format] = format_psms
        return (mz, intensity, annotations_dict)
    return (mz, intensity)


class MSCompressDatasetMember:
    """A single MSZ/MSZX/mzML file. Opens lazily and per-process so it is safe
    to construct in the parent and use from forked ``DataLoader`` workers."""

    def __init__(
        self,
        path: Union[str, Path],
        load_annotations: Optional[List[AnnotationFormat]] = None,
        cache=None,
    ):
        self._path = Path(path)
        self._load_annotations: List[AnnotationFormat] = load_annotations or []
        self._cache = cache  # None | 0 | int byte-budget | BlockCache (shared)
        self._pid = None
        self._handle = None
        self._annotation_readers: Dict[AnnotationFormat, List[BasePSMReader]] = {}
        self._count: Optional[int] = None

    @property
    def path(self):
        return self._path

    def _resolve_cache(self):
        # A bare int budget becomes a private per-process BlockCache; None/0/an
        # explicit (shared) BlockCache are forwarded to read() as-is.
        if isinstance(self._cache, int) and not isinstance(self._cache, bool) and self._cache > 0:
            return mscompress.BlockCache(self._cache)
        return self._cache

    def _ensure_open(self):
        pid = os.getpid()
        if self._handle is None or self._pid != pid:
            # New process (forked worker) or first use: open a fresh handle.
            self._pid = pid
            self._handle = mscompress.read(str(self._path), cache=self._resolve_cache())
            self._annotation_readers = {}
            if isinstance(self._handle, MSZXFile) and self._load_annotations:
                for annotation_format in self._load_annotations:
                    readers = self._handle.get_annotation_readers_by_format(annotation_format)
                    if readers:
                        self._annotation_readers[annotation_format] = readers
        return self._handle

    def __len__(self) -> int:
        if self._count is None:
            self._count = len(self._ensure_open().spectra)
        return self._count

    def __getitem__(self, index):
        handle = self._ensure_open()
        return _spectrum_to_tuple(handle.spectra[index], self._annotation_readers)

    def close(self):
        if self._handle is not None:
            try:
                self._handle._cleanup()
            except Exception:
                pass
            self._handle = None
            self._annotation_readers = {}


class MSCompressDataset(Dataset):
    def __init__(
        self,
        path: Union[str, Path],
        load_annotations: Optional[List[AnnotationFormat]] = None,
        cache_bytes: Optional[int] = None,
        max_open_files: Optional[int] = None,
    ):
        """Map-style dataset over one file or a directory of files.

        Args:
            path: A .msz/.mszx/.mzML file, or a directory of such files.
            load_annotations: Annotation formats to surface (MSZX only).
            cache_bytes: Per-worker decompressed-block cache budget, shared
                across all shards a worker opens. ``None`` (default) uses the
                mscompress process-default BlockCache; ``0`` disables bounding
                (legacy unbounded); a positive int sets the budget in bytes.
                Under N DataLoader workers each worker gets its own, so size for
                ``RAM_target / N``.
            max_open_files: Optional cap on how many shard handles a worker
                keeps open at once (LRU-evicted). ``None`` keeps all open.
        """
        self._path = Path(path)
        self._load_annotations: List[AnnotationFormat] = load_annotations or []
        self._cache_bytes = cache_bytes
        self._max_open_files = max_open_files

        # Enumerate files deterministically.
        if self._path.is_dir():
            files = sorted(
                p for p in self._path.iterdir() if p.suffix.lower() in _SUFFIXES
            )
        elif self._path.suffix.lower() in _SUFFIXES:
            files = [self._path]
        else:
            raise ValueError(
                "Provided path is neither a valid file nor a directory containing valid files."
            )
        if not files:
            raise ValueError(f"No .msz/.mszx/.mzML files found in {self._path}")

        self._files: List[Path] = files

        # Lightweight members (lazy, no open handle) keyed by filename, kept for
        # backward compatibility / introspection. The hot path does not use them.
        self.members: Dict[str, MSCompressDatasetMember] = {
            f.name: MSCompressDatasetMember(f, load_annotations=self._load_annotations)
            for f in files
        }

        # Per-file spectrum counts + prefix sum. Reading a count opens the file
        # (footer only) and closes it again — the parent keeps no open handles
        # and never materializes a per-spectrum index.
        self._offsets: List[int] = [0]
        for f in files:
            m = MSCompressDatasetMember(f)
            try:
                self._offsets.append(self._offsets[-1] + len(m))
            finally:
                m.close()
        self._total_spectra = self._offsets[-1]

        # Per-process lazy state (created in the worker, never inherited).
        self._proc_pid: Optional[int] = None
        self._proc_cache = None
        self._handles: "Dict[int, Any]" = {}
        self._handle_order: List[int] = []
        self._readers: Dict[int, Dict[AnnotationFormat, List[BasePSMReader]]] = {}

    @property
    def path(self):
        return self._path

    def __len__(self) -> int:
        return self._total_spectra

    # -- per-process handle / cache management -----------------------------

    def _make_cache(self):
        if self._cache_bytes is None:
            return None  # mscompress process-default BlockCache
        if self._cache_bytes == 0:
            return 0  # disable bounding
        return mscompress.BlockCache(int(self._cache_bytes))

    def _reset_process_state(self):
        self._proc_pid = os.getpid()
        # One BlockCache shared by every shard this worker opens, so the
        # worker's total decompressed-block memory obeys one budget.
        self._proc_cache = self._make_cache()
        self._handles = {}
        self._handle_order = []
        self._readers = {}

    def _get_handle(self, file_index: int):
        if self._proc_pid != os.getpid():
            self._reset_process_state()

        handle = self._handles.get(file_index)
        if handle is not None:
            # LRU bump.
            self._handle_order.remove(file_index)
            self._handle_order.append(file_index)
            return handle

        handle = mscompress.read(str(self._files[file_index]), cache=self._proc_cache)
        readers: Dict[AnnotationFormat, List[BasePSMReader]] = {}
        if isinstance(handle, MSZXFile) and self._load_annotations:
            for annotation_format in self._load_annotations:
                rs = handle.get_annotation_readers_by_format(annotation_format)
                if rs:
                    readers[annotation_format] = rs

        self._handles[file_index] = handle
        self._readers[file_index] = readers
        self._handle_order.append(file_index)

        if self._max_open_files and len(self._handles) > self._max_open_files:
            evict = self._handle_order.pop(0)
            old = self._handles.pop(evict, None)
            self._readers.pop(evict, None)
            if old is not None:
                try:
                    old._cleanup()
                except Exception:
                    pass
        return handle

    def __getitem__(self, index):
        if index < 0:
            index += self._total_spectra
        if not (0 <= index < self._total_spectra):
            raise IndexError(
                f"Index {index} out of range for dataset with {self._total_spectra} spectra."
            )
        # Locate the file: largest offset <= index.
        file_index = bisect.bisect_right(self._offsets, index) - 1
        local_index = index - self._offsets[file_index]
        handle = self._get_handle(file_index)
        return _spectrum_to_tuple(handle.spectra[local_index], self._readers[file_index])
