# CMake script to test that --verbose produces stdout output
# while a non-verbose run produces none.

# Expected variables:
# EXECUTABLE: Path to mscompress executable
# INPUT_FILE: Path to input file
# TEMP_DIR: Path to temporary directory

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

if(NOT EXISTS "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
endif()

# Run without --verbose
set(QUIET_OUTPUT "${TEMP_DIR}/quiet.msz")
file(REMOVE "${QUIET_OUTPUT}")

execute_process(
    COMMAND ${EXECUTABLE} ${INPUT_FILE} ${QUIET_OUTPUT}
    RESULT_VARIABLE QUIET_RESULT
    OUTPUT_VARIABLE QUIET_STDOUT
)

if(NOT QUIET_RESULT EQUAL 0)
    message(FATAL_ERROR "Non-verbose run failed with return code: ${QUIET_RESULT}")
endif()

# Run with --verbose
set(VERBOSE_OUTPUT "${TEMP_DIR}/verbose.msz")
file(REMOVE "${VERBOSE_OUTPUT}")

execute_process(
    COMMAND ${EXECUTABLE} --verbose ${INPUT_FILE} ${VERBOSE_OUTPUT}
    RESULT_VARIABLE VERBOSE_RESULT
    OUTPUT_VARIABLE VERBOSE_STDOUT
)

if(NOT VERBOSE_RESULT EQUAL 0)
    message(FATAL_ERROR "Verbose run failed with return code: ${VERBOSE_RESULT}")
endif()

# Assert: quiet run should produce no stdout
string(LENGTH "${QUIET_STDOUT}" QUIET_LEN)
if(NOT QUIET_LEN EQUAL 0)
    message(FATAL_ERROR "Non-verbose run should produce no stdout, got ${QUIET_LEN} chars")
endif()

# Assert: verbose run should produce stdout
string(LENGTH "${VERBOSE_STDOUT}" VERBOSE_LEN)
if(VERBOSE_LEN EQUAL 0)
    message(FATAL_ERROR "Verbose run produced no stdout output")
endif()

message(STATUS "Quiet stdout: ${QUIET_LEN} chars, Verbose stdout: ${VERBOSE_LEN} chars")

# Cleanup
file(REMOVE "${QUIET_OUTPUT}" "${VERBOSE_OUTPUT}")

message(STATUS "Test passed!")
