# --extract regression tests (GitHub issue #195):
#   1. Multi-scan --extract-scans requests return every requested scan, not
#      just the first ceil(N/2).
#   2. Extracting the file's final spectrum from an mzML does not crash.
# Each request is run against both the mzML and the msz input, and the scan
# numbers of the <spectrum> elements in the output are compared exactly.
#
# Expected variables: EXECUTABLE, TEST_MZML, TEST_MSZ, TEMP_DIR

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}")

# Runs one extraction and checks the output holds exactly EXPECTED (a list of
# scan numbers, in order).
function(check_extract LABEL INPUT FLAG REQUEST EXPECTED)
    set(OUT "${TEMP_DIR}/${LABEL}.mzML")
    execute_process(
        COMMAND "${EXECUTABLE}" --extract ${FLAG} "${REQUEST}" "${INPUT}" "${OUT}"
        RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
    if(NOT R EQUAL 0)
        message(FATAL_ERROR "${LABEL}: ${FLAG} ${REQUEST} exited with ${R}: ${E}")
    endif()

    file(STRINGS "${OUT}" SPECTRA REGEX "<spectrum ")
    set(GOT "")
    foreach(LINE IN LISTS SPECTRA)
        string(REGEX MATCH "scan=([0-9]+)" _ "${LINE}")
        list(APPEND GOT "${CMAKE_MATCH_1}")
    endforeach()

    if(NOT "${GOT}" STREQUAL "${EXPECTED}")
        message(FATAL_ERROR
            "${LABEL}: ${FLAG} ${REQUEST} extracted scans [${GOT}], expected [${EXPECTED}]")
    endif()
    message(STATUS "${LABEL}: ${FLAG} ${REQUEST} -> [${GOT}]")
endfunction()

foreach(KIND mzml msz)
    if(KIND STREQUAL "mzml")
        set(INPUT "${TEST_MZML}")
    else()
        set(INPUT "${TEST_MSZ}")
    endif()

    check_extract(${KIND}_range      "${INPUT}" --extract-scans "[1-3]"     "1;2;3")
    check_extract(${KIND}_list       "${INPUT}" --extract-scans "[2,3,4]"   "2;3;4")
    check_extract(${KIND}_last       "${INPUT}" --extract-scans "[50]"      "50")
    check_extract(${KIND}_tail       "${INPUT}" --extract-scans "[49-50]"   "49;50")
    check_extract(${KIND}_spread     "${INPUT}" --extract-scans "[1,25,50]" "1;25;50")
    check_extract(${KIND}_last_index "${INPUT}" --extract-indices "[49]"    "50")
endforeach()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Extract tests passed!")
