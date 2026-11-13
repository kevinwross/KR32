# Boots the embedded KR32OS under the display command, captures the committed
# frame twice, and requires an 800x600 screenshot that is identical run to run.
# This is the automated form of the D3 console gate.
if(NOT DEFINED KR32 OR NOT DEFINED WORK)
    message(FATAL_ERROR "KR32 and WORK are required")
endif()

file(MAKE_DIRECTORY "${WORK}")

foreach(shot first second)
    execute_process(
        COMMAND "${KR32}" display --os --embedded
                --screen-shot "${WORK}/${shot}.ppm" --steps 12000000
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "console screen shot ${shot} failed (${result}): ${output}${error}")
    endif()
endforeach()

file(SIZE "${WORK}/first.ppm" size)
if(NOT size EQUAL 1440015)
    message(FATAL_ERROR "expected an 800x600 P6 image (1440015 bytes), got ${size}")
endif()

file(READ "${WORK}/first.ppm" header LIMIT 15)
if(NOT header STREQUAL "P6\n800 600\n255\n")
    message(FATAL_ERROR "unexpected PPM header: ${header}")
endif()

file(READ "${WORK}/first.ppm" first HEX)
file(READ "${WORK}/second.ppm" second HEX)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "console screen shots differ between identical runs")
endif()

message(STATUS "console screen shot stable (${size} bytes)")
