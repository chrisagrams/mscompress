# Batch-mode rejection tests (error paths):
#   1. Empty match set -> non-zero exit, no archive written.
#   2. Non-seekable output (FIFO) -> non-zero exit with a clear message
#      (Option A requires a seekable regular file; POSIX only).
#
# Expected variables: EXECUTABLE, TEST_MZML, TEMP_DIR

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}/empty")

# --- 1: empty match set ---
set(EMPTY_ARCHIVE "${TEMP_DIR}/empty.mszx")
execute_process(
    COMMAND "${EXECUTABLE}" --batch "${TEMP_DIR}/empty" -o "${EMPTY_ARCHIVE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
message(STATUS "empty-match exit=${R} err=${E}")
if(R EQUAL 0)
    message(FATAL_ERROR "empty match set should fail but returned 0")
endif()
if(EXISTS "${EMPTY_ARCHIVE}")
    message(FATAL_ERROR "empty match set must not create an archive")
endif()

# --- 2: non-seekable output (FIFO), POSIX only ---
if(NOT WIN32)
    set(FIFO "${TEMP_DIR}/out.fifo.mszx")
    execute_process(COMMAND mkfifo "${FIFO}" RESULT_VARIABLE MK)
    if(MK EQUAL 0)
        file(MAKE_DIRECTORY "${TEMP_DIR}/in")
        configure_file("${TEST_MZML}" "${TEMP_DIR}/in/a.mzML" COPYONLY)
        configure_file("${TEST_MZML}" "${TEMP_DIR}/in/b.mzML" COPYONLY)
        # Drain the FIFO in the background so the writer's open() does not block.
        execute_process(
            COMMAND bash -c "cat '${FIFO}' > /dev/null &
                             '${EXECUTABLE}' --batch '${TEMP_DIR}/in' -o '${FIFO}'"
            RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
        message(STATUS "non-seekable exit=${R} err=${E}")
        if(R EQUAL 0)
            message(FATAL_ERROR "non-seekable output should be rejected but returned 0")
        endif()
        if(NOT E MATCHES "regular file")
            message(FATAL_ERROR "expected a 'regular file' rejection message, got: ${E}")
        endif()
    else()
        message(STATUS "mkfifo unavailable; skipping non-seekable check")
    endif()
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Batch rejection tests passed!")
