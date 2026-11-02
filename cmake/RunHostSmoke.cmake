if(NOT KR32 OR NOT SOURCE_DIR OR NOT BINARY_DIR)
    message(FATAL_ERROR "kr32, SOURCE_DIR, and BINARY_DIR are required")
endif()

set(image "${BINARY_DIR}/fileio.k32")
set(sandbox "${BINARY_DIR}/host-sandbox")
file(REMOVE_RECURSE "${sandbox}")

execute_process(
    COMMAND "${KR32}" build "${SOURCE_DIR}/examples/fileio.c" -o "${image}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "fileio build failed: ${build_output}${build_error}")
endif()

execute_process(
    COMMAND "${KR32}" vm "${image}" --fs-root "${sandbox}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "fileio run failed: ${run_output}${run_error}")
endif()

if(NOT EXISTS "${sandbox}/host-output.txt")
    message(FATAL_ERROR "host-output.txt was not created")
endif()
file(READ "${sandbox}/host-output.txt" content)
if(NOT content MATCHES "hello from KR32")
    message(FATAL_ERROR "unexpected file contents: ${content}")
endif()
message(STATUS "host file I/O smoke passed")
