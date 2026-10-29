# Manifest member offsets (v2 batch) CLI test:
#   1. Batch-compress a.mzML + b.mzML with the index.
#   2. Every spectra_files[] record and the index block carry an `offset`.
#   3. The tar header at `offset - 512` names that member, and the
#      `size` bytes at `offset` equal the member as extracted by tar.
#
# Expected variables: EXECUTABLE, TEST_MZML, TEST_MZML_B, TEMP_DIR

cmake_minimum_required(VERSION 3.19) # string(JSON)

foreach(F "${EXECUTABLE}" "${TEST_MZML}" "${TEST_MZML_B}")
    if(NOT EXISTS "${F}")
        message(FATAL_ERROR "Missing: ${F}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}/in" "${TEMP_DIR}/x")
configure_file("${TEST_MZML}" "${TEMP_DIR}/in/a.mzML" COPYONLY)
configure_file("${TEST_MZML_B}" "${TEMP_DIR}/in/b.mzML" COPYONLY)

set(ARCHIVE "${TEMP_DIR}/out.mszx")

# --- Step 1: compress ---
execute_process(
    COMMAND "${EXECUTABLE}" --batch "${TEMP_DIR}/in" -o "${ARCHIVE}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(STATUS "out: ${O}\nerr: ${E}")
    message(FATAL_ERROR "batch compress failed (${R})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${ARCHIVE}"
    WORKING_DIRECTORY "${TEMP_DIR}/x" RESULT_VARIABLE R)
if(NOT R EQUAL 0)
    message(FATAL_ERROR "tar xf failed on produced archive (${R})")
endif()
file(READ "${TEMP_DIR}/x/manifest.json" MANIFEST)

# Check one member: header name at offset-512, payload bytes at offset.
function(check_member NAME OFFSET SIZE)
    if(NOT OFFSET MATCHES "^[0-9]+$" OR OFFSET LESS 512)
        message(FATAL_ERROR "${NAME}: bad offset '${OFFSET}'")
    endif()
    math(EXPR HDR "${OFFSET} - 512")
    string(LENGTH "${NAME}" NAME_LEN)
    file(READ "${ARCHIVE}" GOT_NAME OFFSET ${HDR} LIMIT ${NAME_LEN} HEX)
    string(HEX "${NAME}" WANT_NAME)
    if(NOT GOT_NAME STREQUAL WANT_NAME)
        message(FATAL_ERROR "${NAME}: header at ${HDR} does not name it")
    endif()

    file(SIZE "${TEMP_DIR}/x/${NAME}" ON_DISK)
    if(NOT ON_DISK EQUAL SIZE)
        message(FATAL_ERROR "${NAME}: manifest size ${SIZE} != ${ON_DISK}")
    endif()
    file(READ "${ARCHIVE}" GOT OFFSET ${OFFSET} LIMIT ${SIZE} HEX)
    file(READ "${TEMP_DIR}/x/${NAME}" WANT HEX)
    if(NOT GOT STREQUAL WANT)
        message(FATAL_ERROR "${NAME}: bytes at offset ${OFFSET} differ from member")
    endif()
    message(STATUS "${NAME}: offset=${OFFSET} size=${SIZE} ok")
endfunction()

# --- Step 2/3: spectra files ---
string(JSON N LENGTH "${MANIFEST}" spectra_files)
if(NOT N EQUAL 2)
    message(FATAL_ERROR "expected 2 spectra_files, got ${N}:\n${MANIFEST}")
endif()
math(EXPR LAST "${N} - 1")
foreach(I RANGE ${LAST})
    string(JSON NAME GET "${MANIFEST}" spectra_files ${I} entry)
    string(JSON OFF ERROR_VARIABLE JERR GET "${MANIFEST}" spectra_files ${I} offset)
    if(JERR)
        message(FATAL_ERROR "spectra_files[${I}] has no offset:\n${MANIFEST}")
    endif()
    string(JSON SIZE GET "${MANIFEST}" spectra_files ${I} size)
    check_member("${NAME}" "${OFF}" "${SIZE}")
endforeach()

# --- Step 2/3: index ---
string(JSON NAME GET "${MANIFEST}" index entry)
string(JSON OFF ERROR_VARIABLE JERR GET "${MANIFEST}" index offset)
if(JERR)
    message(FATAL_ERROR "index block has no offset:\n${MANIFEST}")
endif()
string(JSON SIZE GET "${MANIFEST}" index size)
check_member("${NAME}" "${OFF}" "${SIZE}")

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Manifest offset test passed!")
