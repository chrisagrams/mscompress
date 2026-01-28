# CMake script to run mscompress extract tests

# Expected variables:
# EXECUTABLE: Path to mscompress executable
# INPUT_MZML: Path to input mzML file
# TEMP_DIR: Path to temporary directory

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

if(NOT EXISTS "${INPUT_MZML}")
    message(FATAL_ERROR "Input file not found: ${INPUT_MZML}")
endif()

if(NOT EXISTS "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
endif()

# Test 1: Extract mzML subset and check spectrumList count
message(STATUS "Test 1: Extract mzML subset and check count...")
set(OUT_MZML "${TEMP_DIR}/extract_out.mzML")
set(ARGS "--extract;--extract-indices;0-10")

execute_process(
    COMMAND "${EXECUTABLE}" ${ARGS} "${INPUT_MZML}" "${OUT_MZML}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

if(NOT CMD_RESULT EQUAL 0)
    message(FATAL_ERROR "Extract to mzML failed: ${CMD_ERROR}")
endif()

file(READ "${OUT_MZML}" MZML_CONTENT)
string(REGEX MATCH "spectrumList count=\"([0-9]+)\"" MATCHED "${MZML_CONTENT}")
if(MATCHED)
    string(REGEX REPLACE "spectrumList count=\"([0-9]+)\"" "\\1" COUNT "${MATCHED}")
    message(STATUS "Found spectrum count: ${COUNT}")
    if(NOT COUNT EQUAL 11)
        message(FATAL_ERROR "Expected count 11 (0-10), got ${COUNT}")
    endif()
else()
    message(FATAL_ERROR "Could not find spectrumList count in output mzML")
endif()

# Test 2: Extract to MSZ and verify roundtrip
message(STATUS "Test 2: Extract to MSZ and verify...")
set(OUT_MSZ "${TEMP_DIR}/extract_out.msz")
set(OUT_DECOMP "${TEMP_DIR}/extract_out_decomp.mzML")

execute_process(
    COMMAND "${EXECUTABLE}" ${ARGS} "${INPUT_MZML}" "${OUT_MSZ}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Extract to MSZ output: ${CMD_OUTPUT}")
    message(STATUS "Extract to MSZ error: ${CMD_ERROR}")
    message(FATAL_ERROR "Extract to MSZ failed with return code: ${CMD_RESULT}")
endif()

if(NOT EXISTS "${OUT_MSZ}")
    message(FATAL_ERROR "Output MSZ file not created")
endif()

# Decompress the extracted MSZ
execute_process(
    COMMAND "${EXECUTABLE}" "${OUT_MSZ}" "${OUT_DECOMP}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

if(NOT CMD_RESULT EQUAL 0)
    message(FATAL_ERROR "Decompress of extracted MSZ failed: ${CMD_ERROR}")
endif()

# Verify count in decompressed file
file(READ "${OUT_DECOMP}" DECOMP_CONTENT)
string(REGEX MATCH "spectrumList count=\"([0-9]+)\"" MATCHED_DECOMP "${DECOMP_CONTENT}")
if(MATCHED_DECOMP)
    string(REGEX REPLACE "spectrumList count=\"([0-9]+)\"" "\\1" COUNT_DECOMP "${MATCHED_DECOMP}")
    message(STATUS "Found spectrum count in decompressed MSZ: ${COUNT_DECOMP}")
    if(NOT COUNT_DECOMP EQUAL 11)
        message(FATAL_ERROR "Expected count 11 (0-10), got ${COUNT_DECOMP}")
    endif()
else()
    message(FATAL_ERROR "Could not find spectrumList count in decompressed mzML")
endif()

message(STATUS "All extract tests passed!")
