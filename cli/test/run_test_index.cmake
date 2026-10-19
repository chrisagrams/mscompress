# Per-spectrum index (index.mszxi) CLI test:
#   1. Batch-compress a.mzML (= TEST_MZML) + b.mzML (= TEST_MZML_B) with and
#      without the index; the indexed manifest is version 2.1 with an index
#      block, the other stays 2.0 with no index member.
#   2. --list --query "entry=0:" prints the full table, byte-identical to GOLDEN_TSV.
#   3. Querying an archive without an index fails with an error.
#   4. Decompression does not extract index.mszxi as an annotation.
#
# Expected variables: EXECUTABLE, TEST_MZML, TEST_MZML_B, GOLDEN_TSV, TEMP_DIR

foreach(F "${EXECUTABLE}" "${TEST_MZML}" "${TEST_MZML_B}" "${GOLDEN_TSV}")
    if(NOT EXISTS "${F}")
        message(FATAL_ERROR "Missing: ${F}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}/in")
configure_file("${TEST_MZML}" "${TEMP_DIR}/in/a.mzML" COPYONLY)
configure_file("${TEST_MZML_B}" "${TEMP_DIR}/in/b.mzML" COPYONLY)

set(IDX "${TEMP_DIR}/idx.mszx")
set(NOIDX "${TEMP_DIR}/noidx.mszx")

function(run_ok)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
    if(NOT R EQUAL 0)
        message(STATUS "out: ${O}\nerr: ${E}")
        message(FATAL_ERROR "command failed (${R}): ${ARGN}")
    endif()
    set(RUN_OUT "${O}" PARENT_SCOPE)
endfunction()

# --- Step 1: with and without the index ---
run_ok("${EXECUTABLE}" --batch "${TEMP_DIR}/in" -o "${IDX}")
run_ok("${EXECUTABLE}" --batch "${TEMP_DIR}/in" -o "${NOIDX}" --no-index)

run_ok("${CMAKE_COMMAND}" -E tar tf "${IDX}")
if(NOT RUN_OUT MATCHES "index.mszxi")
    message(FATAL_ERROR "indexed archive has no index.mszxi:\n${RUN_OUT}")
endif()
run_ok("${CMAKE_COMMAND}" -E tar tf "${NOIDX}")
if(RUN_OUT MATCHES "index.mszxi")
    message(FATAL_ERROR "--no-index archive has an index member")
endif()

file(MAKE_DIRECTORY "${TEMP_DIR}/m_idx" "${TEMP_DIR}/m_noidx")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${IDX}" manifest.json
    WORKING_DIRECTORY "${TEMP_DIR}/m_idx")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${NOIDX}" manifest.json
    WORKING_DIRECTORY "${TEMP_DIR}/m_noidx")
file(READ "${TEMP_DIR}/m_idx/manifest.json" M_IDX)
file(READ "${TEMP_DIR}/m_noidx/manifest.json" M_NOIDX)
if(NOT M_IDX MATCHES "\"version\": \"2.1\"" OR NOT M_IDX MATCHES "\"index\": {\"entry\": \"index.mszxi\"")
    message(FATAL_ERROR "indexed manifest lacks 2.1 / index block:\n${M_IDX}")
endif()
if(NOT M_NOIDX MATCHES "\"version\": \"2.0\"" OR M_NOIDX MATCHES "\"index\"")
    message(FATAL_ERROR "--no-index manifest should be plain 2.0:\n${M_NOIDX}")
endif()

# --- Step 2: full table matches the golden fixture ---
run_ok("${EXECUTABLE}" --list --query "entry=0:" "${IDX}")
file(WRITE "${TEMP_DIR}/table.tsv" "${RUN_OUT}")
file(MD5 "${TEMP_DIR}/table.tsv" GOT)
file(MD5 "${GOLDEN_TSV}" WANT)
if(NOT GOT STREQUAL WANT)
    message(FATAL_ERROR "index table differs from ${GOLDEN_TSV}:\n${RUN_OUT}")
endif()

run_ok("${EXECUTABLE}" --list --query "rt=5:8" "${IDX}")
string(REGEX MATCHALL "\n" LINES "${RUN_OUT}")
list(LENGTH LINES N_LINES)
if(NOT N_LINES EQUAL 28)  # header + 27 hits
    message(FATAL_ERROR "rt=5:8 expected 27 rows:\n${RUN_OUT}")
endif()

# --- Step 3: querying an archive without an index is an error ---
execute_process(COMMAND "${EXECUTABLE}" --list --query "entry=0:" "${NOIDX}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
if(R EQUAL 0 OR NOT E MATCHES "no spectrum index")
    message(FATAL_ERROR "--query on an un-indexed archive should fail (${R}):\n${O}\n${E}")
endif()

# --- Step 4: decompression skips the index member ---
run_ok("${EXECUTABLE}" "${IDX}" "${TEMP_DIR}/out")
if(EXISTS "${TEMP_DIR}/out/index.mszxi")
    message(FATAL_ERROR "index.mszxi was extracted as an annotation")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Spectrum index test passed!")
