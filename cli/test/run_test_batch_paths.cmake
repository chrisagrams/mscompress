# Batch-mode paths that BatchRoundTripTest does not reach:
#   1. Entry-name collisions: two files with the same basename in different
#      directories must become "sample.msz" + "sample__2.msz" and both must
#      expand. (The round-trip test uses distinct names, so __N never fires.)
#   2. --continue-on-error: a non-mzML input must be SKIPPED, leaving a valid
#      archive containing the good entries. This previously deleted the output.
#   3. Fail-fast (no --continue-on-error): the same input set must abort and
#      leave no archive behind.
#   4. Legacy two-positional form must not be inferred as batch just because the
#      output file already exists (re-run / overwrite).
#   5. Same for --extract, whose output is itself an .mzML: re-running onto an
#      existing output must stay single-file (it used to segfault).
#   6. --extract with a batch-style input (a directory) must be rejected with
#      an error, not crash.
#   7. A trailing .mszx positional is the output, not a (missing) input, with
#      or without --batch, case-insensitively, and for a single mzML input.
#      It used to be skipped as "not found" and ./batch.mszx written instead.
#   8. Re-running onto that now-existing .mszx must not feed it back in.
#   9. A missing positional must not change the default name: `<dir> <typo>`
#      still defaults to <dirbase>.mszx.
#
# Expected variables: EXECUTABLE, TEST_MZML, TEMP_DIR

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()
if(NOT EXISTS "${TEST_MZML}")
    message(FATAL_ERROR "Test mzML not found: ${TEST_MZML}")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}/tree/runA")
file(MAKE_DIRECTORY "${TEMP_DIR}/tree/runB")

configure_file("${TEST_MZML}" "${TEMP_DIR}/tree/runA/sample.mzML" COPYONLY)
configure_file("${TEST_MZML}" "${TEMP_DIR}/tree/runB/sample.mzML" COPYONLY)

# --- 1: colliding basenames get __N suffixes and both round-trip ---
set(COLL "${TEMP_DIR}/coll.mszx")
execute_process(
    COMMAND "${EXECUTABLE}" --batch "${TEMP_DIR}/tree" -r -o "${COLL}"
    RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
if(NOT R EQUAL 0)
    message(STATUS "out: ${O}\nerr: ${E}")
    message(FATAL_ERROR "batch collision compress failed (${R})")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar tf "${COLL}"
    RESULT_VARIABLE TR OUTPUT_VARIABLE TOUT ERROR_VARIABLE TERR)
if(NOT TR EQUAL 0)
    message(FATAL_ERROR "tar tf failed on collision archive: ${TERR}")
endif()
if(NOT TOUT MATCHES "sample\\.msz")
    message(FATAL_ERROR "expected sample.msz in archive, got: ${TOUT}")
endif()
if(NOT TOUT MATCHES "sample__2\\.msz")
    message(FATAL_ERROR "expected collision-suffixed sample__2.msz, got: ${TOUT}")
endif()

execute_process(
    COMMAND "${EXECUTABLE}" "${COLL}" "${TEMP_DIR}/out_coll"
    RESULT_VARIABLE DR OUTPUT_VARIABLE DO ERROR_VARIABLE DE)
if(NOT DR EQUAL 0)
    message(STATUS "out: ${DO}\nerr: ${DE}")
    message(FATAL_ERROR "collision archive failed to expand (${DR})")
endif()
foreach(NAME sample.mzML sample__2.mzML)
    if(NOT EXISTS "${TEMP_DIR}/out_coll/${NAME}")
        message(FATAL_ERROR "expanded file missing: ${NAME}")
    endif()
    file(MD5 "${TEMP_DIR}/out_coll/${NAME}" GOT)
    file(MD5 "${TEST_MZML}" WANT)
    if(NOT GOT STREQUAL WANT)
        message(FATAL_ERROR "${NAME} differs from the original mzML")
    endif()
endforeach()

# --- 2: --continue-on-error skips a bad input but still writes the archive ---
file(WRITE "${TEMP_DIR}/junk.txt" "this is not an mzML file\n")
set(COE "${TEMP_DIR}/coe.mszx")
execute_process(
    COMMAND "${EXECUTABLE}" --batch
            "${TEMP_DIR}/tree/runA/sample.mzML"
            "${TEMP_DIR}/tree/runB/sample.mzML"
            "${TEMP_DIR}/junk.txt"
            --continue-on-error -o "${COE}"
    RESULT_VARIABLE CR OUTPUT_VARIABLE CO ERROR_VARIABLE CE)
if(NOT CR EQUAL 0)
    message(STATUS "out: ${CO}\nerr: ${CE}")
    message(FATAL_ERROR "--continue-on-error should succeed, got ${CR}")
endif()
if(NOT EXISTS "${COE}")
    message(FATAL_ERROR "--continue-on-error deleted the archive")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar tf "${COE}"
    RESULT_VARIABLE CTR OUTPUT_VARIABLE CTOUT)
if(NOT CTR EQUAL 0)
    message(FATAL_ERROR "--continue-on-error produced an invalid tar")
endif()
if(NOT CTOUT MATCHES "manifest\\.json")
    message(FATAL_ERROR "manifest.json missing after a skipped input: ${CTOUT}")
endif()
if(CTOUT MATCHES "junk")
    message(FATAL_ERROR "skipped input leaked into the archive: ${CTOUT}")
endif()

# --- 3: without --continue-on-error the same set aborts, leaving no archive ---
set(FF "${TEMP_DIR}/ff.mszx")
execute_process(
    COMMAND "${EXECUTABLE}" --batch
            "${TEMP_DIR}/tree/runA/sample.mzML" "${TEMP_DIR}/junk.txt"
            -o "${FF}"
    RESULT_VARIABLE FR OUTPUT_VARIABLE FO ERROR_VARIABLE FE)
if(FR EQUAL 0)
    message(FATAL_ERROR "fail-fast should have aborted on a non-mzML input")
endif()
if(EXISTS "${FF}")
    message(FATAL_ERROR "aborted batch left a partial archive behind: ${FF}")
endif()

# --- 4: legacy `input output` must stay non-batch when output already exists ---
set(LEG_MSZ "${TEMP_DIR}/legacy.msz")
set(LEG_MZML "${TEMP_DIR}/legacy.mzML")
execute_process(
    COMMAND "${EXECUTABLE}" "${TEST_MZML}" "${LEG_MSZ}"
    RESULT_VARIABLE LR1 ERROR_VARIABLE LE1)
if(NOT LR1 EQUAL 0)
    message(FATAL_ERROR "legacy compress failed (${LR1}): ${LE1}")
endif()
foreach(PASS 1 2)
    execute_process(
        COMMAND "${EXECUTABLE}" "${LEG_MSZ}" "${LEG_MZML}"
        RESULT_VARIABLE LR2 OUTPUT_VARIABLE LO2 ERROR_VARIABLE LE2)
    if(NOT LR2 EQUAL 0)
        message(STATUS "out: ${LO2}\nerr: ${LE2}")
        message(FATAL_ERROR
            "legacy decompress pass ${PASS} failed (${LR2}) — an existing "
            "output must not flip the invocation into batch mode")
    endif()
endforeach()

# --- 5: --extract onto an existing .mzML output must stay non-batch ---
set(EXT_MZML "${TEMP_DIR}/extract.mzML")
foreach(PASS 1 2)
    execute_process(
        COMMAND "${EXECUTABLE}" --extract --extract-indices "[0]"
                "${TEST_MZML}" "${EXT_MZML}"
        RESULT_VARIABLE ER OUTPUT_VARIABLE EO ERROR_VARIABLE EE)
    if(NOT ER EQUAL 0)
        message(STATUS "out: ${EO}\nerr: ${EE}")
        message(FATAL_ERROR
            "extract pass ${PASS} failed (${ER}) — an existing .mzML output "
            "must not flip --extract into batch mode")
    endif()
endforeach()

# --- 6: --extract with a directory input is rejected cleanly ---
execute_process(
    COMMAND "${EXECUTABLE}" --extract --extract-indices "[0]"
            "${TEMP_DIR}/tree" "${TEMP_DIR}/extract_dir.mzML"
    RESULT_VARIABLE DR OUTPUT_VARIABLE DO ERROR_VARIABLE DE)
if(NOT DR EQUAL 1)
    message(FATAL_ERROR
        "--extract with a directory input should exit 1, got ${DR}: ${DE}")
endif()
if(NOT DE MATCHES "single input file")
    message(FATAL_ERROR "expected a 'single input file' rejection, got: ${DE}")
endif()

# --- 7: trailing .mszx positional is the output (issue #225) ---
file(MAKE_DIRECTORY "${TEMP_DIR}/cwd")
set(POS "${TEMP_DIR}/pos.mszx")
foreach(FORM batch inferred upper single)
    if(FORM STREQUAL "batch")
        set(ARGS --batch "${TEMP_DIR}/tree/runA" "${POS}")
        set(WANT "${POS}")
    elseif(FORM STREQUAL "inferred")
        set(WANT "${TEMP_DIR}/pos_inferred.mszx")
        set(ARGS "${TEMP_DIR}/tree/runA" "${WANT}")
    elseif(FORM STREQUAL "upper")
        set(WANT "${TEMP_DIR}/pos_upper.MSZX")
        set(ARGS --batch "${TEMP_DIR}/tree/runA" "${WANT}")
    else()
        set(WANT "${TEMP_DIR}/pos_single.mszx")
        set(ARGS "${TEMP_DIR}/tree/runA/sample.mzML" "${WANT}")
    endif()
    execute_process(
        COMMAND "${EXECUTABLE}" ${ARGS}
        WORKING_DIRECTORY "${TEMP_DIR}/cwd"
        RESULT_VARIABLE PR OUTPUT_VARIABLE PO ERROR_VARIABLE PE)
    if(NOT PR EQUAL 0)
        message(STATUS "out: ${PO}\nerr: ${PE}")
        message(FATAL_ERROR "positional .mszx (${FORM}) failed (${PR})")
    endif()
    if(NOT EXISTS "${WANT}")
        message(FATAL_ERROR "positional .mszx (${FORM}) not written to ${WANT}")
    endif()
    if(EXISTS "${TEMP_DIR}/cwd/batch.mszx")
        message(FATAL_ERROR
            "positional .mszx (${FORM}) ignored; wrote ./batch.mszx instead")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar tf "${WANT}"
        RESULT_VARIABLE PTR OUTPUT_VARIABLE PTOUT)
    if(NOT PTR EQUAL 0 OR NOT PTOUT MATCHES "manifest\\.json")
        message(FATAL_ERROR "positional .mszx (${FORM}) is not a valid archive")
    endif()
endforeach()

# --- 8: re-running onto the existing .mszx must not ingest it ---
execute_process(
    COMMAND "${EXECUTABLE}" --batch "${TEMP_DIR}/tree/runA" "${POS}"
    WORKING_DIRECTORY "${TEMP_DIR}/cwd"
    RESULT_VARIABLE RR OUTPUT_VARIABLE RO ERROR_VARIABLE RE)
if(NOT RR EQUAL 0)
    message(STATUS "out: ${RO}\nerr: ${RE}")
    message(FATAL_ERROR "re-run onto an existing .mszx failed (${RR})")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar tf "${POS}"
    RESULT_VARIABLE RTR OUTPUT_VARIABLE RTOUT)
if(NOT RTR EQUAL 0)
    message(FATAL_ERROR "re-run produced an invalid tar")
endif()
if(RTOUT MATCHES "pos" OR RTOUT MATCHES "sample__2")
    message(FATAL_ERROR "re-run fed the old archive back in: ${RTOUT}")
endif()

# --- 9: a missing positional does not change the default output name ---
execute_process(
    COMMAND "${EXECUTABLE}" --batch "${TEMP_DIR}/tree/runA"
            "${TEMP_DIR}/no_such_dir"
    WORKING_DIRECTORY "${TEMP_DIR}/cwd"
    RESULT_VARIABLE MR OUTPUT_VARIABLE MO ERROR_VARIABLE ME)
if(NOT MR EQUAL 0)
    message(STATUS "out: ${MO}\nerr: ${ME}")
    message(FATAL_ERROR "batch with a missing positional failed (${MR})")
endif()
if(NOT EXISTS "${TEMP_DIR}/cwd/runA.mszx")
    message(FATAL_ERROR "missing positional changed the default name "
                        "(expected runA.mszx)")
endif()
if(EXISTS "${TEMP_DIR}/cwd/batch.mszx")
    message(FATAL_ERROR "missing positional fell back to batch.mszx")
endif()

file(REMOVE_RECURSE "${TEMP_DIR}")
message(STATUS "Batch path tests passed.")
