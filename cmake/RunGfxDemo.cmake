# Runs the gfxdemo user program under KR32OS and checks the rendered pixels.
if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR OR NOT DEFINED WORK)
    message(FATAL_ERROR "KR32, SOURCE_DIR, BINARY_DIR, and WORK are required")
endif()

file(MAKE_DIRECTORY "${WORK}")
if(WIN32)
    set(_osdir "${SOURCE_DIR}/kr32os/build/windows")
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${SOURCE_DIR}/kr32os/build.ps1" -Kr32 "${KR32}"
        RESULT_VARIABLE _build_result)
else()
    set(_osdir "${SOURCE_DIR}/kr32os/build/linux")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "KR32=${KR32}"
                bash "${SOURCE_DIR}/kr32os/build.sh"
        RESULT_VARIABLE _build_result)
endif()
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "KR32OS build failed")
endif()

set(_shot "${WORK}/gfxdemo.ppm")
file(REMOVE "${_shot}")
execute_process(
    COMMAND "${KR32}" display --os "${_osdir}/kernel.k32"
            --bios "${_osdir}/bios.k32" --serial-input "run gfxdemo\n"
            --steps 6000000 --screen-shot "${_shot}"
    RESULT_VARIABLE _run_result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _run_result EQUAL 0)
    message(FATAL_ERROR "gfxdemo run failed: ${_output} ${_error}")
endif()
if(NOT EXISTS "${_shot}")
    message(FATAL_ERROR "gfxdemo produced no screenshot")
endif()

file(READ "${_shot}" _hex HEX)
# "P6\n800 600\n255\n" is 15 bytes, so the pixel data starts at hex offset 30.
string(SUBSTRING "${_hex}" 30 -1 _pixels)
string(LENGTH "${_pixels}" _pixels_length)
if(NOT _pixels_length EQUAL 2880000)
    message(FATAL_ERROR "unexpected screenshot size: ${_pixels_length} hex chars")
endif()

function(check_pixel x y expected)
    math(EXPR _offset "(${y} * 800 + ${x}) * 6")
    string(SUBSTRING "${_pixels}" ${_offset} 6 _pixel)
    string(TOUPPER "${_pixel}" _pixel)
    if(NOT _pixel STREQUAL "${expected}")
        message(FATAL_ERROR "pixel (${x},${y}) is ${_pixel}, expected ${expected}")
    endif()
endfunction()

# The scene: dark background, a green filled rectangle, and a red line.
check_pixel(10 10 "101820")
check_pixel(150 150 "30A030")
check_pixel(400 260 "F06060")
