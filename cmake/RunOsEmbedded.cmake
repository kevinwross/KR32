if(NOT DEFINED KR32 OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "KR32 and BINARY_DIR are required")
endif()

# Run away from the development kr32os/build directory and clear the override
# variables so this only succeeds through the embedded image pair.
set(_work "${BINARY_DIR}/embedded-os")
file(MAKE_DIRECTORY "${_work}")
file(REMOVE_RECURSE "${_work}/kr32os")
file(REMOVE "${_work}/bios.bin" "${_work}/kernel.bin")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            --unset=KR32OS_BIN --unset=KR32OS_BIOS
            "${KR32}" os --embedded --max-steps 12000000 --serial-input "exit\n"
    WORKING_DIRECTORY "${_work}"
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "embedded kr32 os failed (${_result}): ${_output}${_error}")
endif()
if(NOT _output MATCHES "os: embedded kernel")
    message(FATAL_ERROR "embedded OS source was not reported: ${_output}")
endif()
if(NOT _output MATCHES "HALTED exit=0")
    message(FATAL_ERROR "embedded KR32OS did not exit cleanly: ${_output}")
endif()
