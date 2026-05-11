"""JAX/grain DataLoaders for MSCompress datasets.

The :class:`MSCompressJaxDataset` class satisfies grain's random-access
data-source protocol (``__len__`` + ``__getitem__``), so it can be plugged
directly into a grain pipeline::

    import grain
    from mscompress.datasets.jax import MSCompressJaxDataset, pad_collate

    source = MSCompressJaxDataset("/path/to/data")
    pipeline = (
        grain.MapDataset.source(source)
        .shuffle(seed=0)
        .batch(32, batch_fn=pad_collate)
    )

``__getitem__`` returns plain ``numpy.ndarray`` objects (not ``jax.numpy``)
so that grain's multiprocess workers can pickle items cheaply. Move to a
device with ``jax.device_put`` or rely on JAX's implicit numpy conversion
inside a jitted function.
"""
import mscompress
from mscompress.mszx import MSZXFile
from mscompress.annotations.psms import BasePSMReader
from mscompress.types import AnnotationFormat
from typing import Union, Optional, List, Dict, Any, Sequence, Tuple, cast
from typing_extensions import TypeAlias
from pathlib import Path
import numpy as np
import numpy.typing as npt


FloatArray: TypeAlias = npt.NDArray[np.floating]
AnnotationsDict: TypeAlias = Dict[AnnotationFormat, List[Any]]
BatchItem: TypeAlias = Union[
    Tuple[FloatArray, FloatArray],
    Tuple[FloatArray, FloatArray, AnnotationsDict],
]


class MSCompressJaxDatasetMember:
    def __init__(self, path: Union[str, Path], load_annotations: Optional[List[AnnotationFormat]] = None):
        self._path = Path(path)
        self._handle = mscompress.read(str(self._path))
        self._load_annotations: List[AnnotationFormat] = load_annotations or []
        self._annotation_readers: Dict[AnnotationFormat, List[BasePSMReader]] = {}

        if isinstance(self._handle, MSZXFile) and self._load_annotations:
            for annotation_format in self._load_annotations:
                readers = self._handle.get_annotation_readers_by_format(annotation_format)
                if readers:
                    self._annotation_readers[annotation_format] = readers

    @property
    def path(self):
        return self._path

    def __len__(self) -> int:
        """Return the number of spectra in the dataset member."""
        return len(self._handle.spectra)

    def __getitem__(self, index) -> BatchItem:
        """Get spectrum by index in the MSZ/mzML/MSZX file."""
        spectrum = self._handle.spectra[index]
        mz = spectrum.mz
        intensity = spectrum.intensity

        if self._annotation_readers:
            annotations_dict: AnnotationsDict = {}
            scan_number = spectrum.scan

            for annotation_format, readers in self._annotation_readers.items():
                format_psms: List[Any] = []
                for reader in readers:
                    psms = reader.get_by_scan(scan_number)
                    format_psms.extend(psms)
                annotations_dict[annotation_format] = format_psms

            return (mz, intensity, annotations_dict)

        return (mz, intensity)


class MSCompressJaxDataset:
    """JAX-friendly random-access dataset over mzML / MSZ / MSZX files.

    Implements the grain ``RandomAccessDataSource`` protocol (``__len__`` and
    ``__getitem__``) so it composes directly with ``grain.MapDataset.source``.
    """

    def __init__(
            self,
            path: Union[str, Path],
            load_annotations: Optional[List[AnnotationFormat]] = None
        ):
        """
        Args:
            path: Path to a msz, mszx, or mzML file, or a directory containing such files.
            load_annotations: List of annotation formats to load from MSZX files.
                Only applicable for MSZX files. Annotations will be returned in __getitem__ if present.
        """
        self._path = Path(path)
        self._load_annotations: List[AnnotationFormat] = load_annotations or []

        self.members: Dict[str, MSCompressJaxDatasetMember] = {}
        if self._path.is_dir():
            for file in self._path.iterdir():
                if file.suffix.lower() in {'.msz', '.mszx', '.mzml'}:
                    member = MSCompressJaxDatasetMember(file, load_annotations=self._load_annotations)
                    self.members[file.name] = member
        elif self._path.suffix.lower() in {'.msz', '.mszx', '.mzml'}:
            member = MSCompressJaxDatasetMember(self._path, load_annotations=self._load_annotations)
            self.members[self._path.name] = member
        else:
            raise ValueError("Provided path is neither a valid file nor a directory containing valid files.")

        self._index_lookup: Dict[int, Tuple[MSCompressJaxDatasetMember, int]] = {}
        global_index = 0
        for member in self.members.values():
            for local_index in range(len(member)):
                self._index_lookup[global_index] = (member, local_index)
                global_index += 1
        self._total_spectra = global_index

    @property
    def path(self):
        return self._path

    def __len__(self) -> int:
        """Return the total number of spectra in the dataset."""
        return self._total_spectra

    def __getitem__(self, index) -> BatchItem:
        """Get spectrum by index across all dataset members."""
        if index < 0:
            index = self._total_spectra + index

        if index not in self._index_lookup:
            raise IndexError(f"Index {index} out of range for dataset with {self._total_spectra} spectra.")

        member, local_index = self._index_lookup[index]
        return member[local_index]


def list_collate(batch: Sequence[BatchItem]) -> Tuple:
    """Collate variable-length spectra into Python lists (no padding).

    Mirrors the simple ragged-batch pattern used by the PyTorch example loader.
    Useful when downstream code handles variable-length arrays directly.

    Args:
        batch: Sequence of items returned by ``MSCompressJaxDataset.__getitem__``.
            Each item is ``(mz, intensity)`` or ``(mz, intensity, annotations)``.

    Returns:
        ``(mzs, intensities)`` or ``(mzs, intensities, annotations)`` where each
        component is a Python ``list`` of length ``len(batch)``.
    """
    if len(batch) == 0:
        raise ValueError("Cannot collate an empty batch.")

    has_annotations = len(batch[0]) == 3
    mzs: List[FloatArray] = []
    intensities: List[FloatArray] = []
    annotations: List[AnnotationsDict] = []

    for item in batch:
        mzs.append(item[0])
        intensities.append(item[1])
        if len(item) == 3:
            annotations.append(cast(AnnotationsDict, item[2]))  # ty: ignore[index-out-of-bounds]

    if has_annotations:
        return (mzs, intensities, annotations)
    return (mzs, intensities)


def pad_collate(
    batch: Sequence[BatchItem],
    *,
    pad_value: float = 0.0,
    return_mask: bool = True,
) -> Tuple:
    """Collate variable-length spectra into rectangular arrays via right-padding.

    Produces stacked ``(B, max_len)`` arrays suitable for ``jax.jit`` / ``jax.vmap``.
    Spectrum dtypes are preserved (e.g. float32 vs float64 from the source file).

    Args:
        batch: Sequence of items returned by ``MSCompressJaxDataset.__getitem__``.
        pad_value: Value used for right-padding shorter spectra.
        return_mask: If True, also return a boolean mask (True for real peaks).

    Returns:
        A tuple of stacked numpy arrays. Without annotations and with mask:
        ``(mz, intensity, mask)``. With annotations: ``(mz, intensity, mask, annotations)``
        where ``annotations`` is a list (no sensible way to stack PSM dicts).
    """
    if len(batch) == 0:
        raise ValueError("Cannot collate an empty batch.")

    has_annotations = len(batch[0]) == 3
    batch_size = len(batch)
    max_len = max(item[0].shape[0] for item in batch)

    mz_dtype = batch[0][0].dtype
    intensity_dtype = batch[0][1].dtype

    mz_out = np.full((batch_size, max_len), pad_value, dtype=mz_dtype)
    intensity_out = np.full((batch_size, max_len), pad_value, dtype=intensity_dtype)
    mask_out = np.zeros((batch_size, max_len), dtype=np.bool_) if return_mask else None
    annotations: List[AnnotationsDict] = []

    for i, item in enumerate(batch):
        mz = item[0]
        intensity = item[1]
        if len(item) == 3:
            annotations.append(cast(AnnotationsDict, item[2]))  # ty: ignore[index-out-of-bounds]
        n = mz.shape[0]
        mz_out[i, :n] = mz
        intensity_out[i, :n] = intensity
        if mask_out is not None:
            mask_out[i, :n] = True

    components: List[Any] = [mz_out, intensity_out]
    if mask_out is not None:
        components.append(mask_out)
    if has_annotations:
        components.append(annotations)
    return tuple(components)
