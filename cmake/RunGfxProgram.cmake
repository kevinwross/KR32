# Runs a KR32OS user program and checks pixels in its screenshot.
#
# Required: KR32, SOURCE_DIR, BINARY_DIR, WORK, PROGRAM, CHECKS
# CHECKS is a list of "x,y,RRGGBB" entries.
if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR OR
   NOT DEFINED WORK OR NOT DEFINED PROGRAM OR NOT DEFINED CHECKS)
    message(FATAL_ERROR "KR32, SOURCE_DIR, BINARY_DIR, WORK, PROGRAM, and CHECKS are required")
endif()
if(NOT DEFINED STEPS)
    # The frame is presented well inside this budget; the programs then wait for
    # a key, which this harness cannot send, so the run ends at the budget with
    # the frame still on screen. Five million steps is over the three million
    # that already draw the frame, and keeps the test near ten seconds.
    set(STEPS 5000000)
endif()

file(MAKE_DIRECTORY "${WORK}")
if(WIN32)
    set(_osdir "${SOURCE_DIR}/kr32os/build/windows")
else()
    set(_osdir "${SOURCE_DIR}/kr32os/build/linux")
endif()
if(DEFINED SKIP_BUILD)
    set(_build_result 0)
elseif(WIN32)
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
    message(FATAL_ERROR "KR32OS build failed")
endif()

set(_shot "${WORK}/${PROGRAM}.ppm")
file(REMOVE "${_shot}")
execute_process(
    COMMAND "${KR32}" display --os "${_osdir}/kernel.k32"
            --bios "${_osdir}/bios.k32" --serial-input "run ${PROGRAM}\n"
            --steps ${STEPS} --screen-shot "${_shot}"
    RESULT_VARIABLE _run_result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _run_result EQUAL 0)
    message(FATAL_ERROR "${PROGRAM} run failed: ${_output} ${_error}")
endif()
if(NOT EXISTS "${_shot}")
    message(FATAL_ERROR "${PROGRAM} produced no screenshot")
endif()

file(READ "${_shot}" _hex HEX)
# "P6\n800 600\n255\n" is 15 bytes, so the pixel data starts at hex offset 30.
string(SUBSTRING "${_hex}" 30 -1 _pixels)
string(LENGTH "${_pixels}" _pixels_length)
if(NOT _pixels_length EQUAL 2880000)
    message(FATAL_ERROR "unexpected screenshot size: ${_pixels_length} hex chars")
endif()

foreach(_check IN LISTS CHECKS)
    string(REPLACE "," ";" _fields "${_check}")
    list(GET _fields 0 _x)
    list(GET _fields 1 _y)
    list(GET _fields 2 _expected)
    math(EXPR _offset "(${_y} * 800 + ${_x}) * 6")
    string(SUBSTRING "${_pixels}" ${_offset} 6 _pixel)
    string(TOUPPER "${_pixel}" _pixel)
    string(TOUPPER "${_expected}" _expected)
    if(NOT _pixel STREQUAL "${_expected}")
        message(FATAL_ERROR "${PROGRAM}: pixel (${_x},${_y}) is ${_pixel}, expected ${_expected}")
    endif()
endforeach()
