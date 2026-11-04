if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "KR32, SOURCE_DIR, and BINARY_DIR are required")
endif()

set(_kernel "${BINARY_DIR}/os-kernel.bin")
set(_bios "${BINARY_DIR}/os-bios.bin")

execute_process(
    COMMAND "${KR32}" as "${SOURCE_DIR}/tests/fixtures/os-kernel.s"
            --base 0x00020000 -o "${_kernel}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "OS kernel assembly failed")
endif()

execute_process(
    COMMAND "${KR32}" as "${SOURCE_DIR}/tests/fixtures/os-bios.s"
            --base 0xFFFF0000 -o "${_bios}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "OS BIOS assembly failed")
endif()

execute_process(
    COMMAND "${KR32}" os "${_kernel}" --bios "${_bios}"
    OUTPUT_VARIABLE _output
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0 AND NOT _result EQUAL 42)
    message(FATAL_ERROR "kr32 os failed: ${_output}")
endif()
if(NOT _output MATCHES "HALTED exit=42")
    message(FATAL_ERROR "kr32 os did not reach the test kernel: ${_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "KR32OS_BIN=${_kernel}" "KR32OS_BIOS=${_bios}"
            "${KR32}" os
    OUTPUT_VARIABLE _default_output
    RESULT_VARIABLE _default_result)
if(NOT _default_result EQUAL 0 AND NOT _default_result EQUAL 42)
    message(FATAL_ERROR "default kr32 os failed: ${_default_output}")
endif()
if(NOT _default_output MATCHES "HALTED exit=42")
    message(FATAL_ERROR "default kr32 os did not reach the test kernel: ${_default_output}")
endif()
