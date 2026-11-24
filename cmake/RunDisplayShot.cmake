# Builds the color-bar guest, captures two headless screenshots, and requires
# them to be byte-identical. This is the D2 golden-frame gate.
if(NOT DEFINED KR32 OR NOT DEFINED EXAMPLES OR NOT DEFINED WORK)
    message(FATAL_ERROR "KR32, EXAMPLES and WORK are required")
endif()

file(MAKE_DIRECTORY "${WORK}")
set(image "${WORK}/color-bars.kr32")

execute_process(
    COMMAND "${KR32}" build "${EXAMPLES}/color-bars.asm" -o "${image}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "building the color-bar guest failed (${result}): ${build_output}${build_error}")
endif()

foreach(shot first second)
    execute_process(
        COMMAND "${KR32}" display "${image}" --screen-shot "${WORK}/${shot}.ppm"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE shot_output
        ERROR_VARIABLE shot_error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "screen shot ${shot} failed (${result}): ${shot_output}${shot_error}")
    endif()
endforeach()

file(READ "${WORK}/first.ppm" first HEX)
file(READ "${WORK}/second.ppm" second HEX)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "screen shots differ between identical runs")
endif()

file(SIZE "${WORK}/first.ppm" size)
if(size LESS 192000)
    message(FATAL_ERROR "screen shot is truncated: ${size} bytes")
endif()

message(STATUS "display screen shot stable (${size} bytes)")
