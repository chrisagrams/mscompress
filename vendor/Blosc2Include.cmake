# Builds only c-blosc2's shuffle filters, not the whole library: we want
# blosc2_shuffle()/blosc2_unshuffle() and their runtime SIMD dispatch, not its
# compressors, frames, or its own copies of zstd/lz4/zlib. blosc2-shim.c
# supplies the one symbol blosc2.h references that blosc2.c would define.

set(BLOSC2_DIR ${VENDOR_DIR}/c-blosc2)

set(BLOSC2_SHUFFLE_SOURCES
    ${BLOSC2_DIR}/blosc/shuffle.c
    ${BLOSC2_DIR}/blosc/shuffle-generic.c
    ${BLOSC2_DIR}/blosc/bitshuffle-generic.c
    ${VENDOR_DIR}/blosc2-shim.c
)

# Per-ISA kernels, each compiled with its own instruction set while the rest of
# the project stays at baseline. shuffle.c picks between them at runtime via
# cpuid, so the binary still starts on a CPU without AVX2.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
    list(APPEND BLOSC2_SHUFFLE_SOURCES
        ${BLOSC2_DIR}/blosc/shuffle-sse2.c
        ${BLOSC2_DIR}/blosc/bitshuffle-sse2.c
        ${BLOSC2_DIR}/blosc/shuffle-avx2.c
        ${BLOSC2_DIR}/blosc/bitshuffle-avx2.c
    )
    set(BLOSC2_SIMD_DEFS SHUFFLE_SSE2_ENABLED SHUFFLE_AVX2_ENABLED)
    if (MSVC)
        set_source_files_properties(
            ${BLOSC2_DIR}/blosc/shuffle-avx2.c
            ${BLOSC2_DIR}/blosc/bitshuffle-avx2.c
            PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    else()
        set_source_files_properties(
            ${BLOSC2_DIR}/blosc/shuffle-sse2.c
            ${BLOSC2_DIR}/blosc/bitshuffle-sse2.c
            PROPERTIES COMPILE_OPTIONS "-msse2")
        set_source_files_properties(
            ${BLOSC2_DIR}/blosc/shuffle-avx2.c
            ${BLOSC2_DIR}/blosc/bitshuffle-avx2.c
            PROPERTIES COMPILE_OPTIONS "-mavx2")
    endif()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    list(APPEND BLOSC2_SHUFFLE_SOURCES
        ${BLOSC2_DIR}/blosc/shuffle-neon.c
        ${BLOSC2_DIR}/blosc/bitshuffle-neon.c
    )
    # NEON is baseline on ARM64, so no per-file flags are needed.
    set(BLOSC2_SIMD_DEFS SHUFFLE_NEON_ENABLED)
else()
    # Unknown architecture: generic C only. Still correct, just not vectorised.
    set(BLOSC2_SIMD_DEFS "")
endif()

add_library(blosc2_shuffle STATIC ${BLOSC2_SHUFFLE_SOURCES})

target_include_directories(blosc2_shuffle PUBLIC
    ${BLOSC2_DIR}/include
    ${BLOSC2_DIR}/blosc
)

if (BLOSC2_SIMD_DEFS)
    target_compile_definitions(blosc2_shuffle PRIVATE ${BLOSC2_SIMD_DEFS})
endif()

set_target_properties(blosc2_shuffle PROPERTIES POSITION_INDEPENDENT_CODE ON)
