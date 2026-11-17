# Dismisses a graphics program through the serial console and checks that it
# returns to the shell with status 0.
#
# The programs block in sys_read, which serves the keyboard and the serial
# device, so a byte queued after the run command stands in for the user's key.
# The screenshot tests send nothing, which is why they can still sample the frame.
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
    message(FATAL_ERROR "KR32OS M4 build failed")
endif()

foreach(_program gfxdemo gfxcheck)
    execute_process(
        COMMAND "${KR32}" os "${_osdir}/kernel.bin"
                --bios "${_osdir}/bios.bin" --max-steps 12000000
                --serial-input "run ${_program}\nx\nexit 0\n"
        OUTPUT_VARIABLE _output
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_program} did not exit cleanly: ${_output}")
    endif()
    if(NOT _output MATCHES "exit status 0")
        message(FATAL_ERROR "${_program} did not return to the shell: ${_output}")
    endif()
    if(NOT _output MATCHES "HALTED exit=0")
        message(FATAL_ERROR "${_program} left the machine in a bad state: ${_output}")
    endif()
endforeach()

message(STATUS "KR32OS graphics dismissal passed")
