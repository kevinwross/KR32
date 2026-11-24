# Rebuilds the two embedded OS images twice with the compiler and compares the
# two runs, then reports how they relate to the copies under src/resources/os.
#
#   cmake -DKRC=<krc.exe> -DROOT=<kr32 tree> -DSCRATCH=<dir> -P CheckOsImages.cmake
#
# Why twice rather than against src/resources/os yet: the committed images are the
# last output of the retired kr32 toolchain, and the compiler's rebuild of the
# kernel does not boot -- kr32's own run_os_fixture row fails against it. Replacing
# them would trade one failing row for another. Until the rebuild boots, this row
# holds the compiler's output to being reproducible and prints the hashes so the
# gap to the committed images is visible rather than assumed.
if(NOT DEFINED KRC OR NOT DEFINED ROOT OR NOT DEFINED SCRATCH)
    message(FATAL_ERROR "KRC, ROOT and SCRATCH are required")
endif()
if(NOT EXISTS "${KRC}")
    message(FATAL_ERROR "krc was not found at ${KRC}")
endif()

function(build_os output)
    file(REMOVE_RECURSE "${output}")
    execute_process(
        COMMAND pwsh -NoProfile -File "${ROOT}/kr32os/build.ps1" -Krc "${KRC}" -Out "${output}"
        WORKING_DIRECTORY "${ROOT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the OS build failed (${result}):\n${out}${err}")
    endif()
endfunction()

build_os("${SCRATCH}/first")
build_os("${SCRATCH}/second")

foreach(image bios.k32 kernel.k32)
    file(SHA256 "${SCRATCH}/first/${image}" first_hash)
    file(SHA256 "${SCRATCH}/second/${image}" second_hash)
    if(NOT first_hash STREQUAL second_hash)
        message(FATAL_ERROR "${image} is not reproducible: two builds differ\n  ${first_hash}\n  ${second_hash}")
    endif()
    file(SHA256 "${ROOT}/src/resources/os/windows/${image}" committed_hash)
    if(first_hash STREQUAL committed_hash)
        message(STATUS "${image}: reproducible and equal to the committed image")
    else()
        message(STATUS "${image}: reproducible, differs from the committed image (${committed_hash})")
    endif()
endforeach()

message(STATUS "the compiler's OS images are reproducible")