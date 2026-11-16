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
if(WIN32)
    set(_osdir "${SOURCE_DIR}/kr32os/build/windows")
else()
    set(_osdir "${SOURCE_DIR}/kr32os/build/linux")
endif()

if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "KR32OS M4 build failed")
endif()

set(_input "ls\nrun init\nrun wx\nrun fault\nexit 0\n")
execute_process(
    COMMAND "${KR32}" os "${_osdir}/kernel.bin"
            --bios "${_osdir}/bios.bin" --max-steps 12000000
            --serial-input "${_input}"
    OUTPUT_VARIABLE _output
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "KR32OS M4 run failed: ${_output}")
endif()
if(NOT _output MATCHES "init: hello from user mode")
    message(FATAL_ERROR "KR32OS M4 user program output missing: ${_output}")
endif()
if(NOT _output MATCHES "exit status 139")
    message(FATAL_ERROR "KR32OS M4 user fault was not contained: ${_output}")
endif()
if(NOT _output MATCHES "exit status 0")
    message(FATAL_ERROR "KR32OS M4 user exit status missing: ${_output}")
endif()
if(NOT _output MATCHES "HALTED exit=0")
    message(FATAL_ERROR "KR32OS M4 did not exit cleanly: ${_output}")
endif()
