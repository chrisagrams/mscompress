import os
import sys
import platform
import shutil
from setuptools import setup, Extension
from setuptools.command.sdist import sdist as _sdist
from setuptools.command.build_ext import build_ext as _build_ext
from Cython.Build import cythonize
import numpy

debug = False 
linetrace = False

BASE_DIR = os.path.abspath(os.path.dirname(__file__))

# Copy vendor/src/cli directories if they don't exist locally (for sdist)
# This needs to happen early, before any file discovery
_CLEANUP_DIRS = []
def _ensure_vendored_files():
    """Copy vendor/src/cli into package directory if building from git repo."""
    vendor_src = os.path.normpath(os.path.join(BASE_DIR, '../vendor'))
    src_src = os.path.normpath(os.path.join(BASE_DIR, '../src'))
    cli_src = os.path.normpath(os.path.join(BASE_DIR, '../cli'))
    
    vendor_dst = os.path.join(BASE_DIR, 'vendor')
    src_dst = os.path.join(BASE_DIR, 'src')
    cli_dst = os.path.join(BASE_DIR, 'cli')
    
    # Copy directories if they don't exist locally and source exists
    if os.path.exists(vendor_src) and not os.path.exists(vendor_dst):
        print(f"Copying {vendor_src} to {vendor_dst}")
        shutil.copytree(vendor_src, vendor_dst, dirs_exist_ok=True)
        _CLEANUP_DIRS.append(vendor_dst)
        
    if os.path.exists(src_src) and not os.path.exists(src_dst):
        print(f"Copying {src_src} to {src_dst}")
        shutil.copytree(src_src, src_dst, dirs_exist_ok=True)
        _CLEANUP_DIRS.append(src_dst)
        
    if os.path.exists(cli_src) and not os.path.exists(cli_dst):
        print(f"Copying {cli_src} to {cli_dst}")
        shutil.copytree(cli_src, cli_dst, dirs_exist_ok=True)
        _CLEANUP_DIRS.append(cli_dst)

# Ensure files are present before any other setup code runs
_ensure_vendored_files()


def _abs(path):
    """Normalize a path relative to this setup.py"""
    # When building from sdist, files are in the package directory
    # When building from source, they're in the parent directory
    local_path = os.path.normpath(os.path.join(BASE_DIR, path))
    if os.path.exists(local_path):
        return local_path
    # Try without the ../ prefix for sdist builds
    if path.startswith('../'):
        sdist_path = os.path.normpath(os.path.join(BASE_DIR, path[3:]))
        if os.path.exists(sdist_path):
            return sdist_path
    return local_path


def get_all_c_files(path):
    """Simple helper to get all C files for a given path."""
    full = _abs(path)
    if not os.path.isdir(full):
        print(f"Warning: C source directory not found: {full} -- skipping")
        return []
    files = os.listdir(full)
    return [os.path.join(full, f) for f in files if f.endswith('.c')]


class build_ext_with_stubs(_build_ext):
    """Custom build_ext that copies stub files to the build directory."""
    def run(self):
        # Run the standard build
        _build_ext.run(self)
        
        # Copy stub files to the build directory where the .so file is
        if self.inplace:
            build_dir = os.path.dirname(self.get_ext_fullpath('mscompress'))
        else:
            build_dir = os.path.dirname(self.get_ext_fullpath('mscompress'))
        
        # Source files
        stub_file = _abs('bindings/mscompress.pyi')
        py_typed = _abs('bindings/py.typed')
        
        # Copy if they exist
        if os.path.exists(stub_file):
            dest = os.path.join(build_dir, 'mscompress.pyi')
            print(f"Copying {stub_file} to {dest}")
            shutil.copy2(stub_file, dest)
        
        if os.path.exists(py_typed):
            dest = os.path.join(build_dir, 'py.typed')
            print(f"Copying {py_typed} to {dest}")
            shutil.copy2(py_typed, dest)


class sdist_with_vendor(_sdist):
    """Custom sdist command that cleans up copied vendor files after sdist."""
    def run(self):
        try:
            # Run the standard sdist
            _sdist.run(self)
        finally:
            # Clean up copied directories after sdist is complete
            for d in _CLEANUP_DIRS:
                if os.path.exists(d):
                    print(f"Cleaning up {d}")
                    shutil.rmtree(d)


# Collect all C source files from src and vendor directories
c_sources = []
c_sources += get_all_c_files("../vendor/lz4/lib")
c_sources += get_all_c_files("../vendor/zlib")
c_sources += get_all_c_files("../src")
c_sources += get_all_c_files("../cli")
c_sources += get_all_c_files("../vendor/zstd")
c_sources += get_all_c_files("../vendor/zstd/lib/common")
c_sources += get_all_c_files("../vendor/zstd/lib/compress")
c_sources += get_all_c_files("../vendor/zstd/lib/decompress")
c_sources += get_all_c_files("../vendor/yxml")

# Add base64 codecs
c_sources.append(_abs("../vendor/base64/lib/arch/avx2/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/generic/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/neon32/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/neon64/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/ssse3/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/sse41/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/sse42/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/arch/avx/codec.c"))
c_sources.append(_abs("../vendor/base64/lib/lib.c"))
c_sources.append(_abs("../vendor/base64/lib/codec_choose.c"))
c_sources.append(_abs("../vendor/base64/lib/tables/tables.c"))

# Remove gzip support from zlib (not used)
# Note: zutil.c must be included as it contains z_errmsg and other essential symbols
for fname in ["../vendor/zlib/gzlib.c", "../vendor/zlib/gzread.c",
              "../vendor/zlib/gzwrite.c", "../vendor/zlib/gzclose.c"]:
    f = _abs(fname)
    try:
        c_sources.remove(f)
    except ValueError:
        print(f"Warning: {f} not in c_sources; skipping removal")


# Set up include directories
# When building from sdist, we need the package directory itself to resolve
# relative paths like ../vendor/ from cli/ subdirectory
include_dirs = [
    BASE_DIR,  # Add package root for relative includes to work
    _abs("../vendor/base64/include"),
    _abs("../vendor/base64/lib"),
    _abs("../vendor/base64/lib/tables"),
    _abs("../vendor/base64"),
    _abs("../vendor/yxml"),
    _abs("../src"),
    _abs("../vendor/lz4/lib"),
    _abs("../vendor/zlib"),
    _abs("../vendor/zstd"),
    _abs("../vendor/zstd/lib"),  # Add this for direct zstd.h includes
    numpy.get_include(),
]

if debug:
    # Check if compiling on Windows
    if sys.platform == 'win32':
        extra_compile_args = ["/Zi", "/Od"]  # "/Zi" generates debugging information, "/Od" disables optimization
        extra_link_args = ["/DEBUG"]
    else:
        extra_compile_args = ["-g"]
        extra_link_args = ["-g"]
else:
    extra_compile_args = []
    extra_link_args = []


# TODO: Ideally we don't need to disable assembly optimizations, but for now we do.
# Look for a better solution in the future.
# TODO: NO_GZCOMPRESS is also a workaround for macos. Look into how to get around it.
define_macros: list[tuple[str, str | None]] = [
    ('NO_GZCOMPRESS', '1'),  # Disable gzip support to avoid fdopen macro conflicts
    ('ZSTD_DISABLE_ASM', '1') # Temporary workaround to disable assembly optimizations when building.
]

if linetrace:
    define_macros.append(('CYTHON_TRACE', '1'))

# On macOS, define fdopen before compilation to prevent zlib's macro redefinition
if sys.platform == 'darwin':
    define_macros.append(('fdopen', 'fdopen'))

# On Windows, ensure Windows SDK target-architecture macro is defined early
# so that <Windows.h>/winnt.h doesn't error with "No Target Architecture".
if sys.platform == 'win32':
    arch = platform.machine().lower()
    if 'arm64' in arch or 'aarch64' in arch:
        define_macros.append(('_ARM64_', '1'))
    elif 'amd64' in arch or 'x86_64' in arch or 'x64' in arch:
        define_macros.append(('_AMD64_', '1'))
    elif arch in ('x86', 'i386', 'i686'):
        define_macros.append(('_X86_', '1'))
    # Target a reasonably recent Windows version
    define_macros.append(('_WIN32_WINNT', '0x0600'))

extensions = [
    Extension(
        "mscompress",
        sources=["bindings/mscompress.pyx"] + c_sources,
        include_dirs=include_dirs,  
        libraries=[],
        library_dirs=[],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        define_macros=define_macros
    )
]

setup(
    name="mscompress",
    ext_modules=cythonize(extensions, compiler_directives={'linetrace': linetrace}),
    include_dirs=[numpy.get_include()],
    cmdclass={
        'build_ext': build_ext_with_stubs,
        'sdist': sdist_with_vendor
    },
    package_data={
        '': ['*.pyi', 'py.typed'],  # Include stub files wherever the module ends up
    },
    zip_safe=False,
)
