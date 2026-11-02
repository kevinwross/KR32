if(NOT KR32 OR NOT BINARY_DIR)
    message(FATAL_ERROR "kr32 and BINARY_DIR are required")
endif()

set(script "${BINARY_DIR}/shell-script.txt")
file(WRITE "${script}" "version\nhistory\nexit\n")

execute_process(
    COMMAND "${KR32}" shell
    INPUT_FILE "${script}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "shell exited ${result}: ${errors}")
endif()
if(NOT output MATCHES "kr32 0\\.1\\.0")
    message(FATAL_ERROR "shell did not print the version: ${output}")
endif()
if(NOT output MATCHES "history")
    message(FATAL_ERROR "shell did not print history: ${output}")
endif()
message(STATUS "shell smoke passed")
