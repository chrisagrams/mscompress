# indexedmzML index rebuild tests (GitHub issue #301):
#   1. Round-tripping an indexed mzML (uncompressed, and zlib, whose binaries
#      change length when re-encoded) writes an index whose every <offset>
#      lands on the matching <spectrum id>/<chromatogram id> and whose
#      <indexListOffset> lands on <indexList>; the content up to </mzML> is
#      unchanged, and the .msz keeps the version it had before (0.2 with the
#      default byte shuffle; no format bump).
#   2. --extract from mzML and from msz input indexes only the extracted
#      spectra, at their offsets in the output.
#   3. A non-indexed mzML still round-trips byte-for-byte.
#
# Expected variables: EXECUTABLE, TEST_MZML, TEST_ZLIB_MZML, TEST_NONINDEXED,
#                     TEMP_DIR

foreach(F "${EXECUTABLE}" "${TEST_MZML}" "${TEST_ZLIB_MZML}" "${TEST_NONINDEXED}")
    if(NOT EXISTS "${F}")
        message(FATAL_ERROR "Missing: ${F}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

function(run_ok)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
    if(NOT R EQUAL 0)
        message(STATUS "out: ${O}\nerr: ${E}")
        message(FATAL_ERROR "command failed (${R}): ${ARGN}")
    endif()
endfunction()

# Fails unless FILE's index has EXPECTED_SPECTRA spectrum entries (and
# EXPECTED_CHROMS chromatogram entries), each pointing at the element it names.
function(check_index LABEL FILE EXPECTED_SPECTRA EXPECTED_CHROMS)
    file(READ "${FILE}" CONTENT)
    string(FIND "${CONTENT}" "<indexList " LIST_POS)
    if(LIST_POS EQUAL -1)
        message(FATAL_ERROR "${LABEL}: no <indexList> in ${FILE}")
    endif()
    string(SUBSTRING "${CONTENT}" ${LIST_POS} -1 INDEX)

    string(REGEX MATCH "<indexListOffset>([0-9]+)</indexListOffset>" _ "${INDEX}")
    if(NOT CMAKE_MATCH_1 EQUAL LIST_POS)
        message(FATAL_ERROR
            "${LABEL}: indexListOffset ${CMAKE_MATCH_1}, <indexList> is at ${LIST_POS}")
    endif()
    if(NOT INDEX MATCHES "</indexedmzML>\n$")
        message(FATAL_ERROR "${LABEL}: output does not end with </indexedmzML>")
    endif()

    string(FIND "${INDEX}" "<index name=\"chromatogram\">" CHROM_POS)
    if(CHROM_POS EQUAL -1)
        set(BLOCK_spectrum "${INDEX}")
        set(BLOCK_chromatogram "")
    else()
        string(SUBSTRING "${INDEX}" 0 ${CHROM_POS} BLOCK_spectrum)
        string(SUBSTRING "${INDEX}" ${CHROM_POS} -1 BLOCK_chromatogram)
    endif()

    foreach(KIND spectrum chromatogram)
        set(N 0)
        set(BLOCK "${BLOCK_${KIND}}")
        string(REGEX MATCHALL "<offset idRef=\"[^\"]*\">[0-9]+</offset>" ENTRIES "${BLOCK}")
        foreach(ENTRY IN LISTS ENTRIES)
            string(REGEX MATCH "idRef=\"([^\"]*)\">([0-9]+)<" _ "${ENTRY}")
            set(ID "${CMAKE_MATCH_1}")
            set(OFF "${CMAKE_MATCH_2}")
            string(SUBSTRING "${CONTENT}" ${OFF} 400 AT)
            string(FIND "${AT}" ">" GT)
            string(SUBSTRING "${AT}" 0 ${GT} TAG)
            string(FIND "${TAG}" " id=\"${ID}\"" HAS_ID)
            if(NOT TAG MATCHES "^<${KIND} " OR HAS_ID EQUAL -1)
                message(FATAL_ERROR
                    "${LABEL}: ${KIND} offset ${OFF} for '${ID}' points at '${TAG}'")
            endif()
            math(EXPR N "${N} + 1")
        endforeach()
        if(KIND STREQUAL "spectrum")
            set(WANT ${EXPECTED_SPECTRA})
        else()
            set(WANT ${EXPECTED_CHROMS})
        endif()
        if(NOT N EQUAL WANT)
            message(FATAL_ERROR "${LABEL}: ${N} ${KIND} index entries, expected ${WANT}")
        endif()
    endforeach()
    message(STATUS "${LABEL}: index OK (${EXPECTED_SPECTRA} spectra, ${EXPECTED_CHROMS} chromatograms)")
endfunction()

# Content up to and including </mzML>.
function(mzml_body FILE OUT)
    file(READ "${FILE}" CONTENT)
    string(FIND "${CONTENT}" "</mzML>" POS REVERSE)
    math(EXPR LEN "${POS} + 7")
    string(SUBSTRING "${CONTENT}" 0 ${LEN} CONTENT)
    set(${OUT} "${CONTENT}" PARENT_SCOPE)
endfunction()

function(check_version LABEL MSZ WANT)
    execute_process(COMMAND "${EXECUTABLE}" --describe "${MSZ}"
        OUTPUT_VARIABLE O RESULT_VARIABLE R)
    string(REGEX MATCH ",([0-9]+\\.[0-9]+),(true|false)\n?$" _ "${O}")
    if(NOT CMAKE_MATCH_1 STREQUAL "${WANT}")
        message(FATAL_ERROR "${LABEL}: msz version '${CMAKE_MATCH_1}', expected ${WANT}\n${O}")
    endif()
endfunction()

# 1. Round trips
foreach(CASE "plain;${TEST_MZML};50" "zlib;${TEST_ZLIB_MZML};12")
    list(GET CASE 0 NAME)
    list(GET CASE 1 SRC)
    list(GET CASE 2 NSPEC)
    run_ok("${EXECUTABLE}" "${SRC}" "${TEMP_DIR}/${NAME}.msz")
    check_version(${NAME} "${TEMP_DIR}/${NAME}.msz" "0.2")
    run_ok("${EXECUTABLE}" "${TEMP_DIR}/${NAME}.msz" "${TEMP_DIR}/${NAME}.mzML")
    check_index(${NAME}_roundtrip "${TEMP_DIR}/${NAME}.mzML" ${NSPEC} 1)
    if(NAME STREQUAL "plain")
        mzml_body("${SRC}" A)
        mzml_body("${TEMP_DIR}/${NAME}.mzML" B)
        if(NOT A STREQUAL B)
            message(FATAL_ERROR "plain: content up to </mzML> changed")
        endif()
    endif()
endforeach()

# The zlib fixture's index is correct, so its round trip must keep every
# id; only offsets past the first re-encoded binary may move.
file(STRINGS "${TEST_ZLIB_MZML}" SRC_IDS REGEX "<offset idRef=")
file(STRINGS "${TEMP_DIR}/zlib.mzML" OUT_IDS REGEX "<offset idRef=")
list(TRANSFORM SRC_IDS REPLACE ">[0-9]+<" "><")
list(TRANSFORM OUT_IDS REPLACE ">[0-9]+<" "><")
if(NOT SRC_IDS STREQUAL OUT_IDS)
    message(FATAL_ERROR "zlib: index ids differ from the source index")
endif()

# 2. --extract
foreach(KIND mzml msz)
    if(KIND STREQUAL "mzml")
        set(INPUT "${TEST_ZLIB_MZML}")
    else()
        set(INPUT "${TEMP_DIR}/zlib.msz")
    endif()
    run_ok("${EXECUTABLE}" --extract --extract-scans "[2,5,9]" "${INPUT}"
           "${TEMP_DIR}/extract_${KIND}.mzML")
    check_index(extract_${KIND} "${TEMP_DIR}/extract_${KIND}.mzML" 3 1)
    run_ok("${EXECUTABLE}" --extract --extract-indices "[11]" "${INPUT}"
           "${TEMP_DIR}/extract_last_${KIND}.mzML")
    check_index(extract_last_${KIND} "${TEMP_DIR}/extract_last_${KIND}.mzML" 1 1)
endforeach()

# 3. Non-indexed mzML: nothing to rebuild
run_ok("${EXECUTABLE}" "${TEST_NONINDEXED}" "${TEMP_DIR}/nonindexed.msz")
check_version(nonindexed "${TEMP_DIR}/nonindexed.msz" "0.2")
run_ok("${EXECUTABLE}" "${TEMP_DIR}/nonindexed.msz" "${TEMP_DIR}/nonindexed.mzML")
file(MD5 "${TEST_NONINDEXED}" A)
file(MD5 "${TEMP_DIR}/nonindexed.mzML" B)
if(NOT A STREQUAL B)
    message(FATAL_ERROR "nonindexed: round trip is not byte-identical")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "mzML index tests passed!")
