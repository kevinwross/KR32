if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "KR32, SOURCE_DIR, and BINARY_DIR are required")
endif()

if(WIN32)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${SOURCE_DIR}/kr32os/build.ps1" -Kr32 "${KR32}"
        RESULT_VARIABLE _build_result)
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "KR32=${KR32}"
                bash "${SOURCE_DIR}/kr32os/build.sh"
        RESULT_VARIABLE _build_result)
endif()
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "KR32OS M2 build failed")
endif()

set(_input "help\necho hello\nmem\nexit\n")
execute_process(
    COMMAND "${KR32}" os "${SOURCE_DIR}/kr32os/build/kernel.bin"
            --bios "${SOURCE_DIR}/kr32os/build/bios.bin"
            --serial-input "${_input}"
    OUTPUT_VARIABLE _output
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "KR32OS M2 run failed: ${_output}")
endif()
if(NOT _output MATCHES "help echo clear mem ps ls cat run exit")
    message(FATAL_ERROR "KR32OS M2 help output missing: ${_output}")
endif()
if(NOT _output MATCHES "hello")
    message(FATAL_ERROR "KR32OS M2 echo output missing: ${_output}")
endif()
if(NOT _output MATCHES "HALTED exit=0")
    message(FATAL_ERROR "KR32OS M2 did not exit cleanly: ${_output}")
endif()
