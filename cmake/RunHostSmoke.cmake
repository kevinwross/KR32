if(NOT KR32 OR NOT IMAGE OR NOT BINARY_DIR)
    message(FATAL_ERROR "kr32, IMAGE and BINARY_DIR are required")
endif()

set(image "${IMAGE}")
set(sandbox "${BINARY_DIR}/host-sandbox")
file(REMOVE_RECURSE "${sandbox}")

if(NOT EXISTS "${image}")
    message(FATAL_ERROR "missing ${image}")
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
