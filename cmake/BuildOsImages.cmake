# Builds the KR32OS images once for the tests that boot them.
#
# Each OS test used to run kr32os/build.ps1 itself, which is most of its cost -
# 65 s per test on Windows against 12 s on WSL - and forced them to serialise on a
# shared RESOURCE_LOCK. They now declare FIXTURES_REQUIRED on this test and skip
# their own build, so the images are produced once per gate.
if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "KR32 and SOURCE_DIR are required")
endif()

if(WIN32)
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${SOURCE_DIR}/kr32os/build.ps1" -Kr32 "${KR32}"
        RESULT_VARIABLE _build_result)
    set(_osdir "${SOURCE_DIR}/kr32os/build/windows")
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "KR32=${KR32}"
                bash "${SOURCE_DIR}/kr32os/build.sh"
        RESULT_VARIABLE _build_result)
    set(_osdir "${SOURCE_DIR}/kr32os/build/linux")
endif()
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "KR32OS M4 build failed")
endif()
foreach(_image kernel.bin bios.bin)
    if(NOT EXISTS "${_osdir}/${_image}")
        message(FATAL_ERROR "KR32OS build did not produce ${_osdir}/${_image}")
    endif()
endforeach()