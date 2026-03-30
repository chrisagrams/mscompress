import os
import sys
import platform
import shutil
import atexit
import tomli
from setuptools import setup, Extension
from setuptools.command.sdist import sdist as _sdist
from setuptools.command.build_ext import build_ext as _build_ext
from Cython.Build import cythonize
import numpy

# Build configuration via environment variables:
#   MSCOMPRESS_DEBUG=1     - Enable debug symbols (-g flag)
#   MSCOMPRESS_LINETRACE=1 - Enable Cython linetrace (Python-level debugging)
debug = os.environ.get('MSCOMPRESS_DEBUG', '0').lower() in ('1', 'true', 'yes')
linetrace = os.environ.get('MSCOMPRESS_LINETRACE', '0').lower() in ('1', 'true', 'yes')

BASE_DIR = os.path.abspath(os.path.dirname(__file__))

# Copy vendor/src/cli directories if they don't exist locally (for sdist)
# This needs to happen early, before any file discovery
_CLEANUP_DIRS = []

def _cleanup_vendored_files():
    """Clean up copied vendor/src/cli directories on process exit."""
    for d in _CLEANUP_DIRS:
        if os.path.exists(d):
            print(f"Cleaning up {d}")
            shutil.rmtree(d)

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

    # Register cleanup to run when Python process exits
    if _CLEANUP_DIRS:
        atexit.register(_cleanup_vendored_files)

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


class build_ext_with_shared_lib(_build_ext):
    """Custom build_ext that:
    1. Builds all C sources into a shared library (libmscompress_c)
    2. Adds per-file SIMD compiler flags
    3. Links each Cython extension against the shared library
    """

    def _get_shared_lib_name(self):
        """Get the platform-appropriate shared library filename."""
        if sys.platform == 'win32':
            return 'mscompress_c.dll'
        elif sys.platform == 'darwin':
            return 'libmscompress_c.dylib'
        else:
            return 'libmscompress_c.so'

    def _get_shared_lib_dirs(self):
        """Get the directories where the shared library should be placed.

        Returns a list of directories. For editable installs, includes the source
        tree. For regular builds, includes both build_lib and source tree to ensure
        the library is available at both build-time and install-time.
        """
        dirs = []
        # Always put in the source tree for editable installs and inplace builds
        dirs.append(os.path.join(BASE_DIR, 'mscompress', '_core'))
        if not self.inplace and hasattr(self, 'build_lib') and self.build_lib:
            dirs.append(os.path.join(self.build_lib, 'mscompress', '_core'))
        return dirs

    def _build_shared_library(self):
        """Build all C sources into a shared library."""
        from distutils.ccompiler import new_compiler
        from distutils.sysconfig import customize_compiler

        compiler = new_compiler()
        customize_compiler(compiler)

        # Set up the compiler with our flags
        target_arch = get_target_arch()

        # Build SIMD flags map for per-file compilation
        if sys.platform == 'win32':
            simd_flags = {
                'avx2': ['/arch:AVX2'] if target_arch == 'x86_64' else [],
                'avx': ['/arch:AVX'] if target_arch == 'x86_64' else [],
                'ssse3': [] if target_arch == 'x86_64' else [],
                'sse41': [] if target_arch == 'x86_64' else [],
                'sse42': [] if target_arch == 'x86_64' else [],
                'neon32': [] if target_arch == 'arm32' else [],
                'neon64': [] if target_arch == 'arm64' else [],
            }
        else:
            simd_flags = {
                'avx2': ['-mavx2'] if target_arch == 'x86_64' else [],
                'avx': ['-mavx'] if target_arch == 'x86_64' else [],
                'ssse3': ['-mssse3'] if target_arch == 'x86_64' else [],
                'sse41': ['-msse4.1'] if target_arch == 'x86_64' else [],
                'sse42': ['-msse4.2'] if target_arch == 'x86_64' else [],
                'neon32': [] if target_arch == 'arm32' else [],
                'neon64': [] if target_arch == 'arm64' else [],
            }

        lib_dirs = self._get_shared_lib_dirs()
        for d in lib_dirs:
            os.makedirs(d, exist_ok=True)

        # Use the first directory as the primary build target
        primary_lib_dir = lib_dirs[0]

        # Compile each C source file, applying per-file SIMD flags
        objects = []
        for source in c_sources:
            per_file_flags = list(extra_compile_args)  # copy base flags
            source_lower = source.lower()
            for simd_type, flags in simd_flags.items():
                if flags and simd_type in source_lower:
                    per_file_flags.extend(flags)
                    break

            objs = compiler.compile(
                [source],
                output_dir=self.build_temp,
                macros=define_macros,
                include_dirs=include_dirs,
                extra_postargs=per_file_flags,
            )
            objects.extend(objs)

        # Link into a shared library
        lib_name = self._get_shared_lib_name()

        if sys.platform == 'win32':
            # On Windows, create a DLL
            compiler.link_shared_lib(
                objects,
                'mscompress_c',
                output_dir=primary_lib_dir,
                extra_postargs=extra_link_args,
            )
        elif sys.platform == 'darwin':
            # On macOS, create a dylib with install_name set to @loader_path
            # Use compiler.spawn() directly since link_shared_object adds -bundle
            # which conflicts with -dynamiclib
            output_path = os.path.join(primary_lib_dir, lib_name)
            linker_cmd = ['cc', '-dynamiclib'] + objects + [
                '-o', output_path,
                '-install_name', f'@loader_path/{lib_name}',
            ] + extra_link_args
            compiler.spawn(linker_cmd)
        else:
            # On Linux, create a .so with RPATH
            compiler.link_shared_lib(
                objects,
                'mscompress_c',
                output_dir=primary_lib_dir,
                extra_postargs=extra_link_args + [
                    f'-Wl,-soname,{lib_name}',
                ],
            )

        # Copy the shared library to all target directories
        primary_lib_path = os.path.join(primary_lib_dir, lib_name)
        for d in lib_dirs[1:]:
            dest = os.path.join(d, lib_name)
            print(f"Copying {primary_lib_path} to {dest}")
            shutil.copy2(primary_lib_path, dest)

        return primary_lib_dir

    def build_extensions(self):
        # First, build the shared C library
        lib_dir = self._build_shared_library()

        # Configure each extension to link against the shared library
        for ext in self.extensions:
            ext.library_dirs.append(lib_dir)
            ext.libraries.append('mscompress_c')

            # Set rpath so the extension can find the shared library at runtime
            if sys.platform == 'darwin':
                ext.extra_link_args = list(ext.extra_link_args or []) + [
                    '-Wl,-rpath,@loader_path',
                ]
            elif sys.platform != 'win32':
                ext.extra_link_args = list(ext.extra_link_args or []) + [
                    '-Wl,-rpath,$ORIGIN',
                ]

        # Call parent build_extensions
        _build_ext.build_extensions(self)

    def run(self):
        try:
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
        finally:
            # Clean up copied directories (vendor/, src/, cli/) after build is complete
            for d in _CLEANUP_DIRS:
                if os.path.exists(d):
                    print(f"Cleaning up {d}")
                    shutil.rmtree(d)


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
c_sources.append(_abs("../vendor/base64/lib/arch/avx512/codec.c"))  # Needed for stub functions
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
        extra_compile_args = ["-g", "-O0"]
        extra_link_args = ["-g"]
else:
    if sys.platform == 'win32':
        extra_compile_args = ["/O2"]
        extra_link_args = []
    else:
        extra_compile_args = ["-O3"]
        extra_link_args = []


# TODO: Ideally we don't need to disable assembly optimizations, but for now we do.
# Look for a better solution in the future.
# TODO: NO_GZCOMPRESS is also a workaround for macos. Look into how to get around it.
define_macros = [
    ('NO_GZCOMPRESS', '1'),  # Disable gzip support to avoid fdopen macro conflicts
    ('ZSTD_DISABLE_ASM', '1'), # Temporary workaround to disable assembly optimizations when building.
    ('BASE64_STATIC_DEFINE', '1'),  # Tell base64 library to use static linking on Windows
    ('zmemset', 'memset'),  # Define zmemset as memset for Windows (zlib cloudflare fork expects this)
]

if linetrace:
    define_macros.append(('CYTHON_TRACE', '1'))

# On macOS, define fdopen before compilation to prevent zlib's macro redefinition
if sys.platform == 'darwin':
    define_macros.append(('fdopen', 'fdopen'))

# Generate config.h for base64 library based on TARGET architecture
# cibuildwheel sets ARCHFLAGS or _PYTHON_HOST_PLATFORM for cross-compilation
def get_target_arch():
    """Detect target architecture for cross-compilation."""
    # Check environment variables set by cibuildwheel
    archflags = os.environ.get('ARCHFLAGS', '').lower()
    if 'arm64' in archflags or 'aarch64' in archflags:
        return 'arm64'
    elif 'x86_64' in archflags or 'amd64' in archflags:
        return 'x86_64'

    # Check _PYTHON_HOST_PLATFORM (used by cibuildwheel on macOS)
    host_platform = os.environ.get('_PYTHON_HOST_PLATFORM', '').lower()
    if 'arm64' in host_platform or 'aarch64' in host_platform:
        return 'arm64'
    elif 'x86_64' in host_platform or 'amd64' in host_platform or 'i686' in host_platform:
        return 'x86_64'

    # Fall back to native architecture
    arch = platform.machine().lower()
    if 'arm64' in arch or 'aarch64' in arch:
        return 'arm64'
    elif 'arm' in arch:
        return 'arm32'
    else:
        return 'x86_64'

target_arch = get_target_arch()
print(f"Target architecture detected: {target_arch}")

# On ARM64 Linux, enable CRC instructions for zlib's hardware-accelerated CRC32
# This is needed because zlib uses ARM CRC intrinsics that require explicit CPU feature flags
if target_arch == 'arm64' and sys.platform != 'darwin':
    extra_compile_args.append('-march=armv8-a+crc')

if target_arch == 'arm64':
    # ARM64 architecture
    config_h_content = """
#define HAVE_AVX2 0
#define HAVE_NEON32 0
#define HAVE_NEON64 1
#define HAVE_SSSE3 0
#define HAVE_SSE41 0
#define HAVE_SSE42 0
#define HAVE_AVX 0
#define HAVE_AVX512 0
"""
elif target_arch == 'arm32':
    # ARM32 architecture
    config_h_content = """
#define HAVE_AVX2 0
#define HAVE_NEON32 1
#define HAVE_NEON64 0
#define HAVE_SSSE3 0
#define HAVE_SSE41 0
#define HAVE_SSE42 0
#define HAVE_AVX 0
#define HAVE_AVX512 0
"""
else:
    # x86/x64 architecture
    config_h_content = """
#define HAVE_AVX2 1
#define HAVE_NEON32 0
#define HAVE_NEON64 0
#define HAVE_SSSE3 1
#define HAVE_SSE41 1
#define HAVE_SSE42 1
#define HAVE_AVX 1
#define HAVE_AVX512 0
"""

config_h_path = _abs("../vendor/base64/lib/config.h")
os.makedirs(os.path.dirname(config_h_path), exist_ok=True)
with open(config_h_path, 'w') as f:
    f.write(config_h_content)

# On Windows, ensure Windows SDK target-architecture macro is defined early
# so that <Windows.h>/winnt.h doesn't error with "No Target Architecture".
if sys.platform == 'win32':
    if target_arch == 'arm64':
        define_macros.append(('_ARM64_', '1'))
    elif target_arch == 'x86_64':
        define_macros.append(('_AMD64_', '1'))
    elif target_arch == 'x86':
        define_macros.append(('_X86_', '1'))
    # Target a reasonably recent Windows version
    define_macros.append(('_WIN32_WINNT', '0x0600'))

# Cython extension modules — each .py file in _core/ becomes a separate extension.
# C sources are compiled into a shared library (libmscompress_c) by the custom build_ext,
# so each extension only needs its own Cython-generated C file.
_core_modules = [
    '_utils',
    '_types',
    '_base',
    '_mzml',
    '_msz',
    '_spectrum',
    '_functions',
]

# PIC flag for shared library compilation (Unix only)
if sys.platform != 'win32':
    extra_compile_args.append('-fPIC')

extensions = [
    Extension(
        f"mscompress._core.{mod}",
        sources=[f"mscompress/_core/{mod}.py"],
        include_dirs=include_dirs,
        libraries=[],  # Will be populated by build_ext_with_shared_lib
        library_dirs=[],  # Will be populated by build_ext_with_shared_lib
        extra_compile_args=list(extra_compile_args),  # Copy so per-ext mutation is safe
        extra_link_args=list(extra_link_args),
        define_macros=define_macros,
    )
    for mod in _core_modules
]

# Read pyproject.toml
with open("pyproject.toml", "rb") as f:
    pyproject = tomli.load(f)
version = pyproject["project"]["version"]
description = pyproject["project"]["description"]

setup(
    name="mscompress",
    version=version,
    description=description,
    author="Chris Grams",
    author_email="chrisagrams@gmail.com",
    packages=[
        "mscompress",
        "mscompress._core",
        "mscompress.annotations",
        "mscompress.annotations.psms",
        "mscompress.datasets",
        "mscompress.metadata",
    ],
    ext_modules=cythonize(
        extensions,
        compiler_directives={
            'linetrace': linetrace,
            'binding': linetrace,
            'language_level': '3',
        },
        compile_time_env={'MSC_VERSION': version},
        gdb_debug=debug,
    ),
    include_dirs=[numpy.get_include()],
    cmdclass={
        'build_ext': build_ext_with_shared_lib,
        'sdist': sdist_with_vendor
    },
    package_data={
        'mscompress': ['*.pyi', 'py.typed'],
        'mscompress._core': ['*.pxd', '*.dylib', '*.so', '*.dll'],
    },
    zip_safe=False,
)
