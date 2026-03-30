import cython
import os
import warnings
import threading
import ctypes
import numpy as np

from cython.cimports.mscompress._core._bindings import (
    _set_error_callback,
    _set_warning_callback,
)

# Create a numpy dtype that matches C long (32-bit on Windows, typically 64-bit on Linux/Mac)
_c_long_dtype = np.dtype(np.int32 if ctypes.sizeof(ctypes.c_long) == 4 else np.int64)


def _install_mscompress_warning_formatter():
    def _mscompress_formatwarning(message, category, filename, lineno, line=None):
        return f"mscompress : {category.__name__}: {message}\n"
    warnings.formatwarning = _mscompress_formatwarning


# `with gil` is required because these callbacks may be invoked from C code
# running without the GIL (e.g., streaming methods release the GIL for I/O).
@cython.cfunc
@cython.exceptval(check=False)
@cython.with_gil
def _python_error_handler(message: cython.p_const_char) -> cython.void:
    """Callback function to handle C errors in Python"""
    msg = message.decode('utf-8') if isinstance(message, bytes) else message
    warnings.warn(msg.strip(), RuntimeWarning, stacklevel=2)


@cython.cfunc
@cython.exceptval(check=False)
@cython.with_gil
def _python_warning_handler(message: cython.p_const_char) -> cython.void:
    """Callback function to handle C warnings in Python"""
    msg = message.decode('utf-8') if isinstance(message, bytes) else message
    warnings.warn(msg.strip(), RuntimeWarning, stacklevel=2)


def _init_callbacks():
    """Initialize C error/warning callbacks. Called once at package import."""
    _install_mscompress_warning_formatter()
    _set_error_callback(_python_error_handler)
    _set_warning_callback(_python_warning_handler)


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
