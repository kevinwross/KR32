# Runs the KR32OS shutdown paths and checks the exit status the host returns.
#
# The user-mode program calls sys_shutdown(7); the shell command takes its
# status as an argument. Both must reach the host process as its exit code.
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

execute_process(
    COMMAND "${KR32}" os "${_osdir}/kernel.bin"
            --bios "${_osdir}/bios.bin" --max-steps 12000000
            --serial-input "run shutdown\n"
    OUTPUT_VARIABLE _user_output
    RESULT_VARIABLE _user_result)
if(NOT _user_result EQUAL 7)
    message(FATAL_ERROR "user shutdown returned ${_user_result}, expected 7: ${_user_output}")
endif()
if(NOT _user_output MATCHES "shutdown: exiting with status 7")
    message(FATAL_ERROR "shutdown program output missing: ${_user_output}")
endif()
if(NOT _user_output MATCHES "HALTED exit=7")
    message(FATAL_ERROR "machine did not halt with the shutdown status: ${_user_output}")
endif()

execute_process(
    COMMAND "${KR32}" os "${_osdir}/kernel.bin"
            --bios "${_osdir}/bios.bin" --max-steps 12000000
            --serial-input "exit 5\n"
    OUTPUT_VARIABLE _shell_output
    RESULT_VARIABLE _shell_result)
if(NOT _shell_result EQUAL 5)
    message(FATAL_ERROR "shell exit 5 returned ${_shell_result}: ${_shell_output}")
endif()
if(NOT _shell_output MATCHES "HALTED exit=5")
    message(FATAL_ERROR "shell exit status missing: ${_shell_output}")
endif()

message(STATUS "KR32OS shutdown exit status passed")
