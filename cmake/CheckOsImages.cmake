# Rebuilds the two embedded OS images with the compiler and compares them with the
# copies under src/resources/os, so a hand edit to a committed image fails the row.
#
#   cmake -DKRC=<krc.exe> -DROOT=<kr32 tree> -DSCRATCH=<dir> [-DOS_DIR=<dir>] -P CheckOsImages.cmake
#
# The images are built twice as well: a build that is not reproducible cannot be
# compared with a committed copy in a way a reader can trust, so the row says which
# of the two properties failed.
if(NOT DEFINED KRC OR NOT DEFINED ROOT OR NOT DEFINED SCRATCH)
    message(FATAL_ERROR "KRC, ROOT and SCRATCH are required")
endif()
if(NOT EXISTS "${KRC}")
    message(FATAL_ERROR "krc was not found at ${KRC}")
endif()
if(NOT DEFINED OS_DIR)
    set(OS_DIR "${ROOT}/src/resources/os/windows")
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

set(failures 0)
foreach(image bios.kr32 kernel.kr32)
    file(SHA256 "${SCRATCH}/first/${image}" first_hash)
    file(SHA256 "${SCRATCH}/second/${image}" second_hash)
    if(NOT first_hash STREQUAL second_hash)
        message(SEND_ERROR "${image} is not reproducible: two builds differ
  ${first_hash}
  ${second_hash}")
        math(EXPR failures "${failures} + 1")
        continue()
    endif()
    if(NOT EXISTS "${OS_DIR}/${image}")
        message(SEND_ERROR "the committed image ${OS_DIR}/${image} is missing")
        math(EXPR failures "${failures} + 1")
        continue()
    endif()
    file(SHA256 "${OS_DIR}/${image}" committed_hash)
    if(NOT first_hash STREQUAL committed_hash)
        file(SIZE "${SCRATCH}/first/${image}" built_size)
        file(SIZE "${OS_DIR}/${image}" committed_size)
        message(SEND_ERROR "${image} differs from the committed image
  rebuilt   ${built_size} bytes ${first_hash}
  committed ${committed_size} bytes ${committed_hash}")
        math(EXPR failures "${failures} + 1")
    endif()
endforeach()

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} of 2 OS images failed their checks")
endif()
message(STATUS "the compiler's OS images are reproducible and equal to the committed ones")
