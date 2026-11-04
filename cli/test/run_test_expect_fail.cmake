# Runs mscompress on an input that must be rejected cleanly: the command has to
# exit non-zero (not crash with a signal), not leave an output file behind, and
# print EXPECTED_PATTERN to stdout/stderr.
#
# Expected variables: EXECUTABLE, INPUT_FILE, TEMP_DIR, EXPECTED_PATTERN

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")
set(OUTPUT_FILE "${TEMP_DIR}/output.msz")

execute_process(
    COMMAND "${EXECUTABLE}" "${INPUT_FILE}" "${OUTPUT_FILE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
message(STATUS "exit=${R}")
message(STATUS "output=${O}${E}")

if(R EQUAL 0)
    message(FATAL_ERROR "Expected failure but command returned 0")
endif()

# A crash is reported by execute_process as a string (e.g. "Segmentation fault")
# rather than a numeric exit code.
if(NOT R MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Command crashed instead of failing cleanly: ${R}")
endif()

if(NOT "${O}${E}" MATCHES "${EXPECTED_PATTERN}")
    message(FATAL_ERROR "Output did not match expected pattern: ${EXPECTED_PATTERN}")
endif()
