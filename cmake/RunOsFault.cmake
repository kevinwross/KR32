if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "KR32, SOURCE_DIR, and BINARY_DIR are required")
endif()

if(DEFINED SKIP_BUILD)
    set(_build_result 0)
else()
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
endif()
if(WIN32)
    set(_osdir "${SOURCE_DIR}/kr32os/build/windows")
else()
    set(_osdir "${SOURCE_DIR}/kr32os/build/linux")
endif()

if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "KR32OS M3 build failed")
endif()

set(_fault_input "crash\n")
execute_process(
    COMMAND "${KR32}" os "${_osdir}/kernel.bin"
            --bios "${_osdir}/bios.bin" --max-steps 24000000
            --serial-input "${_fault_input}"
    OUTPUT_VARIABLE _fault_output
    RESULT_VARIABLE _fault_result)
if(_fault_result EQUAL 0)
    message(FATAL_ERROR "KR32OS fault test exited successfully")
endif()
if(NOT _fault_output MATCHES "panic vector=3")
    message(FATAL_ERROR "KR32OS panic output missing: ${_fault_output}")
endif()

# Boot runs millions of steps with interrupts enabled, so the timer has
# advanced before the shell prompt; one mem query observes it.
set(_timer_input "mem\nexit 0\n")
execute_process(
    COMMAND "${KR32}" os "${_osdir}/kernel.bin"
            --bios "${_osdir}/bios.bin" --max-steps 24000000
            --serial-input "${_timer_input}"
    OUTPUT_VARIABLE _timer_output
    RESULT_VARIABLE _timer_result)
if(NOT _timer_result EQUAL 0)
    message(FATAL_ERROR "KR32OS timer test failed: ${_timer_output}")
endif()
if(NOT _timer_output MATCHES "ticks=[1-9][0-9]*")
    message(FATAL_ERROR "KR32OS timer did not advance: ${_timer_output}")
endif()
