# Test the .mszx decompression CLI path:
# 1. Run `mscompress <input.mszx> <out_dir>`.
# 2. Verify the mzML and at least one annotation entry are written.
# 3. Verify that no .zst suffix leaks into the output directory.
# 4. Verify manifest.json is NOT extracted by default.
# 5. Verify the produced mzML matches what you'd get by extracting the
#    embedded MSZ manually and running standard decompression on it.
#
# Expected variables:
#   EXECUTABLE: Path to mscompress
#   INPUT_FILE: Path to .mszx archive
#   TEMP_DIR:   Working directory for extracted/decompressed outputs

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()
if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

set(OUT_DIR "${TEMP_DIR}/out")
set(EXTRACT_DIR "${TEMP_DIR}/extract")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")

# Step 1: decompress the MSZX into OUT_DIR.
execute_process(
    COMMAND "${EXECUTABLE}" "${INPUT_FILE}" "${OUT_DIR}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)
message(STATUS "MSZX decompress output: ${CMD_OUTPUT}")
if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "MSZX decompress error: ${CMD_ERROR}")
    message(FATAL_ERROR "MSZX decompress failed with return code: ${CMD_RESULT}")
endif()

# Step 2: derive expected mzML name from input basename minus .mszx.
get_filename_component(INPUT_BASE "${INPUT_FILE}" NAME)
string(REGEX REPLACE "\\.mszx$" "" INPUT_STEM "${INPUT_BASE}")
set(MZML_PATH "${OUT_DIR}/${INPUT_STEM}.mzML")

if(NOT EXISTS "${MZML_PATH}")
    message(FATAL_ERROR "Expected mzML not produced: ${MZML_PATH}")
endif()

# Step 3: list output directory contents and verify constraints.
file(GLOB OUT_FILES RELATIVE "${OUT_DIR}" "${OUT_DIR}/*")
message(STATUS "Output files: ${OUT_FILES}")

set(HAS_ANNOTATION FALSE)
foreach(F ${OUT_FILES})
    if("${F}" MATCHES "\\.zst$")
        message(FATAL_ERROR ".zst suffix leaked into output: ${F}")
    endif()
    if("${F}" STREQUAL "manifest.json")
        message(FATAL_ERROR "manifest.json should not be extracted by default")
    endif()
    if(NOT "${F}" STREQUAL "${INPUT_STEM}.mzML")
        set(HAS_ANNOTATION TRUE)
    endif()
endforeach()

if(NOT HAS_ANNOTATION)
    message(FATAL_ERROR "No annotation files extracted")
endif()

# Step 4: extract the MSZ entry manually with `tar` and decompress it
# directly to confirm the MSZX-produced mzML is byte-identical.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${INPUT_FILE}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE TAR_RESULT
)
if(NOT TAR_RESULT EQUAL 0)
    message(FATAL_ERROR "Manual tar extract failed")
endif()

file(GLOB MSZ_ENTRIES "${EXTRACT_DIR}/*.msz")
list(LENGTH MSZ_ENTRIES MSZ_COUNT)
if(NOT MSZ_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one .msz in archive, found ${MSZ_COUNT}")
endif()
list(GET MSZ_ENTRIES 0 MSZ_PATH)

set(REFERENCE_MZML "${EXTRACT_DIR}/reference.mzML")
execute_process(
    COMMAND "${EXECUTABLE}" "${MSZ_PATH}" "${REFERENCE_MZML}"
    RESULT_VARIABLE REF_RESULT
    OUTPUT_VARIABLE REF_OUTPUT
    ERROR_VARIABLE REF_ERROR
)
if(NOT REF_RESULT EQUAL 0)
    message(STATUS "Reference decompress output: ${REF_OUTPUT}")
    message(STATUS "Reference decompress error: ${REF_ERROR}")
    message(FATAL_ERROR "Reference MSZ decompress failed: ${REF_RESULT}")
endif()

file(MD5 "${MZML_PATH}" MSZX_MD5)
file(MD5 "${REFERENCE_MZML}" REF_MD5)
message(STATUS "MSZX-decompressed MD5:    ${MSZX_MD5}")
message(STATUS "Reference-decompressed MD5: ${REF_MD5}")

file(REMOVE_RECURSE "${TEMP_DIR}")

if(NOT "${MSZX_MD5}" STREQUAL "${REF_MD5}")
    message(FATAL_ERROR "MSZX-decompressed mzML does not match reference decompression")
endif()

message(STATUS "MSZX decompress test passed!")
