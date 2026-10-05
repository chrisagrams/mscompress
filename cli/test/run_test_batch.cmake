# Batch-mode CLI test:
#   1. Stage two copies of TEST_MZML into a directory.
#   2. Batch-compress the directory -> one .mszx (Option A streaming writer).
#   3. Batch-compress the same set via --from-file and assert the archives are
#      byte-identical (dir vs list parity; writer is deterministic).
#   4. `tar tf` the archive and assert it lists both .msz entries + manifest.json.
#   5. `--list` and assert it reports a v2 "batch" container.
#   6. Decompress the archive to a directory and assert each recovered mzML is
#      byte-identical to the original (N mzML -> .mszx -> N mzML round-trip).
#
# Expected variables: EXECUTABLE, TEST_MZML, TEMP_DIR

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()
if(NOT EXISTS "${TEST_MZML}")
    message(FATAL_ERROR "Test mzML not found: ${TEST_MZML}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}/in")

configure_file("${TEST_MZML}" "${TEMP_DIR}/in/a.mzML" COPYONLY)
configure_file("${TEST_MZML}" "${TEMP_DIR}/in/b.mzML" COPYONLY)

set(DIR_ARCHIVE "${TEMP_DIR}/dir.mszx")
set(LIST_ARCHIVE "${TEMP_DIR}/list.mszx")

# --- Step 2: compress via directory input ---
execute_process(
    COMMAND "${EXECUTABLE}" --batch "${TEMP_DIR}/in" -o "${DIR_ARCHIVE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(STATUS "out: ${O}\nerr: ${E}")
    message(FATAL_ERROR "batch dir compress failed (${R})")
endif()
if(NOT EXISTS "${DIR_ARCHIVE}")
    message(FATAL_ERROR "archive not created: ${DIR_ARCHIVE}")
endif()

# --- Step 3: compress via --from-file and check parity ---
file(WRITE "${TEMP_DIR}/manifest.txt" "${TEMP_DIR}/in/a.mzML\n${TEMP_DIR}/in/b.mzML\n")
execute_process(
    COMMAND "${EXECUTABLE}" --from-file "${TEMP_DIR}/manifest.txt" -o "${LIST_ARCHIVE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(STATUS "out: ${O}\nerr: ${E}")
    message(FATAL_ERROR "batch --from-file compress failed (${R})")
endif()

file(MD5 "${DIR_ARCHIVE}" DIR_MD5)
file(MD5 "${LIST_ARCHIVE}" LIST_MD5)
message(STATUS "dir archive MD5:  ${DIR_MD5}")
message(STATUS "list archive MD5: ${LIST_MD5}")
if(NOT "${DIR_MD5}" STREQUAL "${LIST_MD5}")
    message(FATAL_ERROR "dir vs --from-file archives are not byte-identical")
endif()

# --- Step 4: tar validity ---
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${DIR_ARCHIVE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE TAR_OUT ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(FATAL_ERROR "tar tf failed on produced archive (${R}): ${E}")
endif()
message(STATUS "tar contents:\n${TAR_OUT}")
foreach(WANT "a.msz" "b.msz" "manifest.json")
    if(NOT TAR_OUT MATCHES "${WANT}")
        message(FATAL_ERROR "archive missing entry: ${WANT}")
    endif()
endforeach()

# --- Step 5: --list reports a batch container ---
execute_process(
    COMMAND "${EXECUTABLE}" --list "${DIR_ARCHIVE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE LIST_OUT ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(FATAL_ERROR "--list failed (${R})")
endif()
message(STATUS "--list output:\n${LIST_OUT}")
if(NOT LIST_OUT MATCHES "container batch")
    message(FATAL_ERROR "--list did not report a batch container")
endif()

# --- Step 6: decompress round-trip, byte-identical ---
set(OUT_DIR "${TEMP_DIR}/out")
execute_process(
    COMMAND "${EXECUTABLE}" "${DIR_ARCHIVE}" "${OUT_DIR}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(STATUS "out: ${O}\nerr: ${E}")
    message(FATAL_ERROR "batch decompress failed (${R})")
endif()

file(MD5 "${TEST_MZML}" ORIG_MD5)
foreach(NAME "a.mzML" "b.mzML")
    if(NOT EXISTS "${OUT_DIR}/${NAME}")
        message(FATAL_ERROR "recovered file missing: ${NAME}")
    endif()
    file(MD5 "${OUT_DIR}/${NAME}" GOT_MD5)
    message(STATUS "${NAME}: original=${ORIG_MD5} recovered=${GOT_MD5}")
    if(NOT "${GOT_MD5}" STREQUAL "${ORIG_MD5}")
        message(FATAL_ERROR "round-trip mismatch for ${NAME}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Batch round-trip / parity / list / validity test passed!")
