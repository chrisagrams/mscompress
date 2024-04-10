cmake_minimum_required(VERSION 3.15)

project(mscompress_modules VERSION 1.0)

# Find PyInstaller
find_program(PYINSTALLER_EXECUTABLE pyinstaller)

# Verify if PyInstaller is found
if(PYINSTALLER_EXECUTABLE)
    message(STATUS "PyInstaller found: ${PYINSTALLER_EXECUTABLE}")
    # Set the path to your Python script
    set(PYTHON_SCRIPT "mzml_to_tic.py")

    # Define the output file path
    set(OUTPUT_FILE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dist/${PYTHON_SCRIPT}")

    # Add PyInstaller command as a build command
    add_custom_command(OUTPUT ${OUTPUT_FILE_PATH}
        COMMAND ${PYINSTALLER_EXECUTABLE} --onefile ${CMAKE_CURRENT_SOURCE_DIR}/${PYTHON_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running PyInstaller..."
        DEPENDS ${PYTHON_SCRIPT} # Ensure that the Python script is treated as a dependency
    )

    # Make the Python script a dependency of the custom target
    add_custom_target(run_pyinstaller ALL
        DEPENDS ${OUTPUT_FILE_PATH}
    )

else()
    message(FATAL_ERROR "PyInstaller not found. Please make sure it is installed and accessible.")
endif()
