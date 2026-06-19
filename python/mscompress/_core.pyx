import os
import shutil
import numpy as np
import warnings
import tempfile
import ctypes
import threading
cimport numpy as np
from typing import Union, Iterator, Optional
from os import PathLike
from pathlib import Path
from xml.etree.ElementTree import fromstring, Element, ParseError
from libc.stdlib cimport malloc, free
from libc.string cimport memcpy, const_char
from libc.math cimport nan
import math
import re

np.import_array()

# Create a numpy dtype that matches C long (32-bit on Windows, typically 64-bit on Linux/Mac)
_c_long_dtype = np.dtype(np.int32 if ctypes.sizeof(ctypes.c_long) == 4 else np.int64)
include "_headers.pxi"

# Global error/warning handler for Python
def _install_mscompress_warning_formatter():
    import warnings
    def _mscompress_formatwarning(message, category, filename, lineno, line=None):
        return f"mscompress : {category.__name__}: {message}\n"
    warnings.formatwarning = _mscompress_formatwarning

_install_mscompress_warning_formatter()


class MSCompressError(RuntimeError):
    """Raised when the mscompress C core reports a fatal ``error()``.

    Subclasses :class:`RuntimeError` so existing ``except RuntimeError`` handlers
    keep working. Distinct from the :class:`RuntimeWarning` emitted for the
    C core's non-fatal ``warning()`` (graceful-degradation) path. See issue #171.
    """


# Pending fatal-error state, shared across threads. The C ``error()`` callback
# may fire from worker threads in the compress/decompress pipelines, so we cannot
# raise from inside the callback (the C frames running after it would keep
# executing). Instead the callback stashes the first message here and the next
# C<->Python boundary (a GIL-holding Python frame) turns it into a raise via
# ``_raise_pending_error()``. A list cell avoids needing ``global`` declarations.
_error_lock = threading.Lock()
_pending_error = [None]

# `with gil` is required because these callbacks may be invoked from C code
# running without the GIL (e.g., streaming methods release the GIL for I/O).
cdef void _python_error_handler(const char* message) noexcept with gil:
    """Record a fatal C ``error()`` to be re-raised at the next boundary.

    Cannot raise here: the callback is invoked from C (potentially a worker
    thread) that does not check for a pending Python exception, so the remaining
    C frames would keep running. We stash the first message and let
    ``_raise_pending_error()`` surface it as :class:`MSCompressError`.
    """
    msg = message.decode('utf-8') if isinstance(message, bytes) else message
    with _error_lock:
        if _pending_error[0] is None:
            _pending_error[0] = msg.strip()

cdef void _python_warning_handler(const char* message) noexcept with gil:
    """Callback function to handle non-fatal C warnings in Python.

    ``warning()`` denotes graceful degradation (e.g. corrupt-base64 input that
    yields an empty array by design), so it stays a :class:`RuntimeWarning`.
    """
    msg = message.decode('utf-8') if isinstance(message, bytes) else message
    warnings.warn(msg.strip(), RuntimeWarning, stacklevel=2)


def _clear_pending_error():
    """Drop any stashed C error. Call at the start of a C-backed operation so a
    stale error from an abandoned/early-closed stream cannot leak into it."""
    with _error_lock:
        _pending_error[0] = None


def _raise_pending_error():
    """Raise (and clear) any C error stashed by the error callback."""
    with _error_lock:
        msg = _pending_error[0]
        _pending_error[0] = None
    if msg is not None:
        raise MSCompressError(msg)

# Initialize callbacks when module is imported
_set_error_callback(_python_error_handler)
_set_warning_callback(_python_warning_handler)


# ---------------------------------------------------------------------------
# Spectrum-object cache sizing. Defined above any class that uses them as
# default-arg values because Cython evaluates def-default values at
# class-definition time, not at call time.
# ---------------------------------------------------------------------------

CACHE_SPECTRA_AUTO = -1
"""Sentinel for ``cache_spectra`` meaning "use the bundled default".

When passed (the default), ``Spectra`` keeps the last ``_DEFAULT_CACHE_SPECTRA``
returned ``Spectrum`` objects in a bounded LRU. Repeated access to a cached
index hands back the same ``Spectrum`` instance (so its lazily-loaded ``mz`` /
``intensity`` / ``xml`` payloads survive). Pass ``cache_spectra=N`` for an
explicit cap, or ``cache_spectra=0`` to disable eviction (legacy: the cache
grows unboundedly, one entry per ever-accessed index)."""

_DEFAULT_CACHE_SPECTRA = 128
# Big enough that contiguous .mz/.intensity/.xml accesses on the same Spectrum
# stay hot and typical ML batch sizes don't thrash; small enough that random
# sampling can't pin tens of thousands of Spectrum objects.


# ---------------------------------------------------------------------------
# Shared, byte-budgeted decompressed-block cache.
#
# A single BlockCache can be shared across many open MSZ/MSZX files so that the
# AGGREGATE decompressed-block memory obeys one ceiling, instead of each file
# holding its own unbounded cache (which sums to hundreds of GB across a
# multi-shard random-access dataloader). The cap is in BYTES, measured exactly
# from each block's decompressed size.
#
# By default every file shares one process-wide BlockCache sized to
# DEFAULT_CACHE_BYTES (overridable via MSCOMPRESS_CACHE_BYTES or
# set_cache_budget()). Pass cache=BlockCache(n) to read() to use a private
# pool, or cache=0 to opt out of bounding entirely (legacy unbounded). read()
# is the only public entry point for the knob; the file constructors don't
# expose it.
# ---------------------------------------------------------------------------

DEFAULT_CACHE_BYTES = 4 * 1024 * 1024 * 1024  # 4 GiB

_default_block_cache = None


cdef _resolve_default_budget():
    env = os.environ.get("MSCOMPRESS_CACHE_BYTES")
    if env:
        try:
            return int(env)
        except (TypeError, ValueError):
            warnings.warn(
                f"Ignoring invalid MSCOMPRESS_CACHE_BYTES={env!r}; "
                f"using {DEFAULT_CACHE_BYTES} bytes",
                RuntimeWarning,
            )
    return DEFAULT_CACHE_BYTES


cdef class BlockCache:
    """A shared, byte-budgeted LRU over decompressed mz/intensity/xml blocks.

    Pass one instance to several files (``read(path, cache=shared)``) and the
    total decompressed-block memory they hold together is bounded by ``budget``
    bytes; least-recently-used blocks are evicted (and re-decompressed on demand)
    past the cap.

    Args:
        budget_bytes: Maximum total cached bytes. ``None`` resolves to the
            process default (``MSCOMPRESS_CACHE_BYTES`` or ``DEFAULT_CACHE_BYTES``).
    """
    cdef block_lru_t* _lru
    cdef readonly object budget

    def __cinit__(self, budget_bytes=None):
        if budget_bytes is None:
            budget_bytes = _resolve_default_budget()
        budget_bytes = int(budget_bytes)
        if budget_bytes < 0:
            raise ValueError("budget_bytes must be >= 0")
        self.budget = budget_bytes
        self._lru = _alloc_block_lru(<size_t>budget_bytes)

    def __dealloc__(self):
        if self._lru != NULL:
            # Free any cache buffers still pinned, then the struct. Open files
            # hold a Python ref to this BlockCache, so this only runs once no
            # file references it — there are no live block pointers to dangle.
            _lru_evict_all(self._lru)
            _dealloc_block_lru(self._lru)
            self._lru = NULL

    @property
    def usage(self):
        """Current total decompressed bytes held across all sharing files."""
        return _block_cache_usage(self._lru)

    def clear(self):
        """Evict every cached block (frees buffers; files re-decompress lazily)."""
        if self._lru != NULL:
            _lru_evict_all(self._lru)

    def __repr__(self):
        return (f"<BlockCache budget={self.budget} bytes "
                f"usage={self.usage} bytes>")


def get_default_cache():
    """Return the process-wide default BlockCache, creating it on first use."""
    global _default_block_cache
    if _default_block_cache is None:
        _default_block_cache = BlockCache(_resolve_default_budget())
    return _default_block_cache


def set_cache_budget(budget_bytes):
    """Set the process-default block-cache budget (in bytes).

    Installs a fresh default ``BlockCache`` of the given size. Files opened
    afterward (without an explicit ``cache=``) share it. Files already open keep
    the cache they were opened with. Call once at startup for predictable
    behavior; under a multiprocessing DataLoader each worker has its own default,
    so size for ``RAM_target / n_workers``.
    """
    global _default_block_cache
    _default_block_cache = BlockCache(int(budget_bytes))
    return _default_block_cache


def get_cache_usage():
    """Current bytes held by the process-default BlockCache (0 if unused)."""
    if _default_block_cache is None:
        return 0
    return _default_block_cache.usage


cdef BlockCache _coerce_cache(object cache):
    """Resolve a user-supplied ``cache=`` argument to a BlockCache instance.

    - None        -> the shared process default
    - 0           -> None (opt out: unbounded legacy caching, no LRU)
    - int > 0     -> a fresh private BlockCache of that byte budget
    - BlockCache  -> used as-is (shared)
    """
    if cache is None:
        return get_default_cache()
    if isinstance(cache, BlockCache):
        return <BlockCache>cache
    if isinstance(cache, int):
        if cache == 0:
            return None
        return BlockCache(cache)
    raise TypeError(
        "cache must be a BlockCache, an int byte-budget, 0 to disable, or None"
    )


def _pipe_stream(c_func, args, chunk_size=1_048_576):
    """
    Pipe-based streaming bridge between C fd-oriented writes and Python iterators.

    Creates an OS pipe, runs the C function in a background thread writing to the
    pipe's write end, and yields bytes chunks read from the pipe's read end.

    Args:
        c_func: Callable that accepts (*args, write_fd) and writes output to the fd.
        args: Tuple of arguments to pass to c_func before the write_fd.
        chunk_size: Number of bytes to read per iteration (default 1MB).

    Yields:
        bytes: Chunks of output data from the C function.

    Raises:
        RuntimeError: If the C function raises an exception in the background thread.
    """
    read_fd, write_fd = os.pipe()
    exc_holder = [None]

    def _worker():
        try:
            c_func(*args, write_fd)
        except Exception as e:
            exc_holder[0] = e
        finally:
            os.close(write_fd)

    t = threading.Thread(target=_worker, daemon=True)
    t.start()

    reader = os.fdopen(read_fd, 'rb')
    try:
        while True:
            chunk = reader.read(chunk_size)
            if not chunk:
                break
            yield chunk
    except GeneratorExit:
        reader.close()
        t.join()
        return
    finally:
        reader.close()

    t.join()
    if exc_holder[0] is not None:
        raise exc_holder[0]


cdef class RuntimeArguments:
    cdef Arguments _arguments
    cdef bytes _mz_lossy_buf
    cdef bytes _int_lossy_buf

    def __init__(self):
        self._arguments.threads = _get_num_threads()
        self._mz_lossy_buf = b"lossless"
        self._int_lossy_buf = b"lossless"
        self._arguments.mz_lossy = self._mz_lossy_buf
        self._arguments.int_lossy = self._int_lossy_buf
        self._arguments.blocksize = <long>1e+8
        self._arguments.mz_scale_factor = 1000
        self._arguments.int_scale_factor = 0
        self._arguments.target_xml_format = _ZSTD_compression_
        self._arguments.target_mz_format = _ZSTD_compression_
        self._arguments.target_inten_format = _ZSTD_compression_
        self._arguments.zstd_compression_level = 3

    cdef Arguments* get_ptr(self):
        return &self._arguments

    @staticmethod
    cdef RuntimeArguments from_ptr(Arguments* ptr):
        cdef RuntimeArguments obj = RuntimeArguments.__new__(RuntimeArguments)
        obj._arguments = ptr[0] # Dereference the pointer
        return obj

    property threads:
        def __get__(self):
            return self._arguments.threads
        def __set__(self, value):
            self._arguments.threads = value
    
    property blocksize:
        def __get__(self):
            return self._arguments.blocksize
        def __set__(self, value):
            self._arguments.blocksize = value

    property mz_scale_factor:
        def __get__(self):
            return self._arguments.mz_scale_factor
        def __set__(self, value):
            self._arguments.mz_scale_factor = value
    
    property int_scale_factor:
        def __get__(self):
            return self._arguments.int_scale_factor
        def __set__(self, value):
            self._arguments.int_scale_factor = value
        
    property target_xml_format:
        def __get__(self):
            return self._arguments.target_xml_format
        def __set__(self, value):
            self._arguments.target_xml_format = value
        
    property target_mz_format:
        def __get__(self):
            return self._arguments.target_mz_format
        def __set__(self, value):
            self._arguments.target_mz_format = value
    
    property target_inten_format:
        def __get__(self):
            return self._arguments.target_inten_format
        def __set__(self, value):
            self._arguments.target_inten_format = value
    
    property mz_lossy:
        def __get__(self):
            return self._arguments.mz_lossy.decode('utf-8') if isinstance(self._arguments.mz_lossy, bytes) else self._arguments.mz_lossy
        def __set__(self, value):
            if isinstance(value, str):
                value_bytes = value.encode('utf-8')
            else:
                value_bytes = value
            value_str = value_bytes.decode('utf-8') if isinstance(value_bytes, bytes) else value_bytes
            if value_str != "lossless":
                valid = False
                for i in range(algo_registry_size):
                    if algo_registry[i].name == value_bytes and (algo_registry[i].target & TARGET_MZ):
                        valid = True
                        if algo_registry[i].default_mz_scale != 0:
                            self._arguments.mz_scale_factor = algo_registry[i].default_mz_scale
                        break
                if not valid:
                    mz_algos = []
                    for i in range(algo_registry_size):
                        if algo_registry[i].target & TARGET_MZ:
                            mz_algos.append(algo_registry[i].name.decode('utf-8'))
                    raise ValueError(
                        f"Unknown m/z lossy algorithm '{value_str}'. "
                        f"Valid options: 'lossless', {', '.join(repr(a) for a in mz_algos)}"
                    )
            self._mz_lossy_buf = value_bytes
            self._arguments.mz_lossy = self._mz_lossy_buf

    property int_lossy:
        def __get__(self):
            return self._arguments.int_lossy.decode('utf-8') if isinstance(self._arguments.int_lossy, bytes) else self._arguments.int_lossy
        def __set__(self, value):
            if isinstance(value, str):
                value_bytes = value.encode('utf-8')
            else:
                value_bytes = value
            value_str = value_bytes.decode('utf-8') if isinstance(value_bytes, bytes) else value_bytes
            if value_str != "lossless":
                valid = False
                for i in range(algo_registry_size):
                    if algo_registry[i].name == value_bytes and (algo_registry[i].target & TARGET_INT):
                        valid = True
                        if algo_registry[i].default_int_scale != 0:
                            self._arguments.int_scale_factor = algo_registry[i].default_int_scale
                        break
                if not valid:
                    int_algos = []
                    for i in range(algo_registry_size):
                        if algo_registry[i].target & TARGET_INT:
                            int_algos.append(algo_registry[i].name.decode('utf-8'))
                    raise ValueError(
                        f"Unknown intensity lossy algorithm '{value_str}'. "
                        f"Valid options: 'lossless', {', '.join(repr(a) for a in int_algos)}"
                    )
            self._int_lossy_buf = value_bytes
            self._arguments.int_lossy = self._int_lossy_buf

    property zstd_compression_level:
        def __get__(self):
            return self._arguments.zstd_compression_level
        def __set__(self, value):
            self._arguments.zstd_compression_level = value


def list_algorithms():
    """Return a list of available lossy algorithm descriptors from the C registry.

    Each entry is a dict with keys: name, target, description,
    default_mz_scale, default_int_scale, experimental.
    """
    result = []
    for i in range(algo_registry_size):
        targets = []
        if algo_registry[i].target & TARGET_MZ:
            targets.append("mz")
        if algo_registry[i].target & TARGET_INT:
            targets.append("intensity")
        result.append({
            "name": algo_registry[i].name.decode('utf-8'),
            "target": targets,
            "description": algo_registry[i].description.decode('utf-8'),
            "default_mz_scale": algo_registry[i].default_mz_scale,
            "default_int_scale": algo_registry[i].default_int_scale,
            "experimental": bool(algo_registry[i].experimental),
        })
    return result


cdef class DataBlock:
    cdef data_block_t _data_block

    def __init__(self, char* mem, size_t size, size_t max_size):
        self._data_block.mem = mem
        self._data_block.size = size
        self._data_block.max_size = max_size


cdef class DataFormat:
    cdef data_format_t _data_format

    @staticmethod
    cdef DataFormat from_ptr(data_format_t* ptr):
        cdef DataFormat obj = DataFormat.__new__(DataFormat)
        obj._data_format = ptr[0]  # Dereference the pointer
        return obj

    property source_mz_fmt:
        def __get__(self) -> int:
            return self._data_format.source_mz_fmt

    property source_inten_fmt:
        def __get__(self) -> int:
            return self._data_format.source_inten_fmt

    property source_compression:
        def __get__(self) -> int:
            return self._data_format.source_compression

    property source_total_spec:
        def __get__(self) -> int:
            return self._data_format.source_total_spec

    property target_xml_format:
        def __get__(self) -> int:
            return self._data_format.target_xml_format

    property target_mz_format:
        def __get__(self) -> int:
            return self._data_format.target_mz_format
    
    property target_inten_format:
        def __get__(self) -> int:
            return self._data_format.target_inten_format

    def __str__(self):
        return f"DataFormat(source_mz_fmt={self.source_mz_fmt}, source_inten_fmt={self.source_inten_fmt}, source_compression={self.source_compression}, source_total_spec={self.source_total_spec})"

    def to_dict(self):
        return {
            'source_mz_fmt': 'MS:' + str(self._data_format.source_mz_fmt),
            'source_inten_fmt': 'MS:' + str(self._data_format.source_inten_fmt),
            'source_compression': 'MS:' + str(self._data_format.source_compression),
            'source_total_spec': self._data_format.source_total_spec
        }


cdef class DataPositions:
    cdef data_positions_t *data_positions
    
    @staticmethod
    cdef DataPositions from_ptr(data_positions_t* ptr):
        cdef DataPositions obj = DataPositions.__new__(DataPositions)
        obj.data_positions = ptr
        return obj
    
    property start_positions:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self.data_positions.total_spec
            return np.asarray(<uint64_t[:shape[0]]>self.data_positions.start_positions)
    
    property end_positions:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self.data_positions.total_spec
            return np.asarray(<uint64_t[:shape[0]]>self.data_positions.end_positions)
    
    property total_spec:
        def __get__(self) -> int:
            return self.data_positions.total_spec


cdef class Division:
    cdef division_t* _division

    @staticmethod
    cdef Division from_ptr(division_t* ptr):
        cdef Division obj = Division.__new__(Division)
        obj._division = ptr
        return obj

    property spectra:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.spectra)

    property xml:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.xml)

    property mz:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.mz)

    property inten:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.inten)

    property size:
        def __get__(self) -> int:
            return self._division.size

    property scans:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.mz.total_spec
            return np.asarray(<uint32_t[:shape[0]]>self._division.scans)

    property ms_levels:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.mz.total_spec
            return np.asarray(<uint16_t[:shape[0]]>self._division.ms_levels)

    property ret_times:
        def __get__(self) -> np.ndarray:
            if self._division.ret_times is NULL:
                return None
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.mz.total_spec
            return np.asarray(<float[:shape[0]]>self._division.ret_times)


cdef class MZMLFile(BaseFile):
    def __init__(self, bytes path):
        super(MZMLFile, self).__init__(path)
        if self._mapping is NULL:
             raise RuntimeError("File mapping is NULL. Filesize might be 0.")
        self._df = _pattern_detect(<char*> self._mapping)
        if self._df is NULL:
             raise RuntimeError("pattern_detect returned NULL. Failed to detect mzML pattern.")
        
        self._positions = _scan_mzml(<char*> self._mapping, self._df, self.filesize, 7) # 7 = MSLEVEL|SCANNUM|RETTIME
        if self._positions is NULL:
             raise RuntimeError("scan_mzml returned NULL. The file might be empty or invalid.")
        _set_compress_runtime_variables(self._arguments.get_ptr(), self._df)

    @staticmethod
    def _reopen(path: bytes):
        return MZMLFile(path)

    def _cleanup(self):
        """Free MZMLFile-specific resources before calling parent cleanup"""
        # Free divisions created by _prepare_divisions (fully malloc'd)
        if self._divisions != NULL:
            _dealloc_divisions(self._divisions)
            self._divisions = NULL
        # Call parent cleanup
        super(MZMLFile, self)._cleanup()

    def _prepare_divisions(self):
        cdef long n_divisions = _determine_n_divisions(self._positions.size, self._arguments.blocksize)
        if n_divisions > self._positions.mz.total_spec:  # If we have more divisions than spectra, decrease number of divisions
            warnings.warn(
                f"n_divisions ({n_divisions}) > total_spec ({self._positions.mz.total_spec}). "
                f"Setting n_divisions to {self._positions.mz.total_spec}"
            )
            n_divisions = self._positions.mz.total_spec
            if n_divisions == 0:
                n_divisions = 1
            self._divisions = _create_divisions(self._positions, n_divisions)
        elif n_divisions >= self._arguments.threads:
            self._divisions = _create_divisions(self._positions, n_divisions)
        else:
            effective_divisions = self._arguments.threads
            if effective_divisions > self._positions.mz.total_spec:
                warnings.warn(
                    f"n_threads ({effective_divisions}) > total_spec ({self._positions.mz.total_spec}). "
                    f"Setting n_divisions to {self._positions.mz.total_spec}"
                )
                effective_divisions = self._positions.mz.total_spec
                if effective_divisions == 0:
                    effective_divisions = 1
            self._divisions = _create_divisions(self._positions, effective_divisions)
            # If we have more threads than divisions, increase the blocksize to max division size
            self._arguments.blocksize = _get_division_size_max(self._divisions)

    def _validate_scale_factors(self):
        cdef char err_buf[512]
        err_buf[0] = b'\0'
        if _validate_args(self._arguments.get_ptr(), err_buf, sizeof(err_buf)) != 0:
            msg = err_buf.decode('utf-8').strip() if err_buf[0] != b'\0' else ""
            raise ValueError(msg or "Invalid argument configuration.")

    def compress(self, output: Union[str, PathLike]) -> MSZFile:
        output = os.fspath(output)
        self._validate_scale_factors()
        self._prepare_divisions()
        self.output_fd = self._prepare_output_fd(output)
        cdef int rv
        cdef char* mapping = <char*> self._mapping
        cdef size_t filesize = self.filesize
        cdef Arguments* args = self._arguments.get_ptr()
        cdef data_format_t* df = self._df
        cdef divisions_t* divisions = self._divisions
        cdef int fd = self.output_fd
        # Release the GIL so worker threads can re-enter Python via the
        # error/warning callbacks (declared `with gil`). Without this, a worker
        # that hits a base64 decode failure (or any other warning path) blocks
        # in PyGILState_Ensure while the main thread is parked in pthread_join,
        # producing an unrecoverable deadlock.
        _clear_pending_error()
        with nogil:
            rv = _compress_mzml(mapping, filesize, args, df, divisions, fd)
        _flush(self.output_fd)
        _close_file(self.output_fd)
        self.output_fd = -1
        _raise_pending_error()
        if rv != 0:
            raise RuntimeError("Compression failed: write error during compression.")
        return MSZFile(output.encode('utf-8'))

    def _compress_to_fd(self, int write_fd):
        """Internal helper: run compression pipeline writing to the given fd."""
        self._validate_scale_factors()
        cdef int rv
        cdef char* mapping = <char*> self._mapping
        cdef size_t filesize = self.filesize
        cdef Arguments* args = self._arguments.get_ptr()
        cdef data_format_t* df = self._df
        cdef divisions_t* divisions = self._divisions
        # Release the GIL so the reader thread can drain the pipe; without this,
        # write() blocks on a full pipe while holding the GIL → deadlock.
        _clear_pending_error()
        with nogil:
            rv = _compress_mzml(mapping, filesize, args, df, divisions, write_fd)
            _flush(write_fd)
        _raise_pending_error()
        if rv != 0:
            raise RuntimeError("Compression failed: write error during compression.")

    def compress_stream(self, chunk_size: int = 1_048_576) -> Iterator[bytes]:
        """Compress this mzML file and yield the MSZ output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of compressed MSZ data.
        """
        self._prepare_divisions()
        return _pipe_stream(self._compress_to_fd, (), chunk_size=chunk_size)

    def extract(
        self,
        output: Union[str, PathLike],
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None
    ) -> Union[MZMLFile, MSZFile]:
        """
        Extract spectra from an mzML file to mzML format with optional filtering.
        
        Args:
            output: Path to the output file. Must have .mzml extension (or .msz).
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
        
        Raises:
            ValueError: If output file extension is not supported.
        """
        cdef long* c_indicies = NULL
        cdef long indicies_length = 0
        cdef uint32_t* c_scans = NULL
        cdef long scans_length = 0
        cdef uint16_t c_ms_level = 0
        cdef np.ndarray indicies_arr
        cdef np.ndarray[np.uint32_t, ndim=1] scans_arr

        output = Path(output).resolve()
        output_ext = output.suffix.lower()

        if output_ext == '.msz':
            # Output as MSZ (compressed)
            # Create a unique temporary directory to avoid collisions in concurrent extractions
            temp_dir = tempfile.mkdtemp()
            temp_mzml_path = Path(temp_dir) / f"{output.stem}_temp.mzML"

            # Delete output file if it already exists
            if output.exists():
                output.unlink()

            # Extract to temporary mzML file
            temp_mzml = None
            try:
                self.extract(temp_mzml_path, indicies=indicies, scan_numbers=scan_numbers, ms_level=ms_level)

                # Compress the temporary mzML to MSZ
                temp_mzml = MZMLFile(str(temp_mzml_path).encode('utf-8'))
                return temp_mzml.compress(str(output))
            finally:
                if temp_mzml is not None:
                    temp_mzml._cleanup()
                shutil.rmtree(temp_dir, ignore_errors=True)

        elif output_ext == '.mzml':
             # Convert Python lists to C arrays
            # Use _c_long_dtype to match C long type (32-bit on Windows, 64-bit on Linux)
            if indicies is not None:
                indicies_arr = np.array(indicies, dtype=_c_long_dtype)
                c_indicies = <long*>indicies_arr.data
                indicies_length = len(indicies)

            if scan_numbers is not None:
                scans_arr = np.array(scan_numbers, dtype=np.uint32)
                c_scans = <uint32_t*>scans_arr.data
                scans_length = len(scan_numbers)

            if ms_level is not None:
                c_ms_level = <uint16_t>ms_level

            # Prepare output file
            self.output_fd = self._prepare_output_fd(str(output))

            _clear_pending_error()
            _extract_mzml_filtered(
                <char*>self._mapping,
                self.filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                self._positions,
                self.output_fd
            )

            # Flush and close output
            _flush(self.output_fd)
            _close_file(self.output_fd)
            self.output_fd = -1
            _raise_pending_error()
            return MZMLFile(str(output).encode('utf-8'))
        else:
            raise ValueError(f"Unsupported output file extension: {output_ext}. Use .msz or .mzML")

    def _extract_mzml_to_fd(self, indicies_arr, scans_arr, uint16_t c_ms_level, int write_fd):
        """Internal helper: run mzML extraction writing to the given fd."""
        cdef long* c_indicies = NULL
        cdef long indicies_length = 0
        cdef uint32_t* c_scans = NULL
        cdef long scans_length = 0
        cdef char* mapping = <char*>self._mapping
        cdef size_t filesize = self.filesize
        cdef division_t* positions = self._positions

        if indicies_arr is not None:
            c_indicies = <long*>(<np.ndarray>indicies_arr).data
            indicies_length = len(indicies_arr)

        if scans_arr is not None:
            c_scans = <uint32_t*>(<np.ndarray[np.uint32_t, ndim=1]>scans_arr).data
            scans_length = len(scans_arr)

        # Release the GIL so the reader thread can drain the pipe.
        _clear_pending_error()
        with nogil:
            _extract_mzml_filtered(
                mapping,
                filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                positions,
                write_fd
            )
            _flush(write_fd)
        _raise_pending_error()

    def extract_stream(
        self,
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None,
        chunk_size: int = 1_048_576
    ) -> Iterator[bytes]:
        """Extract spectra from this mzML file and yield mzML output as byte chunks.

        Args:
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of extracted mzML data.
        """
        indicies_arr = None
        scans_arr = None
        cdef uint16_t c_ms_level = 0

        if indicies is not None:
            indicies_arr = np.array(indicies, dtype=_c_long_dtype)
        if scan_numbers is not None:
            scans_arr = np.array(scan_numbers, dtype=np.uint32)
        if ms_level is not None:
            c_ms_level = <uint16_t>ms_level

        return _pipe_stream(self._extract_mzml_to_fd, (indicies_arr, scans_arr, c_ms_level), chunk_size=chunk_size)

    def get_mz_binary(self, size_t index):
        cdef char* dest = NULL
        cdef char* decode_output = NULL  # Track decode function's allocation
        cdef size_t out_len = 0
        cdef data_block_t* tmp = _alloc_data_block(self._arguments.blocksize)
        cdef char* mapping_ptr
        cdef size_t start, end
        cdef np.ndarray[np.float64_t, ndim=1] mz_array_64
        cdef np.ndarray[np.float32_t, ndim=1] mz_array_32
        cdef double* double_ptr
        cdef float* float_ptr

        start = self._positions.mz.start_positions[index]
        end = self._positions.mz.end_positions[index]

        mapping_ptr = <char*>self._mapping
        mapping_ptr += start

        self._df.decode_source_compression_mz_fun(self._z, mapping_ptr, end - start, &dest, &out_len, tmp)
        decode_output = dest  # Save pointer to decode function's allocation

        dest += ZLIB_SIZE_OFFSET # Skip zlib header

        try:
            if self._df.source_mz_fmt == _64d_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 8)
                double_ptr = <double*>dest

                if out_len > 0:
                    # Copy data into numpy-owned array
                    mz_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(<void*>mz_array_64.data, <void*>double_ptr, count * 8)
                    return mz_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_mz_fmt == _32f_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 4)
                float_ptr = <float*>dest

                if out_len > 0:
                    # Copy data into numpy-owned array
                    mz_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(<void*>mz_array_32.data, <void*>float_ptr, count * 4)
                    return mz_array_32
                else:
                    return np.array([], dtype=np.float32)
            else:
                raise NotImplementedError("Data format not implemented.")
        finally:
            free(decode_output)
            _dealloc_data_block(tmp)

    def get_inten_binary(self, size_t index):
        cdef char* dest = NULL
        cdef char* decode_output = NULL  # Track decode function's allocation
        cdef size_t out_len = 0
        cdef data_block_t* tmp = _alloc_data_block(self._arguments.blocksize)
        cdef char* mapping_ptr
        cdef size_t start, end
        cdef np.ndarray[np.float64_t, ndim=1] inten_array_64
        cdef np.ndarray[np.float32_t, ndim=1] inten_array_32
        cdef double* double_ptr
        cdef float* float_ptr

        start = self._positions.inten.start_positions[index]
        end = self._positions.inten.end_positions[index]

        mapping_ptr = <char*>self._mapping
        mapping_ptr += start

        self._df.decode_source_compression_inten_fun(self._z, mapping_ptr, end - start, &dest, &out_len, tmp)
        decode_output = dest  # Save pointer to decode function's allocation

        dest += ZLIB_SIZE_OFFSET # Skip zlib header

        try:
            if self._df.source_inten_fmt == _64d_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 8)
                double_ptr = <double*>dest

                if out_len > 0:
                    # Copy data into numpy-owned array
                    inten_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(<void*>inten_array_64.data, <void*>double_ptr, count * 8)
                    return inten_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_inten_fmt == _32f_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 4)
                float_ptr = <float*>dest

                if out_len > 0:
                    # Copy data into numpy-owned array
                    inten_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(<void*>inten_array_32.data, <void*>float_ptr, count * 4)
                    return inten_array_32
                else:
                    return np.array([], dtype=np.float32)
            else:
                raise NotImplementedError("Data format not implemented.")
        finally:
            free(decode_output)
            _dealloc_data_block(tmp)

    
    def get_xml(self, size_t index):
        cdef char* res
        cdef char* mapping_ptr

        start = self._positions.spectra.start_positions[index]
        end = self._positions.spectra.end_positions[index]

        size = end-start

        mapping_ptr = <char*>self._mapping
        mapping_ptr += start

        res = <char*>malloc(size + 1)

        memcpy(res, <const void*> mapping_ptr, size)

        res[size] = '\0'

        result_str = res.decode('utf-8')

        free(res)

        element = fromstring(result_str)

        return element


cdef class MSZFile(BaseFile):
    cdef footer_t* _footer
    cdef ZSTD_DCtx* _dctx
    cdef block_len_queue_t* _xml_block_lens
    cdef block_len_queue_t* _mz_binary_block_lens
    cdef block_len_queue_t* _inten_binary_block_lens
    cdef BlockCache _block_cache   # shared/owned decompressed-block cache;
                                   # None disables bounding (legacy unbounded)
    # Prefix sum of per-division spectrum counts (length n_divisions+1) used to
    # resolve a global spectrum index -> (division, local index) WITHOUT
    # materializing the flattened per-spectrum position arrays. _positions (the
    # flattened division) is now built lazily, only if .positions/.describe()
    # are accessed. This is the ~22.6 GB/90-shard metadata saving.
    cdef long* _div_spec_offsets

    def __init__(self, bytes path):
        super(MSZFile, self).__init__(path)
        self._block_cache = _coerce_cache(None)  # read() may override
        self._div_spec_offsets = NULL
        self._df = _get_header_df(self._mapping)
        self._footer = _read_footer(self._mapping, self.filesize)
        self._divisions = _read_divisions(self._mapping, self._footer.divisions_t_pos, self._footer.n_divisions)
        self._positions = NULL  # built lazily via _ensure_positions()
        self._build_offsets()
        self._dctx = _alloc_dctx()
        self._xml_block_lens = _read_block_len_queue(self._mapping, self._footer.xml_blk_pos, self._footer.mz_binary_blk_pos)
        self._mz_binary_block_lens = _read_block_len_queue(self._mapping, self._footer.mz_binary_blk_pos, self._footer.inten_binary_blk_pos)
        self._inten_binary_block_lens = _read_block_len_queue(self._mapping, self._footer.inten_binary_blk_pos, self._footer.divisions_t_pos)
        _set_decompress_runtime_variables(self._df, self._footer)

    cdef block_lru_t* _lru_ptr(self):
        """The shared LRU pointer for extract calls, or NULL if bounding is
        disabled (cache=0)."""
        if self._block_cache is None:
            return NULL
        return self._block_cache._lru

    def _set_block_cache(self, cache):
        """Install the block cache, coercing None/int/0/BlockCache the same way
        the old ``cache=`` constructor argument did. Used by ``read()``, which
        is the only public entry point for the knob. Must be called before any
        spectrum/block access so no blocks are attached under the old cache."""
        self._block_cache = _coerce_cache(cache)

    cdef void _build_offsets(self):
        """Build the per-division spectrum-count prefix sum so a global index
        resolves to (division, local) in O(log n_divisions) without flattening
        the per-spectrum position arrays into one contiguous heap copy."""
        cdef int n = self._divisions.n_divisions
        self._div_spec_offsets = <long*>malloc(sizeof(long) * (n + 1))
        if self._div_spec_offsets == NULL:
            raise MemoryError("MSZFile._build_offsets: malloc failed")
        cdef long acc = 0
        cdef int i
        for i in range(n):
            self._div_spec_offsets[i] = acc
            acc += self._divisions.divisions[i].mz.total_spec
        self._div_spec_offsets[n] = acc

    cdef int _resolve_division(self, size_t index, size_t* local) except -1:
        """Resolve a global spectrum index to its division index, writing the
        in-division offset to *local. Binary search over the prefix sum."""
        cdef int lo = 0
        cdef int hi = self._divisions.n_divisions - 1
        cdef int mid
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if <size_t>self._div_spec_offsets[mid] <= index:
                lo = mid
            else:
                hi = mid - 1
        local[0] = index - <size_t>self._div_spec_offsets[lo]
        return lo

    cdef int _spectrum_meta(self, size_t index, uint32_t* scan,
                            uint16_t* ms_level, float* ret_time,
                            bint* has_rt) except -1:
        """Per-spectrum metadata for the given global index, read straight from
        the mmap-backed per-division scan/ms_level arrays (no flattened copy).
        MSZ stores no per-spectrum retention times, so has_rt is always 0."""
        cdef size_t local = 0
        cdef int d = self._resolve_division(index, &local)
        cdef division_t* div = self._divisions.divisions[d]
        scan[0] = div.scans[local]
        ms_level[0] = div.ms_levels[local]
        has_rt[0] = False
        return 0

    cdef void _ensure_positions(self):
        """Materialize the flattened position arrays on demand (only when
        .positions / describe() need a contiguous Division). Cached thereafter."""
        if self._positions == NULL:
            self._positions = _flatten_divisions(self._divisions)

    @staticmethod
    def _reopen(path: bytes):
        return MSZFile(path)

    @property
    def block_cache(self):
        """The shared ``BlockCache`` bounding this file's decompressed blocks,
        or ``None`` if bounding is disabled (opened with ``cache=0``)."""
        return self._block_cache

    def clear_cache(self):
        """Evict this file's decompressed blocks from the shared cache.

        Frees the per-block buffers held on this file's behalf without closing
        the file; subsequent reads re-decompress on demand. No-op when bounding
        is disabled (``cache=0``)."""
        cdef block_lru_t* lru = self._lru_ptr()
        if lru == NULL:
            return
        if self._xml_block_lens != NULL:
            _lru_remove_queue_blocks(lru, self._xml_block_lens)
        if self._mz_binary_block_lens != NULL:
            _lru_remove_queue_blocks(lru, self._mz_binary_block_lens)
        if self._inten_binary_block_lens != NULL:
            _lru_remove_queue_blocks(lru, self._inten_binary_block_lens)

    def _init_from_archive(self, bytes mszx_path, bytes entry_name):
        """
        Populate this instance's BaseFile + MSZFile state from an MSZX
        archive entry. Used by both MSZFile.from_mszx and MSZXFile.open
        to share the offset-mmap construction path.
        """
        if not os.path.exists(mszx_path):
            raise FileNotFoundError(f"File not found: {os.fsdecode(mszx_path)}")

        cdef int fd = _open_input_file(mszx_path)
        if fd < 0:
            raise OSError(f"Could not open MSZX archive: {os.fsdecode(mszx_path)}")

        cdef int64_t off = 0
        cdef size_t sz = 0
        if _find_tar_entry(fd, entry_name, &off, &sz) != 0:
            _close_file(fd)
            raise ValueError(
                f"Entry '{os.fsdecode(entry_name)}' not found in archive "
                f"or archive is not a valid USTAR tar"
            )

        cdef size_t pad = 0
        cdef size_t maplen = 0
        cdef void* base = _get_mapping_range(fd, off, sz, &pad, &maplen)
        if base == NULL:
            _close_file(fd)
            raise ValueError(
                "MSZX archive is truncated or corrupted (mmap range exceeds file size)"
            )

        # Populate BaseFile fields manually (bypassing __init__).
        self._path = mszx_path
        self._fd = fd
        self._map_base = base
        self._map_length = maplen
        self._mapping = (<char*>base) + pad
        self.filesize = sz
        self._opened_from_archive = True
        self._spectra = None
        self._cache_spectra = CACHE_SPECTRA_AUTO  # read() may override
        self._block_cache = _coerce_cache(None)   # read() may override
        self._arguments = RuntimeArguments()
        self._z = _alloc_z_stream()
        self.output_fd = -1
        self._df = NULL
        self._divisions = NULL
        self._positions = NULL
        self._div_spec_offsets = NULL

        # Run the MSZFile-specific init body.
        self._df = _get_header_df(self._mapping)
        self._footer = _read_footer(self._mapping, self.filesize)
        self._divisions = _read_divisions(self._mapping, self._footer.divisions_t_pos, self._footer.n_divisions)
        self._positions = NULL  # built lazily via _ensure_positions()
        self._build_offsets()
        self._dctx = _alloc_dctx()
        self._xml_block_lens = _read_block_len_queue(self._mapping, self._footer.xml_blk_pos, self._footer.mz_binary_blk_pos)
        self._mz_binary_block_lens = _read_block_len_queue(self._mapping, self._footer.mz_binary_blk_pos, self._footer.inten_binary_blk_pos)
        self._inten_binary_block_lens = _read_block_len_queue(self._mapping, self._footer.inten_binary_blk_pos, self._footer.divisions_t_pos)
        _set_decompress_runtime_variables(self._df, self._footer)

    @classmethod
    def from_mszx(cls, bytes mszx_path, bytes entry_name):
        """
        Open an MSZ file embedded inside an MSZX (tar) archive without
        extracting it to disk. Mmap's the archive at the entry's byte
        offset (page-aligned).

        Parameters
        ----------
        mszx_path : bytes
            Path to the .mszx archive.
        entry_name : bytes
            Name of the MSZ entry within the archive (e.g. b"spectra.msz").
        """
        cdef MSZFile self = cls.__new__(cls)
        self._init_from_archive(mszx_path, entry_name)
        return self

    def __reduce__(self):
        if self._opened_from_archive:
            raise TypeError(
                "MSZFile opened from MSZX cannot be pickled. "
                "Re-open the .mszx archive in the worker process instead."
            )
        return (self.__class__._reopen, (self._path,))

    def _cleanup(self):
        """Free MSZFile-specific resources before calling parent cleanup"""

        # Free ZSTD_DCtx
        if self._dctx != NULL:
            ZSTD_freeDCtx(self._dctx)
            self._dctx = NULL

        # Free the division prefix-sum table.
        if self._div_spec_offsets != NULL:
            free(self._div_spec_offsets)
            self._div_spec_offsets = NULL

        # Detach this file's blocks from the shared cache BEFORE freeing the
        # queues, or the shared LRU would retain dangling pointers into freed
        # nodes. This also frees any cache buffers those blocks still hold and
        # decrements the cache's byte usage. Safe when cache is disabled (None).
        cdef block_lru_t* lru = self._lru_ptr()
        if lru != NULL:
            if self._xml_block_lens != NULL:
                _lru_remove_queue_blocks(lru, self._xml_block_lens)
            if self._mz_binary_block_lens != NULL:
                _lru_remove_queue_blocks(lru, self._mz_binary_block_lens)
            if self._inten_binary_block_lens != NULL:
                _lru_remove_queue_blocks(lru, self._inten_binary_block_lens)
        # Drop our reference to the shared cache (it outlives us if shared).
        self._block_cache = None

        # Free block length queues
        if self._xml_block_lens != NULL:
            _dealloc_block_len_queue(self._xml_block_lens)
            self._xml_block_lens = NULL
        if self._mz_binary_block_lens != NULL:
            _dealloc_block_len_queue(self._mz_binary_block_lens)
            self._mz_binary_block_lens = NULL
        if self._inten_binary_block_lens != NULL:
            _dealloc_block_len_queue(self._inten_binary_block_lens)
            self._inten_binary_block_lens = NULL

        # Free divisions from read_divisions (mmap-backed, only free struct wrappers)
        if self._divisions != NULL:
            _dealloc_read_divisions(self._divisions)
            self._divisions = NULL

        # _footer points to mmap'd memory, don't free

        # Call parent cleanup
        super(MSZFile, self)._cleanup()
    
    def decompress(self, output: Union[str, PathLike]) -> MZMLFile:
        output = os.fspath(output)
        self.output_fd = self._prepare_output_fd(output)
        cdef int rv
        cdef char* mapping = <char*>self._mapping
        cdef size_t filesize = self.filesize
        cdef Arguments* args = self._arguments.get_ptr()
        cdef int fd = self.output_fd
        # Release the GIL so worker threads can re-enter Python via the
        # error/warning callbacks. See MZMLFile.compress for the deadlock
        # this avoids.
        _clear_pending_error()
        with nogil:
            rv = _decompress_msz(mapping, filesize, args, fd)
        _flush(self.output_fd)
        _close_file(self.output_fd)
        self.output_fd = -1
        _raise_pending_error()
        if rv != 0:
            raise RuntimeError("Decompression failed: error during decompression.")
        return MZMLFile(output.encode('utf-8'))

    def _decompress_to_fd(self, int write_fd):
        """Internal helper: run decompression pipeline writing to the given fd."""
        cdef int rv
        cdef char* mapping = <char*>self._mapping
        cdef size_t filesize = self.filesize
        cdef Arguments* args = self._arguments.get_ptr()
        # Release the GIL so the reader thread can drain the pipe.
        _clear_pending_error()
        with nogil:
            rv = _decompress_msz(mapping, filesize, args, write_fd)
            _flush(write_fd)
        _raise_pending_error()
        if rv != 0:
            raise RuntimeError("Decompression failed: error during decompression.")

    def decompress_stream(self, chunk_size: int = 1_048_576) -> Iterator[bytes]:
        """Decompress this MSZ file and yield the mzML output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of decompressed mzML data.
        """
        return _pipe_stream(self._decompress_to_fd, (), chunk_size=chunk_size)

    def extract(
        self,
        output: Union[str, PathLike],
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None
    ) -> Union[MZMLFile, MSZFile]:
        """
        Extract spectra from an MSZ file to mzML format with optional filtering.
        
        Args:
            output: Path to the output file. Must have .mzml extension.
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
        
        Raises:
            ValueError: If output file extension is not supported.
            NotImplementedError: If MSZ output format is requested.
        """
        cdef long* c_indicies = NULL
        cdef long indicies_length = 0
        cdef uint32_t* c_scans = NULL
        cdef long scans_length = 0
        cdef uint16_t c_ms_level = 0
        cdef np.ndarray indicies_arr
        cdef np.ndarray[np.uint32_t, ndim=1] scans_arr
        
        # Determine if output will be mzML or MSZ based on file extension
        output = Path(output).resolve()  # Convert to absolute path
        output_ext = output.suffix.lower()
        
        if output_ext == '.msz':
            # Output as MSZ (compressed)
            # Create a unique temporary directory to avoid collisions in concurrent extractions
            temp_dir = tempfile.mkdtemp()
            temp_mzml_path = Path(temp_dir) / f"{output.stem}_temp.mzML"

            # Delete output file if it already exists to avoid stale data
            if output.exists():
                output.unlink()

            # Extract to temporary mzML file. Use unbound MSZFile.extract
            # to avoid re-dispatching through a subclass override (e.g.,
            # MSZXFile.extract) and recursing infinitely.
            temp_mzml = None
            try:
                MSZFile.extract(self, temp_mzml_path, indicies=indicies, scan_numbers=scan_numbers, ms_level=ms_level)

                # Compress the temporary mzML to MSZ
                temp_mzml = MZMLFile(str(temp_mzml_path).encode('utf-8'))
                return temp_mzml.compress(str(output))
            finally:
                # Clean up MZMLFile's memory mapping before deleting the temp directory
                if temp_mzml is not None:
                    temp_mzml._cleanup()
                shutil.rmtree(temp_dir, ignore_errors=True)
        
        elif output_ext == '.mzml':
            # Convert Python lists to C arrays
            # Use _c_long_dtype to match C long type (32-bit on Windows, 64-bit on Linux)
            if indicies is not None:
                indicies_arr = np.array(indicies, dtype=_c_long_dtype)
                c_indicies = <long*>indicies_arr.data
                indicies_length = len(indicies)
            
            if scan_numbers is not None:
                scans_arr = np.array(scan_numbers, dtype=np.uint32)
                c_scans = <uint32_t*>scans_arr.data
                scans_length = len(scan_numbers)
            
            if ms_level is not None:
                c_ms_level = <uint16_t>ms_level
            
            # Prepare output file
            self.output_fd = self._prepare_output_fd(str(output))
            
            # Call the C extraction function
            _clear_pending_error()
            _extract_msz(
                <char*>self._mapping,
                self.filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                self.output_fd
            )

            # Flush and close output
            _flush(self.output_fd)
            _close_file(self.output_fd)
            self.output_fd = -1
            _raise_pending_error()
            return MZMLFile(str(output).encode('utf-8'))
        else:
            raise ValueError(f"Unsupported output file extension: {output_ext}. Use .msz or .mzML")

    def _extract_msz_to_fd(self, indicies_arr, scans_arr, uint16_t c_ms_level, int write_fd):
        """Internal helper: run MSZ extraction writing to the given fd."""
        cdef long* c_indicies = NULL
        cdef long indicies_length = 0
        cdef uint32_t* c_scans = NULL
        cdef long scans_length = 0
        cdef char* mapping = <char*>self._mapping
        cdef size_t filesize = self.filesize

        if indicies_arr is not None:
            c_indicies = <long*>(<np.ndarray>indicies_arr).data
            indicies_length = len(indicies_arr)

        if scans_arr is not None:
            c_scans = <uint32_t*>(<np.ndarray[np.uint32_t, ndim=1]>scans_arr).data
            scans_length = len(scans_arr)

        # Release the GIL so the reader thread can drain the pipe.
        _clear_pending_error()
        with nogil:
            _extract_msz(
                mapping,
                filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                write_fd
            )
            _flush(write_fd)
        _raise_pending_error()

    def extract_stream(
        self,
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None,
        chunk_size: int = 1_048_576
    ) -> Iterator[bytes]:
        """Extract spectra from this MSZ file and yield mzML output as byte chunks.

        Args:
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of extracted mzML data.
        """
        indicies_arr = None
        scans_arr = None
        cdef uint16_t c_ms_level = 0

        if indicies is not None:
            indicies_arr = np.array(indicies, dtype=_c_long_dtype)
        if scan_numbers is not None:
            scans_arr = np.array(scan_numbers, dtype=np.uint32)
        if ms_level is not None:
            c_ms_level = <uint16_t>ms_level

        return _pipe_stream(self._extract_msz_to_fd, (indicies_arr, scans_arr, c_ms_level), chunk_size=chunk_size)

    def get_mz_binary(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef np.ndarray[np.float64_t, ndim=1] mz_array_64
        cdef np.ndarray[np.float32_t, ndim=1] mz_array_32
        cdef double* double_ptr
        cdef float* float_ptr

        res = _extract_spectrum_mz(<char*> self._mapping, self._dctx, self._df, self._mz_binary_block_lens, self._footer.mz_binary_pos, self._divisions, index, &out_len, FALSE, self._lru_ptr())

        if res == NULL:
            raise ValueError(f"Failed to extract m/z binary for index {index}")

        try:
            if self._df.source_mz_fmt == _64d_:
                count = int((out_len) / 8)
                double_ptr = <double*>res
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    mz_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(<void*>mz_array_64.data, <void*>double_ptr, count * 8)
                    return mz_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_mz_fmt == _32f_:
                count = int((out_len) / 4)
                float_ptr = <float*>res
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    mz_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(<void*>mz_array_32.data, <void*>float_ptr, count * 4)
                    return mz_array_32
                else:
                    return np.array([], dtype=np.float32)
        finally:
            # Free the C buffer returned by _extract_spectrum_mz
            free(res)
    
    
    def get_inten_binary(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef np.ndarray[np.float64_t, ndim=1] inten_array_64
        cdef np.ndarray[np.float32_t, ndim=1] inten_array_32
        cdef double* double_ptr
        cdef float* float_ptr

        res = _extract_spectrum_inten(<char*> self._mapping, self._dctx, self._df, self._inten_binary_block_lens, self._footer.inten_binary_pos, self._divisions, index, &out_len, FALSE, self._lru_ptr())

        if res == NULL:
            raise ValueError(f"Failed to extract intensity binary for index {index}")

        try:
            if self._df.source_inten_fmt == _64d_:
                count = int((out_len) / 8)
                double_ptr = <double*>res
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    inten_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(<void*>inten_array_64.data, <void*>double_ptr, count * 8)
                    return inten_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_inten_fmt == _32f_:
                count = int((out_len) / 4)
                float_ptr = <float*>res
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    inten_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(<void*>inten_array_32.data, <void*>float_ptr, count * 4)
                    return inten_array_32
                else:
                    return np.array([], dtype=np.float32)
        finally:
            # Free the C buffer returned by _extract_spectrum_inten
            free(res)

    
    def get_xml(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef long xml_pos, mz_pos, inten_pos
        cdef int mz_fmt, inten_fmt

        xml_pos = <long>self._footer.xml_pos
        mz_pos = <long>self._footer.mz_binary_pos
        inten_pos = <long>self._footer.inten_binary_pos
        mz_fmt = <int>self._footer.mz_fmt
        inten_fmt = <int>self._footer.inten_fmt

        res = _extract_spectra(
            <char*>self._mapping, self._dctx, self._df,
            self._xml_block_lens, self._mz_binary_block_lens,
            self._inten_binary_block_lens, xml_pos, mz_pos,
            inten_pos, mz_fmt, inten_fmt, self._divisions, index, &out_len,
            self._lru_ptr()
        )

        if res == NULL:
            raise ValueError(f"Failed to extract XML for index {index}")

        try:
            result_str = res.decode('utf-8')
        finally:
            # Free the C buffer returned by _extract_spectra
            free(res)

        element = fromstring(result_str)

        return element
    
    def get_header(self) -> str:
        """
        Extract the complete mzML header as a raw string from MSZ file.
        
        This function decompresses the first XML block and extracts the header portion
        (everything from the start of the file to the first spectrum element).
        
        Returns:
            str: The raw XML header string.
            
        Raises:
            RuntimeError: If header extraction fails.
        """
        cdef char* header_data = NULL
        cdef char* decmp_xml = NULL
        cdef size_t header_len = 0
        cdef division_t* first_division = NULL
        cdef block_len_t* xml_blk_len = NULL
        cdef long xml_blk_offset = 0
        
        try:
            # Get the first division
            first_division = self._divisions.divisions[0]
            
            if first_division == NULL:
                raise RuntimeError("Failed to access first division.")
            
            # Get the first XML block
            xml_blk_len = _get_block_by_index(self._xml_block_lens, 0)
            xml_blk_offset = self._footer.xml_pos
            
            # Decompress the XML block
            decmp_xml = <char*>_decmp_block(self._df.xml_decompression_fun, self._dctx, 
                                             self._mapping, xml_blk_offset, xml_blk_len)
            
            if decmp_xml == NULL:
                raise RuntimeError("Failed to decompress XML block for mzML header.")
            
            # Extract the header from decompressed XML
            header_data = _extract_mzml_header(decmp_xml, first_division, &header_len)
            
            if header_data == NULL:
                raise RuntimeError("Failed to extract mzML header.")
            
            # Convert to Python string
            header_str = header_data[:header_len].decode('utf-8', errors='replace')
            
            return header_str
        
        finally:
            # Free the allocated memory
            if header_data != NULL:
                free(header_data)
            if decmp_xml != NULL:
                free(decmp_xml)
    

cdef class MSZXFile(MSZFile):
    """
    First-class Cython reader for MSZX (tar) archives.

    Extends MSZFile so all properties and methods (`spectra`, `positions`,
    `decompress`, `extract`, `get_mz_binary`, etc.) work directly without
    going through a separate `.msz` accessor. The embedded MSZ payload is
    mmap'd in place via the offset-mmap path; annotation entries are
    eagerly cached as Python objects.

    Open via the `open()` classmethod. The MSZX-specific `extract()` and
    `decompress()` overrides handle filtered MSZX-archive output and
    directory-output mzML+annotations respectively.
    """
    cdef object _manifest                # MSZXManifest
    cdef dict _annotations               # {filename: PSMReader}
    cdef object _archive_path            # pathlib.Path
    cdef object _primary_annotation_reader
    cdef public bint _closed             # idempotent-close guard (visible to Python)

    @classmethod
    def open(cls, path):
        """
        Open an MSZX archive for reading.

        Mmaps the embedded MSZ payload directly out of the archive (no
        temp extraction). Annotation entries are read into memory via the
        existing tar-based reader and the tar handle is closed before
        returning.

        Args:
            path: Path to the .mszx file.
        """
        # Local imports break the circular module load between _core and
        # the Python-side annotation/tarfile machinery (mscompress.mszx
        # re-exports MSZXFile from _core, so it can't be imported eagerly
        # at the top of this file).
        import tarfile
        import warnings
        from mscompress.annotations import PSMReader, MSZXAnnotationFile
        from mscompress.mszx.metadata import MSZXManifest

        archive_path = Path(path)
        if not archive_path.exists():
            raise FileNotFoundError(f"MSZX file not found: {archive_path}")

        with tarfile.open(archive_path, "r") as tar:
            try:
                manifest_member = tar.getmember("manifest.json")
            except KeyError:
                raise ValueError("Invalid MSZX archive: missing manifest.json")

            manifest_file = tar.extractfile(manifest_member)
            if manifest_file is None:
                raise ValueError("Could not read manifest.json")

            manifest = MSZXManifest.from_json(manifest_file.read().decode("utf-8"))

            annotation_readers = {}
            for entry in manifest.annotations:
                try:
                    member = tar.getmember(entry.filename)
                    mszx_source = MSZXAnnotationFile(
                        mszx_file=tar,
                        member=member,
                        name=entry.filename,
                    )
                    reader = PSMReader(mszx_source)
                    annotation_readers[entry.filename] = reader
                except KeyError:
                    warnings.warn(
                        f"Annotation file '{entry.filename}' not found in archive",
                        UserWarning,
                        stacklevel=2,
                    )
                except (ValueError, Exception) as e:
                    warnings.warn(
                        f"Could not parse annotation file '{entry.filename}': {e}",
                        UserWarning,
                        stacklevel=2,
                    )

        # Construct and populate via the shared MSZ-from-archive helper.
        cdef MSZXFile self = cls.__new__(cls)
        self._init_from_archive(
            str(archive_path).encode(),
            manifest.spectra_file.encode(),
        )
        self._manifest = manifest
        self._annotations = annotation_readers
        self._archive_path = archive_path
        self._primary_annotation_reader = (
            next(iter(annotation_readers.values())) if annotation_readers else None
        )
        self._closed = False
        return self

    def close(self):
        """Close the archive and release all resources. Idempotent."""
        self._cleanup()

    def __reduce__(self):
        # MSZXFile re-opens cleanly from its archive path — unlike a bare
        # MSZFile-from-archive which can't reconstruct the entry name.
        return (MSZXFile.open, (self._archive_path,))

    def _cleanup(self):
        # Idempotent: subsequent calls are no-ops once the resources have
        # been released. The parent's _cleanup is also defensive (NULL
        # guards) but tracking _closed lets us short-circuit Python work.
        if self._closed:
            return
        self._closed = True
        # Drop Python references so the underlying data can be GC'd before
        # the C resources are torn down by the parent.
        self._annotations = None
        self._primary_annotation_reader = None
        self._manifest = None
        MSZFile._cleanup(self)

    @property
    def manifest(self):
        """The archive manifest."""
        return self._manifest

    @property
    def archive_path(self):
        """Path to the MSZX archive."""
        return self._archive_path

    @property
    def annotations(self):
        """Primary annotation reader, or None if no annotations."""
        return self._primary_annotation_reader

    @property
    def annotation_readers(self):
        """Dict of all annotation readers, keyed by filename."""
        return self._annotations

    @property
    def annotation_files(self):
        """List of annotation entries in the archive."""
        return self._manifest.annotations

    def get_annotation_reader(self, filename):
        """Get the annotation reader for a specific file by name."""
        if filename in self._annotations:
            return self._annotations[filename]
        raise KeyError(f"Annotation file not found: {filename}")

    def get_annotation_readers_by_format(self, format):
        """Get all annotation readers matching a specific format."""
        return [
            self._annotations[entry.filename]
            for entry in self._manifest.annotations
            if entry.format == format and entry.filename in self._annotations
        ]

    def describe(self):
        """Description including the archive metadata."""
        desc = MSZFile.describe(self)
        desc["archive"] = {
            "path": str(self._archive_path),
            "annotations": [sr.to_dict() for sr in self._manifest.annotations],
        }
        return desc

    def decompress(self, output):
        """
        Decompress the embedded MSZ to mzML.

        If `output` is an existing directory or has no file extension, the
        mzML is written inside it as `<archive_stem>.mzML` and any cached
        annotation readers are written alongside (using the entry's name
        with any trailing `.zst` suffix stripped). Otherwise, `output` is
        treated as a single file path and only the mzML is written.
        """
        out_path = Path(os.fspath(output))
        is_dir_output = out_path.is_dir() or out_path.suffix == ""

        if not is_dir_output:
            return MSZFile.decompress(self, output)

        out_path.mkdir(parents=True, exist_ok=True)
        mzml_path = out_path / f"{self._archive_path.stem}.mzML"
        result = MSZFile.decompress(self, mzml_path)

        for filename, reader in self._annotations.items():
            out_name = filename[:-4] if filename.endswith(".zst") else filename
            (out_path / out_name).write_bytes(reader.source.read())

        return result

    def extract(self, output, indicies=None, scan_numbers=None, ms_level=None):
        """
        Extract a subset of spectra and annotations to a new MSZX archive.

        Args:
            output: Path to the output .mszx file.
            indicies: List of spectrum indices to extract.
            scan_numbers: List of scan numbers to extract.
            ms_level: Filter by MS level.

        Returns:
            New MSZXFile instance for the created archive.
        """
        # Local imports avoid the _core <-> mszx circular at module load.
        import tempfile
        import warnings
        from mscompress.mszx import MSZXBuilder
        from mscompress.annotations import PSMReader

        output_path = Path(output)
        if not output_path.suffix:
            output_path = output_path.with_suffix(".mszx")

        target_scans = set()

        if scan_numbers:
            target_scans.update(scan_numbers)

        if indicies:
            all_scans = self.positions.scans
            for idx in indicies:
                if 0 <= idx < len(all_scans):
                    target_scans.add(int(all_scans[idx]))

        if ms_level is not None:
            all_scans = self.positions.scans
            all_levels = self.positions.ms_levels

            for i in range(len(all_scans)):
                if all_levels[i] == ms_level:
                    scan = int(all_scans[i])
                    is_match = True
                    if indicies and i not in indicies:
                        is_match = False
                    if scan_numbers and scan not in scan_numbers:
                        is_match = False
                    if is_match:
                        target_scans.add(scan)

            if indicies or scan_numbers:
                valid_scans_by_level = set()
                for i in range(len(all_scans)):
                    if all_levels[i] == ms_level:
                        valid_scans_by_level.add(int(all_scans[i]))
                target_scans.intersection_update(valid_scans_by_level)
            else:
                for i in range(len(all_scans)):
                    if all_levels[i] == ms_level:
                        target_scans.add(int(all_scans[i]))

        if not indicies and not scan_numbers and ms_level is None:
            all_scans = self.positions.scans
            target_scans.update(int(s) for s in all_scans)

        final_scan_list = sorted(target_scans)

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            temp_msz_path = temp_path / "temp.msz"

            MSZFile.extract(self, output=str(temp_msz_path), scan_numbers=final_scan_list)

            extracted_msz = MSZFile(str(temp_msz_path).encode("utf-8"))
            try:
                builder = MSZXBuilder(extracted_msz, compression=True)
                builder.set_description(self._manifest.description or "")

                for k, v in self._manifest.extra.items():
                    builder.set_extra(k, v)

                for filename, reader in self._annotations.items():
                    fname_path = Path(filename)
                    temp_filename = fname_path.stem if fname_path.suffix.lower() == ".zst" else filename
                    temp_ann_path = temp_path / temp_filename

                    try:
                        reader.filter_to_file(temp_ann_path, target_scans)
                        new_reader = PSMReader(temp_ann_path)
                        builder.add_annotations(new_reader)
                    except Exception as e:
                        warnings.warn(f"Failed to filter annotation {filename}: {e}")

                builder.save(output_path)
            finally:
                extracted_msz._cleanup()

            return MSZXFile.open(output_path)


cdef class BaseFile:
    """
    Parent class for MZMLFile and MSZFile classes. Provides common interfaces for both child classes.

    Properties:
    spectra:
        Returns a Spectra class iterator to represent and manage collections of spectra in both mzML and MSZ files.

    positions:
        Returns the Division class, repesenting the positions of spectra, m/z binaries, intensity binaries, and XML in a mzML or MSZ file.
    

    Methods:
    __init__(self, bytes path, size_t filesize, int fd):
        Initializes the base attributes for file classes. This includes input file mapping, runtime arguments, and zlib z_stream.
        Other attributes (_df, _positions, etc.) are expected to be implemented by child class, as implementation varies by file.
    
    _prepare_output_fd(self, path: Union[str,bytes])->:
        Prepares a output file for compression/decompression and returns an integer representing the open file descriptor.
    
    """
    cdef bytes _path
    cdef size_t filesize
    cdef int _fd
    cdef void* _mapping
    cdef void* _map_base       # true mmap base; equals _mapping for path-opened files,
                               # differs by _map_pad for offset-mmap'd files (e.g. MSZX)
    cdef size_t _map_length    # actual mapped length, used for unmap (≥ filesize)
    cdef bint _opened_from_archive
    cdef data_format_t* _df
    cdef divisions_t* _divisions
    cdef division_t* _positions
    cdef Spectra _spectra
    cdef public int _cache_spectra  # Cap forwarded to Spectra; CACHE_SPECTRA_AUTO
                                    # (-1) by default. Set by read() before the
                                    # first .spectra access; public so the
                                    # pure-Python read() factory can assign it.
    cdef RuntimeArguments _arguments
    cdef z_stream* _z
    cdef int output_fd


    def __init__(self, bytes path):
        self._path = path
        if not os.path.exists(path):
            raise FileNotFoundError(f"File not found: {os.fsdecode(path)}")
        self.filesize = _get_filesize(self._path)
        self._fd = _open_input_file(self._path)
        self._mapping = _get_mapping(self._fd)
        self._map_base = self._mapping
        self._map_length = self.filesize
        self._opened_from_archive = False
        self._spectra = None
        self._cache_spectra = CACHE_SPECTRA_AUTO  # read() may override
        self._arguments = RuntimeArguments()
        self._z = _alloc_z_stream()
        self.output_fd = -1
        # Initialize pointers to NULL to prevent undefined behavior
        self._df = NULL
        self._divisions = NULL
        self._positions = NULL

    cdef void _ensure_positions(self):
        """Ensure self._positions (the flattened Division) is materialized.

        Default: a no-op — subclasses whose _positions is populated at open
        (e.g. MZMLFile from scan_mzml) need nothing. MSZFile overrides this to
        flatten lazily, since it now resolves spectrum metadata without the
        flattened arrays."""
        pass

    cdef int _spectrum_meta(self, size_t index, uint32_t* scan,
                            uint16_t* ms_level, float* ret_time,
                            bint* has_rt) except -1:
        """Per-spectrum metadata (scan number, MS level, retention time) for a
        global index. Default reads from the flat self._positions division;
        MSZFile overrides to read from the mmap-backed per-division arrays
        without flattening. has_rt=0 means no retention time (caller uses NaN)."""
        if self._positions == NULL:
            raise RuntimeError("_spectrum_meta: positions unavailable")
        scan[0] = self._positions.scans[index]
        ms_level[0] = self._positions.ms_levels[index]
        if self._positions.ret_times != NULL:
            ret_time[0] = self._positions.ret_times[index]
            has_rt[0] = True
        else:
            has_rt[0] = False
        return 0


    def __enter__(self):
        # Only open if not already open (e.g., from __init__)
        if self._fd <= 0 or self._mapping == NULL:
            self.filesize = _get_filesize(self._path)
            self._fd = _open_input_file(self._path)
            self._mapping = _get_mapping(self._fd)
        return self
    

    def __exit__(self, exc_type, exc_value, traceback):
        self._cleanup()
    
    def __del__(self):
        # Ensure file handles are released when object is garbage collected
        self._cleanup()
    
    def __reduce__(self):
        return (self.__class__._reopen, (self._path,))

    @staticmethod
    def _reopen(path: bytes):
        fs = _get_filesize(path)
        fd = _open_input_file(path)
        return BaseFile(path)

    def _cleanup(self):
        # On Windows, unmap must happen before closing the file descriptor
        # Unmap file mapping if exists. For offset-mmap'd files (MSZX), unmap
        # the true base + actual mapped length, not the user-facing pointer.
        if self._map_base != NULL:
            _remove_mapping(self._map_base, self._map_length)
            self._map_base = NULL
            self._mapping = NULL
        elif self._mapping != NULL:
            _remove_mapping(self._mapping, self.filesize)
            self._mapping = NULL

        # Close input file descriptor if open
        if self._fd >= 0:
            _close_file(self._fd)
            self._fd = -1

        # Close output file descriptor if open
        if self.output_fd >= 0:
            _close_file(self.output_fd)
            self.output_fd = -1

        # Free zlib z_stream
        if self._z != NULL:
            _dealloc_z_stream(self._z)
            self._z = NULL

        # Free data_format_t
        if self._df != NULL:
            _dealloc_df(self._df)
            self._df = NULL

        # Free division_t _positions (always fully malloc'd from scan_mzml or flatten_divisions)
        if self._positions != NULL:
            _dealloc_division(self._positions)
            self._positions = NULL

    @property
    def path(self) -> bytes:
        return self._path

    @property
    def filesize(self) -> int:
        return self.filesize
 
    @property
    def format(self) -> DataFormat:
        return DataFormat.from_ptr(self._df)
    
    @property
    def spectra(self):
        if self._spectra is None:
            # Spectra resolves per-spectrum metadata via _spectrum_meta(), so it
            # does NOT need the flattened Division. Pass it only if already
            # materialized (MZMLFile); MSZFile keeps _positions lazy/NULL here.
            positions_div = (Division.from_ptr(self._positions)
                             if self._positions != NULL else None)
            self._spectra = Spectra(
                self,
                DataFormat.from_ptr(self._df),
                positions_div,
                cap=self._cache_spectra,
            )
        return self._spectra

    @property
    def positions(self):
        # Materializes the flattened Division on demand (MSZFile). For mzML it
        # is already populated, so this is a no-op there.
        self._ensure_positions()
        return Division.from_ptr(self._positions)

    @property
    def arguments(self):
        return self._arguments


    def _prepare_output_fd(self, path: Union[str, PathLike, bytes]) -> int:
        # Use os.fspath() to handle path-like objects (PEP 519)
        path = os.fspath(path)
        if isinstance(path, str):
            path = os.path.expanduser(path)
            path = os.path.abspath(path)
            path = path.encode('utf-8')
        elif isinstance(path, bytes):
            # Handle bytes path - decode, expand, encode back
            path_str = path.decode('utf-8')
            path_str = os.path.expanduser(path_str)
            path_str = os.path.abspath(path_str)
            path = path_str.encode('utf-8')
        cdef int output_fd = _open_output_file(path)
        return output_fd 

    def get_mz_binary(self, size_t index):
        raise NotImplementedError("This method should be overridden in subclasses")
    
    def get_inten_binary(self, size_t index):
        raise NotImplementedError("This method should be overridden in subclasses")

    def get_xml(self, size_t index):
        raise NotImplementedError("This method should be overridden in subclasses")

    def describe(self) -> dict:
        self._ensure_positions()
        return {
            "path": self.path,
            "filesize": self.filesize,
            "format": DataFormat.from_ptr(self._df),
            "positions": Division.from_ptr(self._positions)
        }

    def compress(self, output):
        raise NotImplementedError("Cannot compress this file type.")
    
    def decompress(self, output):
        raise NotImplementedError("Cannot decompress this file type.")
    
    def get_header(self) -> str:
        """
        Extract the complete mzML header as a raw string.
        
        This function extracts the header portion of an mzML file (everything from the start
        of the file to the first spectrum element).
        
        Returns:
            str: The raw XML header string.
            
        Raises:
            RuntimeError: If header extraction fails.
        """
        cdef char* header_data = NULL
        cdef size_t header_len = 0
        cdef division_t* first_division = NULL
        cdef bint use_positions = False
        cdef bytes header_bytes
        
        # Validate mapping is available
        if self._mapping == NULL:
            raise RuntimeError("File mapping is not available. File may be closed.")
        
        try:
            # Check if we should use _positions (MZMLFile) or _divisions (MSZFile)
            if self._divisions == NULL:
                use_positions = True
            elif self._divisions.n_divisions == 0:
                use_positions = True
            
            if use_positions:
                # For MZMLFile, use _positions and mapping directly
                if self._positions == NULL:
                    raise RuntimeError("Failed to access division information (_positions is NULL).")
                first_division = self._positions
            else:
                # For MSZFile, use first division from divisions array
                if self._divisions.divisions == NULL:
                    raise RuntimeError("Failed to access divisions array.")
                first_division = self._divisions.divisions[0]
            
            if first_division == NULL:
                raise RuntimeError("Failed to access first division (first_division is NULL).")
            
            # Validate first_division has required fields
            if first_division.spectra == NULL:
                raise RuntimeError("first_division.spectra is NULL")
            if first_division.xml == NULL:
                raise RuntimeError("first_division.xml is NULL")
            
            header_data = _extract_mzml_header(<char*>self._mapping, first_division, &header_len)
            
            if header_data == NULL:
                raise RuntimeError("Failed to extract mzML header.")
            
            # Convert to Python bytes then string
            header_bytes = header_data[:header_len]
            header_str = header_bytes.decode('utf-8', errors='replace')
            
            return header_str
        
        finally:
            # Free the allocated memory
            if header_data != NULL:
                free(header_data)
    
    def extract_metadata(self, tag_name: str) -> Element:
        """
        Extract and parse a specific XML tag from the mzML file header.
        
        This method extracts the header portion of an mzML file, searches for a specific
        XML tag (e.g., 'referenceableParamGroupList', 'cvList', 'fileDescription'), 
        strips any content outside of it, and parses that XML element.
        
        Parameters:
            tag_name (str): The name of the XML tag to extract (without namespace).
            
        Returns:
            Element: An xml.etree.ElementTree.Element containing the parsed XML tag.
            
        Raises:
            ValueError: If the tag is not found in the header.
            RuntimeError: If header extraction fails.
            ParseError: If XML parsing fails.
            
        Examples:
            >>> with mscompress.read('data.mzml') as f:
            ...     param_groups = f.extract_metadata('referenceableParamGroupList')
            ...     for group in param_groups:
            ...         print(group.attrib)
        """
        header_str = self.get_header()
        
        # Find the tag in the header
        # We need to handle namespace-aware searching
        tag_pattern = f'<{tag_name}'
        tag_start = header_str.find(tag_pattern)
        
        if tag_start == -1:
            raise ValueError(f"Tag '{tag_name}' not found in mzML header.")
        
        # Find the closing tag
        closing_tag = f'</{tag_name}>'
        tag_end = header_str.find(closing_tag, tag_start)
        
        if tag_end == -1:
            raise ValueError(f"Closing tag for '{tag_name}' not found in mzML header.")
        
        # Extract the tag with its closing tag
        tag_end += len(closing_tag)
        tag_content = header_str[tag_start:tag_end]
        
        # Wrap in a minimal XML document to handle namespaces properly
        # Extract namespace declarations from the header
        mzml_start = header_str.find('<mzML')
        if mzml_start != -1:
            mzml_tag_end = header_str.find('>', mzml_start)
            mzml_tag = header_str[mzml_start:mzml_tag_end + 1]
            
            # Extract namespace attributes
            ns_attrs = re.findall(r'xmlns[^=]*="[^"]*"', mzml_tag)
            ns_declaration = ' '.join(ns_attrs) if ns_attrs else ''
            
            # Create a wrapper with proper namespace
            wrapped_xml = f'<root {ns_declaration}>{tag_content}</root>'
        else:
            wrapped_xml = f'<root>{tag_content}</root>'
        
        # Parse the XML
        root = fromstring(wrapped_xml)
        
        # Return the first child (the actual tag we want)
        if len(root) > 0:
            return root[0]
        else:
            raise ValueError(f"Failed to parse '{tag_name}' from header.")


cdef class Spectra:
    """
    A class to represent and manage a collection of spectra, allowing (lazy) iteration and access by index.

    Methods:
    __init__(self, DataFormat df, Division positions):
        Initializes the Spectra object with a data format and a list of postions.

    __iter__(self):
        Resets the iteration index and returns the iterator object.

    __next__(self):
        Returns the next spectrum in the sequence during iteration, raises `StopIteration` when the end is reached.

    __getitem__(self, size_t index):
        Computes and returns the spectrum at the specified index.
        Raises `IndexError` if the index is out of range.

    __len__(self) -> int:
        Returns the total number of spectra.

    clear_cache(self):
        Drop all cached ``Spectrum`` objects (and any payloads they lazily loaded).

    cache_cap (property):
        The effective LRU cap. 0 means unbounded; positive ints are the maximum
        number of ``Spectrum`` objects held at once.
    """
    cdef BaseFile _f
    cdef object _df
    cdef object _positions
    cdef object _cache  # dict[size_t, Spectrum] — insertion-ordered LRU
    cdef int _cap       # 0 = unbounded; positive = bounded
    cdef int _index
    cdef size_t length
    cdef object _transform

    def __init__(self, BaseFile f, DataFormat df, Division positions,
                 int cap=CACHE_SPECTRA_AUTO):
        self._f = f
        self._df = df
        self._positions = positions
        self.length = self._df.source_total_spec
        self._cache = {}
        if cap == CACHE_SPECTRA_AUTO:
            self._cap = _DEFAULT_CACHE_SPECTRA
        else:
            self._cap = cap if cap > 0 else 0
        self._index = 0
        self._transform = None

    def __iter__(self):
        self._index = 0  # Reset index for new iteration
        return self

    def __next__(self):
        if self._index >= self.length:
            raise StopIteration

        result = self[self._index]
        self._index += 1
        return result

    def __getitem__(self, size_t index):
        if index >= self.length:
            raise IndexError("Spectra index out of range")

        # LRU hit: bump to most-recently-used by reinserting at the dict tail.
        # Python 3.7+ dicts preserve insertion order, which gives LRU ordering
        # for free (oldest = first iter, newest = last insertion).
        if index in self._cache:
            sp = self._cache.pop(index)
            self._cache[index] = sp
            return sp

        sp = self._compute_spectrum(index)
        self._cache[index] = sp

        # Evict oldest while over cap. cap=0 means unbounded — never evict.
        if self._cap > 0:
            while len(self._cache) > self._cap:
                oldest = next(iter(self._cache))
                del self._cache[oldest]

        return sp

    @property
    def cache_cap(self):
        """The effective LRU cap. 0 means unbounded; positive ints are the
        maximum number of ``Spectrum`` objects held at once."""
        return self._cap

    @property
    def cache_size(self):
        """Number of ``Spectrum`` objects currently held in the cache."""
        return len(self._cache)

    def _has_cached(self, size_t index):
        """True if ``index`` is currently resident in the object cache.
        Primarily for tests/introspection; does not affect LRU ordering."""
        return index in self._cache

    def clear_cache(self):
        """Drop all cached ``Spectrum`` objects.

        Subsequent ``spectra[i]`` calls construct fresh objects and re-fetch
        their ``mz`` / ``intensity`` / ``xml`` lazily. The file stays open.
        Does not touch any C-level block cache.
        """
        self._cache.clear()

    cdef Spectrum _compute_spectrum(self, size_t index):
        # Pull metadata via the file's resolver instead of a flattened position
        # array. For MSZ this reads the mmap-backed per-division scan/ms_level
        # arrays directly (no 22.6 GB/90-shard flatten); for mzML it reads the
        # scan division populated at open.
        cdef uint32_t scan = 0
        cdef uint16_t ms_level = 0
        cdef float ret_time = 0.0
        cdef bint has_rt = False
        self._f._spectrum_meta(index, &scan, &ms_level, &ret_time, &has_rt)
        return Spectrum(
            index=index,
            scan=scan,
            ms_level=ms_level,
            retention_time=(ret_time if has_rt else nan("1")),
            file=self._f,
            transform=self._transform
        )

    def set_transform(self, transform):
        """Set a transform function to apply lazily to all spectra.

        The transform must be a callable that accepts a dict with 'mz' and
        'intensity' NumPy array keys and returns a dict with the same keys.

        Setting a new transform invalidates cached spectra so they will be
        recomputed with the new transform on next access.

        Parameters:
            transform: A callable matching the SpectrumTransform protocol,
                       or None to clear the transform.
        """
        from mscompress.types import SpectrumTransform
        if transform is not None and not isinstance(transform, SpectrumTransform):
            raise TypeError(
                "transform must be a callable with signature "
                "(SpectrumDict) -> SpectrumDict, or None"
            )
        self._transform = transform
        # Invalidate cache so spectra are recomputed with new transform
        self._cache.clear()

    def with_transform(self, transform):
        """Return a new Spectra instance with the given transform applied.

        The original Spectra is not modified.

        Parameters:
            transform: A callable matching the SpectrumTransform protocol,
                       or None for no transform.

        Returns:
            A new Spectra instance with the transform set.
        """
        new = Spectra(self._f, self._df, self._positions, cap=self._cap)
        new.set_transform(transform)
        return new

    def __len__(self) -> int:
        return self.length


cdef class Spectrum:
    """
    A class representing a mass spectrum within a mzML or msz file.

    Attributes:
    index (int): Index of spectrum relative to the file.
    scan (int): Scan number of spectrum reported by instrument.
    size (int): Number of m/z - intensity pairs (pre-transform).
    ms_level (int): MS level of spectrum.
    retention_time (float): Retention time of spectrum.
    """
    cdef:
        uint64_t index
        uint32_t scan
        uint16_t ms_level
        float _retention_time
        BaseFile _file
        object _mz
        object _intensity
        object _xml
        object _transform
        object _transformed

    def __init__(self, uint64_t index, uint32_t scan, uint16_t ms_level, float retention_time, BaseFile file, transform=None):
        self.index = index
        self.scan = scan
        self.ms_level = ms_level
        self._retention_time = retention_time
        self._file = file
        self._mz = None
        self._intensity = None
        self._xml = None
        self._transform = transform
        self._transformed = None

    def __repr__(self):
        return f"Spectrum(index={self.index}, scan={self.scan}, ms_level={self.ms_level}, retention_time={self.retention_time})"

    cdef dict _apply_transform(self):
        """Apply the transform and cache the result."""
        if self._transformed is None and self._transform is not None:
            self._transformed = self._transform({
                'mz': self.raw_mz,
                'intensity': self.raw_intensity,
            })
        return self._transformed

    property index:
        def __get__(self):
            return self.index

    property scan:
        def __get__(self):
            return self.scan

    property xml:
        def __get__(self):
            if self._xml is None:
                self._xml = self._file.get_xml(self.index)
            return self._xml

    property size:
        def __get__(self):
            return len(self.raw_mz)

    property ms_level:
        def __get__(self):
            return self.ms_level

    property retention_time:
        def __get__(self):
            if math.isnan(self._retention_time): # If the ms level wasn't derived from preprocessing step, find it
                try:
                    if self._xml is None:
                        self._xml = self._file.get_xml(self.index)
                    scan = self._xml.find('scanList/scan')
                    for param in scan.findall("cvParam"):
                        if param.attrib['accession'] == 'MS:1000016':
                            if param.attrib.get('unitAccession', '') == 'UO:0000031':  # minutes
                                return float(param.attrib['value']) * 60.0
                            else:  # seconds
                                return float(param.attrib['value'])
                except ParseError as e:
                    return nan("1")
            else:
                return self._retention_time

    property raw_mz:
        def __get__(self):
            if self._mz is None:
                self._mz = self._file.get_mz_binary(self.index)
            return self._mz

    property raw_intensity:
        def __get__(self):
            if self._intensity is None:
                self._intensity = self._file.get_inten_binary(self.index)
            return self._intensity

    property mz:
        def __get__(self):
            if self._transform is not None:
                return self._apply_transform()['mz']
            return self.raw_mz

    property intensity:
        def __get__(self):
            if self._transform is not None:
                return self._apply_transform()['intensity']
            return self.raw_intensity

    property peaks:
        def __get__(self):
            mz = self.mz
            intensity = self.intensity
            if len(mz) != len(intensity):
                raise ValueError(f"Mismatch in array lengths: mz has {len(mz)} elements, intensity has {len(intensity)} elements for spectrum {self.index}")
            return np.column_stack((mz, intensity))

def get_num_threads() -> int:
    """
    Simple function to return current amount of threads on system.

    Returns:
    int: Number of usable threads.
    """
    return _get_num_threads()

def get_filesize(path: Union[str, bytes]) -> int:
    """
    Simple function to get filesize of file.

    Parameters:
    path (Union[str, bytes]): Path to file. Can be a string or bytes.

    Returns:
    int: Size of the file in bytes.
    """
    if isinstance(path, str):
        path = path.encode('utf-8')

    if not os.path.exists(path):
        raise FileNotFoundError(f"File not found: {path}")
    
    return _get_filesize(path)
