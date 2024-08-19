import os
from setuptools import setup, Extension
from Cython.Build import cythonize
import numpy

def get_all_c_files(path):
    files = os.listdir(path)
    c_files = [os.path.join(path, f) for f in files if f.endswith('.c')]
    return c_files    

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


c_sources.append("../vendor/base64/lib/arch/avx2/codec.c")
c_sources.append("../vendor/base64/lib/arch/generic/codec.c")
c_sources.append("../vendor/base64/lib/arch/neon32/codec.c")
c_sources.append("../vendor/base64/lib/arch/neon64/codec.c")
c_sources.append("../vendor/base64/lib/arch/ssse3/codec.c")
c_sources.append("../vendor/base64/lib/arch/sse41/codec.c")
c_sources.append("../vendor/base64/lib/arch/sse42/codec.c")
c_sources.append("../vendor/base64/lib/arch/avx/codec.c")
c_sources.append("../vendor/base64/lib/lib.c")
c_sources.append("../vendor/base64/lib/codec_choose.c")
c_sources.append("../vendor/base64/lib/tables/tables.c")



# Set up include directories
include_dirs = [
    "../vendor/base64/include",
    "../vendor/base64/lib",
    "../vendor/base64/lib/tables",
    "../vendor/base64",
    "../vendor/yxml",
    "../src",
    "../vendor/lz4/lib",
    "../vendor/zlib",
    "../vendor/zstd",
]

extensions = [
    Extension(
        "mscompress",
        sources=["bindings/mscompress.pyx"] + c_sources,
        include_dirs=include_dirs,  
        libraries=[],
        library_dirs=[],
        extra_compile_args=[],
        extra_link_args=[],
    )
]

setup(
    name="mscompress",
    ext_modules=cythonize(extensions),
    include_dirs=[numpy.get_include()]
)
