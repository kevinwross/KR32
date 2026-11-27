# Rebuilds the two embedded OS images with the compiler and compares them with the
# copy the build embedded, so a hand edit to the image cannot pass the row.
#
#   cmake -DKRC=<krc.exe> -DROOT=<kr32 tree> -DSCRATCH=<dir> [-DOS_DIR=<dir>] -P CheckOsImages.cmake
#
# The images are built twice as well: a build that is not reproducible cannot be
# compared with the copy the build embedded in a way a reader can trust, so the row says which
# of the two properties failed. Windows drives kr32os/build.ps1, other hosts drive
# kr32os/build.sh, because the two hosts carry their own images -- the compiler
# embeds source paths in the debug sections.
if(NOT DEFINED KRC OR NOT DEFINED ROOT OR NOT DEFINED SCRATCH)
    message(FATAL_ERROR "KRC, ROOT and SCRATCH are required")
endif()
if(NOT EXISTS "${KRC}")
    message(FATAL_ERROR "krc was not found at ${KRC}")
endif()
if(NOT DEFINED OS_DIR)
    if(WIN32)
        set(OS_DIR "${ROOT}/src/resources/os/windows")
    else()
        set(OS_DIR "${ROOT}/src/resources/os/linux")
    endif()
endif()

function(build_os output)
    file(REMOVE_RECURSE "${output}")
    if(WIN32)
        find_program(SHELL_EXE NAMES pwsh powershell)
        if(NOT SHELL_EXE)
            message(FATAL_ERROR "no PowerShell found to run kr32os/build.ps1")
        endif()
        execute_process(
            COMMAND "${SHELL_EXE}" -NoProfile -File "${ROOT}/kr32os/build.ps1"
                    -Krc "${KRC}" -Out "${output}"
            WORKING_DIRECTORY "${ROOT}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE out
            ERROR_VARIABLE err)
    else()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "KRC=${KRC}" "KR32OS_BUILD_DIR=${output}"
                    bash "${ROOT}/kr32os/build.sh"
            WORKING_DIRECTORY "${ROOT}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE out
            ERROR_VARIABLE err)
    endif()
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
        message(SEND_ERROR "the image the build embedded, ${OS_DIR}/${image}, is missing")
        math(EXPR failures "${failures} + 1")
        continue()
    endif()
    file(SHA256 "${OS_DIR}/${image}" embedded_hash)
    if(NOT first_hash STREQUAL embedded_hash)
        file(SIZE "${SCRATCH}/first/${image}" built_size)
        file(SIZE "${OS_DIR}/${image}" embedded_size)
        message(SEND_ERROR "${image} differs from the image the build embedded
  rebuilt   ${built_size} bytes ${first_hash}
  embedded  ${embedded_size} bytes ${embedded_hash}")
        math(EXPR failures "${failures} + 1")
    endif()
endforeach()

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} of 2 OS images failed their checks")
endif()
message(STATUS "the compiler's OS images are reproducible and equal to the ones the build embedded")
