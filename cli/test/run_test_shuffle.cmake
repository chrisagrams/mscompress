# Verifies the --shuffle transform end to end: a shuffled file decompresses
# byte-for-byte back to the original mzML, differs from the unshuffled output
# (so the flag is not silently doing nothing), and is smaller.
#
# Expected variables:
# EXECUTABLE: Path to mscompress executable
# INPUT_FILE: Path to input .mzML
# TEMP_DIR:   Path to temporary directory

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

if(NOT EXISTS "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
endif()

set(PLAIN_MSZ "${TEMP_DIR}/plain.msz")
set(SHUF_MSZ "${TEMP_DIR}/shuffled.msz")
set(ROUNDTRIP "${TEMP_DIR}/roundtrip.mzML")

foreach(F "${PLAIN_MSZ}" "${SHUF_MSZ}" "${ROUNDTRIP}")
    if(EXISTS "${F}")
        file(REMOVE "${F}")
    endif()
endforeach()

# --- compress without the shuffle, as a size reference ----------------------
# --no-shuffle is required now that the shuffle is the default.
execute_process(
    COMMAND "${EXECUTABLE}" --no-shuffle "${INPUT_FILE}" "${PLAIN_MSZ}"
    RESULT_VARIABLE PLAIN_RESULT
    OUTPUT_QUIET ERROR_VARIABLE PLAIN_ERR
)
if(NOT PLAIN_RESULT EQUAL 0)
    message(FATAL_ERROR "Baseline compression failed (${PLAIN_RESULT}): ${PLAIN_ERR}")
endif()

# --- compress with the shuffle ----------------------------------------------
execute_process(
    COMMAND "${EXECUTABLE}" --shuffle "${INPUT_FILE}" "${SHUF_MSZ}"
    RESULT_VARIABLE SHUF_RESULT
    OUTPUT_QUIET ERROR_VARIABLE SHUF_ERR
)
if(NOT SHUF_RESULT EQUAL 0)
    message(FATAL_ERROR "Shuffled compression failed (${SHUF_RESULT}): ${SHUF_ERR}")
endif()

# --- decompress and compare against the original ----------------------------
execute_process(
    COMMAND "${EXECUTABLE}" "${SHUF_MSZ}" "${ROUNDTRIP}"
    RESULT_VARIABLE RT_RESULT
    OUTPUT_QUIET ERROR_VARIABLE RT_ERR
)
if(NOT RT_RESULT EQUAL 0)
    message(FATAL_ERROR "Decompression of shuffled file failed (${RT_RESULT}): ${RT_ERR}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files "${INPUT_FILE}" "${ROUNDTRIP}"
    RESULT_VARIABLE CMP_RESULT
)
if(NOT CMP_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Shuffled round trip is not byte-identical to the source mzML")
endif()

file(SIZE "${PLAIN_MSZ}" PLAIN_SIZE)
file(SIZE "${SHUF_MSZ}" SHUF_SIZE)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files "${PLAIN_MSZ}" "${SHUF_MSZ}"
    RESULT_VARIABLE SAME_RESULT
)
if(SAME_RESULT EQUAL 0)
    message(FATAL_ERROR
        "--shuffle produced an identical file to the baseline; the flag did nothing")
endif()

if(NOT SHUF_SIZE LESS PLAIN_SIZE)
    message(FATAL_ERROR
        "--shuffle did not reduce size: ${SHUF_SIZE} vs baseline ${PLAIN_SIZE}")
endif()

message(STATUS "Shuffle round trip OK: ${PLAIN_SIZE} -> ${SHUF_SIZE} bytes")
