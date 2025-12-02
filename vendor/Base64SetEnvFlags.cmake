# Set the variables based on platform detection
# For x86/x64 platforms, enable SIMD features
# For ARM platforms, enable NEON features

# Initialize all to 0
set(HAVE_AVX2 0)
set(HAVE_AVX512 0)
set(HAVE_NEON32 0)
set(HAVE_NEON64 0)
set(HAVE_SSSE3 0)
set(HAVE_SSE41 0)
set(HAVE_SSE42 0)
set(HAVE_AVX 0)

# Detect architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|x86|i686|i386")
    # x86/x64 architecture - enable SSE/AVX features
    set(HAVE_SSSE3 1)
    set(HAVE_SSE41 1)
    set(HAVE_SSE42 1)
    set(HAVE_AVX 1)
    set(HAVE_AVX2 1)
    # Disable AVX512 by default as it requires specific compiler flags
    set(HAVE_AVX512 0)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64.*|AARCH64.*|arm64.*|ARM64.*)")
    # 64-bit ARM architecture
    set(HAVE_NEON64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm.*|ARM.*)")
    # 32-bit ARM architecture
    set(HAVE_NEON32 1)
endif()

# Allow environment variables to override
if(DEFINED ENV{AVX2_CFLAGS})
    set(HAVE_AVX2 1)
endif()

if(DEFINED ENV{NEON32_CFLAGS})
    set(HAVE_NEON32 1)
endif()

if(DEFINED ENV{NEON64_CFLAGS})
    set(HAVE_NEON64 1)
endif()

if(DEFINED ENV{SSSE3_CFLAGS})
    set(HAVE_SSSE3 1)
endif()

if(DEFINED ENV{SSE41_CFLAGS})
    set(HAVE_SSE41 1)
endif()

if(DEFINED ENV{SSE42_CFLAGS})
    set(HAVE_SSE42 1)
endif()

if(DEFINED ENV{AVX_CFLAGS})
    set(HAVE_AVX 1)
endif()

if(DEFINED ENV{AVX512_CFLAGS})
    set(HAVE_AVX512 1)
endif()

# Generate the config.h file
configure_file(${VENDOR_DIR}/config.h.in ${VENDOR_DIR}/base64/lib/config.h)