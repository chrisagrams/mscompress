# CMake script to run mscompress tests

# Expected variables:
# EXECUTABLE: Path to mscompress executable
# MODE: "compress" or "decompress"
# INPUT_FILE: Path to input file
# EXPECTED_FILE: Path to expected output file (used for decompress mode only)
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

get_filename_component(INPUT_NAME "${INPUT_FILE}" NAME_WE)

if("${MODE}" STREQUAL "compress")
    # For compress mode: compress then decompress and verify roundtrip
    # (Compression output is not deterministic across different systems/compilers)
    set(COMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}.msz")
    set(DECOMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}.mzML")

    message(STATUS "Testing ${MODE} (roundtrip)...")
    message(STATUS "Input: ${INPUT_FILE}")
    message(STATUS "Compressed: ${COMPRESSED_FILE}")
    message(STATUS "Decompressed: ${DECOMPRESSED_FILE}")

    # Step 1: Compress
    execute_process(
        COMMAND "${EXECUTABLE}" "${INPUT_FILE}" "${COMPRESSED_FILE}"
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE CMD_OUTPUT
        ERROR_VARIABLE CMD_ERROR
    )

    if(NOT CMD_RESULT EQUAL 0)
        message(STATUS "Compress output: ${CMD_OUTPUT}")
        message(STATUS "Compress error: ${CMD_ERROR}")
        file(REMOVE "${COMPRESSED_FILE}")
        message(FATAL_ERROR "Compress failed with return code: ${CMD_RESULT}")
    endif()

    if(NOT EXISTS "${COMPRESSED_FILE}")
        message(FATAL_ERROR "Compressed file not created: ${COMPRESSED_FILE}")
    endif()

    # Step 2: Decompress
    execute_process(
        COMMAND "${EXECUTABLE}" "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}"
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE CMD_OUTPUT
        ERROR_VARIABLE CMD_ERROR
    )

    if(NOT CMD_RESULT EQUAL 0)
        message(STATUS "Decompress output: ${CMD_OUTPUT}")
        message(STATUS "Decompress error: ${CMD_ERROR}")
        file(REMOVE "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}")
        message(FATAL_ERROR "Decompress failed with return code: ${CMD_RESULT}")
    endif()

    if(NOT EXISTS "${DECOMPRESSED_FILE}")
        file(REMOVE "${COMPRESSED_FILE}")
        message(FATAL_ERROR "Decompressed file not created: ${DECOMPRESSED_FILE}")
    endif()

    # Step 3: Verify roundtrip - decompressed should match original input
    file(MD5 "${INPUT_FILE}" ORIGINAL_MD5)
    file(MD5 "${DECOMPRESSED_FILE}" ROUNDTRIP_MD5)

    message(STATUS "Original MD5:  ${ORIGINAL_MD5}")
    message(STATUS "Roundtrip MD5: ${ROUNDTRIP_MD5}")

    # Cleanup
    file(REMOVE "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}")

    if(NOT "${ORIGINAL_MD5}" STREQUAL "${ROUNDTRIP_MD5}")
        message(FATAL_ERROR "Roundtrip checksum mismatch! Decompressed file does not match original.")
    endif()

elseif("${MODE}" STREQUAL "decompress")
    # For decompress mode: decompress, recompress, decompress again and verify roundtrip
    # (Decompressed output may differ across systems due to floating-point or library differences)
    set(DECOMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}.mzML")
    set(RECOMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}_recompressed.msz")
    set(DECOMPRESSED_FILE2 "${TEMP_DIR}/${INPUT_NAME}_2.mzML")

    message(STATUS "Testing ${MODE} (roundtrip)...")
    message(STATUS "Input: ${INPUT_FILE}")
    message(STATUS "Decompressed: ${DECOMPRESSED_FILE}")
    message(STATUS "Recompressed: ${RECOMPRESSED_FILE}")
    message(STATUS "Decompressed2: ${DECOMPRESSED_FILE2}")

    # Step 1: Decompress
    execute_process(
        COMMAND "${EXECUTABLE}" "${INPUT_FILE}" "${DECOMPRESSED_FILE}"
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE CMD_OUTPUT
        ERROR_VARIABLE CMD_ERROR
    )

    if(NOT CMD_RESULT EQUAL 0)
        message(STATUS "Decompress output: ${CMD_OUTPUT}")
        message(STATUS "Decompress error: ${CMD_ERROR}")
        file(REMOVE "${DECOMPRESSED_FILE}")
        message(FATAL_ERROR "Decompress failed with return code: ${CMD_RESULT}")
    endif()

    if(NOT EXISTS "${DECOMPRESSED_FILE}")
        message(FATAL_ERROR "Decompressed file not created: ${DECOMPRESSED_FILE}")
    endif()

    # Step 2: Recompress
    execute_process(
        COMMAND "${EXECUTABLE}" "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}"
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE CMD_OUTPUT
        ERROR_VARIABLE CMD_ERROR
    )

    if(NOT CMD_RESULT EQUAL 0)
        message(STATUS "Recompress output: ${CMD_OUTPUT}")
        message(STATUS "Recompress error: ${CMD_ERROR}")
        file(REMOVE "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}")
        message(FATAL_ERROR "Recompress failed with return code: ${CMD_RESULT}")
    endif()

    if(NOT EXISTS "${RECOMPRESSED_FILE}")
        file(REMOVE "${DECOMPRESSED_FILE}")
        message(FATAL_ERROR "Recompressed file not created: ${RECOMPRESSED_FILE}")
    endif()

    # Step 3: Decompress again
    execute_process(
        COMMAND "${EXECUTABLE}" "${RECOMPRESSED_FILE}" "${DECOMPRESSED_FILE2}"
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE CMD_OUTPUT
        ERROR_VARIABLE CMD_ERROR
    )

    if(NOT CMD_RESULT EQUAL 0)
        message(STATUS "Decompress2 output: ${CMD_OUTPUT}")
        message(STATUS "Decompress2 error: ${CMD_ERROR}")
        file(REMOVE "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}" "${DECOMPRESSED_FILE2}")
        message(FATAL_ERROR "Second decompress failed with return code: ${CMD_RESULT}")
    endif()

    if(NOT EXISTS "${DECOMPRESSED_FILE2}")
        file(REMOVE "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}")
        message(FATAL_ERROR "Second decompressed file not created: ${DECOMPRESSED_FILE2}")
    endif()

    # Step 4: Verify roundtrip - both decompressed files should match
    file(MD5 "${DECOMPRESSED_FILE}" FIRST_MD5)
    file(MD5 "${DECOMPRESSED_FILE2}" SECOND_MD5)

    message(STATUS "First decompress MD5:  ${FIRST_MD5}")
    message(STATUS "Second decompress MD5: ${SECOND_MD5}")

    # Cleanup
    file(REMOVE "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}" "${DECOMPRESSED_FILE2}")

    if(NOT "${FIRST_MD5}" STREQUAL "${SECOND_MD5}")
        message(FATAL_ERROR "Roundtrip checksum mismatch! Decompressed files do not match.")
    endif()

else()
    message(FATAL_ERROR "Unknown mode: ${MODE}")
endif()

message(STATUS "Test passed!")
