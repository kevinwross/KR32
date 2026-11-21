cmake_minimum_required(VERSION 3.25)

project(KR32 VERSION 0.3.0 LANGUAGES CXX)

option(KR32_BUILD_UI "Build the SDL2 windowed debugger" ON)
option(KR32_BUILD_TESTS "Build tests" ON)
option(KR32_EMBED_OS "Embed the default KR32OS BIOS and kernel" ON)
option(KR32_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(KR32_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# The installed krc package is the compiler: the window's BUILD and the test
# images both use it, and the package owns the target sysroot and runtime.
find_package(krc-toolchain CONFIG REQUIRED)
if(NOT KRC_SYSROOT)
    message(FATAL_ERROR
        "krc-toolchain did not export KRC_SYSROOT. The package has no target "
        "sysroot; install a KRCompiler built with KRC_BUILD_TOOLS=ON.")
endif()
if(NOT KRC_KR32_RUNTIME_DIR)
    message(FATAL_ERROR
        "krc-toolchain did not export KRC_KR32_RUNTIME_DIR. The package has "
        "no KR32 runtime directory.")
endif()
get_target_property(KR32_KRC_PACKAGE_INCLUDES krc::toolchain
    INTERFACE_INCLUDE_DIRECTORIES)
set(_kr32_krc_header "")
foreach(_kr32_krc_directory IN LISTS KR32_KRC_PACKAGE_INCLUDES)
    if(EXISTS "${_kr32_krc_directory}/krc/toolchain.h")
        set(_kr32_krc_header "${_kr32_krc_directory}/krc/toolchain.h")
    endif()
endforeach()
if(NOT _kr32_krc_header)
    message(FATAL_ERROR
        "krc-toolchain was found, but its public header is missing: "
        "krc/toolchain.h was not under its INTERFACE_INCLUDE_DIRECTORIES "
        "('${KR32_KRC_PACKAGE_INCLUDES}')")
endif()
foreach(_kr32_krc_sysroot_file IN ITEMS
        include/stdio.h
        include/stdint.h
        kr32/include/bits/stdint.h
        kr32/include/bits/types.h)
    if(NOT EXISTS "${KRC_SYSROOT}/${_kr32_krc_sysroot_file}")
        message(FATAL_ERROR
            "KRC_SYSROOT ('${KRC_SYSROOT}') is missing "
            "'${_kr32_krc_sysroot_file}'")
    endif()
endforeach()
foreach(_kr32_krc_runtime_file IN ITEMS libkrcrt.kro crt0.s fileio.s eh.s)
    if(NOT EXISTS "${KRC_KR32_RUNTIME_DIR}/${_kr32_krc_runtime_file}")
        message(FATAL_ERROR
            "KRC_KR32_RUNTIME_DIR ('${KRC_KR32_RUNTIME_DIR}') is missing "
            "'${_kr32_krc_runtime_file}'")
    endif()
endforeach()
message(STATUS "KR32: KRC_SYSROOT=${KRC_SYSROOT}")
message(STATUS "KR32: KRC_KR32_RUNTIME_DIR=${KRC_KR32_RUNTIME_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_library(kr32_warnings INTERFACE)
if(MSVC)
    target_compile_options(kr32_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
    if(KR32_WARNINGS_AS_ERRORS)
        target_compile_options(kr32_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(kr32_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(KR32_WARNINGS_AS_ERRORS)
        target_compile_options(kr32_warnings INTERFACE -Werror)
    endif()
endif()

if(KR32_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "KR32_SANITIZE is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

function(kr32_add_library name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${name} PRIVATE kr32_warnings)
    target_compile_features(${name} PUBLIC cxx_std_20)
endfunction()

kr32_add_library(kr32_core src/core/encoding.cpp src/core/machine.cpp src/core/devices.cpp)

kr32_add_library(kr32_container src/container/container.cpp)
target_link_libraries(kr32_container PUBLIC kr32_core)

kr32_add_library(kr32_dis src/dis/disassembler.cpp)
target_link_libraries(kr32_dis PUBLIC kr32_core kr32_container)

kr32_add_library(kr32_host src/vm/host.cpp)
target_link_libraries(kr32_host PUBLIC kr32_core)

kr32_add_library(kr32_debug src/debug/debug.cpp)
target_link_libraries(kr32_debug PUBLIC kr32_core kr32_container kr32_host)

kr32_add_library(kr32_vm src/vm/vm.cpp src/vm/threads.cpp)
target_link_libraries(kr32_vm PUBLIC kr32_core kr32_host kr32_container)

kr32_add_library(kr32_dap src/dap/dap.cpp)
target_link_libraries(kr32_dap PUBLIC kr32_debug kr32_vm)
if(WIN32)
    target_link_libraries(kr32_dap PUBLIC ws2_32)
endif()

if(WIN32)
    set(KR32_OS_PLATFORM "windows")
elseif(APPLE)
    set(KR32_OS_PLATFORM "macos")
else()
    set(KR32_OS_PLATFORM "linux")
endif()
set(KR32_OS_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/os/${KR32_OS_PLATFORM}")
if(KR32_EMBED_OS AND NOT EXISTS "${KR32_OS_RESOURCE_DIR}/kernel.k32")
    message(FATAL_ERROR
        "No embedded KR32OS images for ${KR32_OS_PLATFORM}: "
        "${KR32_OS_RESOURCE_DIR}/kernel.k32 is missing. "
        "Run scripts/update-os.ps1 or scripts/update-os.sh.")
endif()
set(KR32_EMBEDDED_OS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/os")
set(KR32_EMBEDDED_OS_SOURCE "${KR32_EMBEDDED_OS_DIR}/embedded_os.cpp")
file(MAKE_DIRECTORY "${KR32_EMBEDDED_OS_DIR}")

add_executable(kr32_embed_os tools/embed_os/main.cpp)
target_compile_features(kr32_embed_os PRIVATE cxx_std_20)

# Host-side generator for the kernel's 8x16 console font. Run it by hand and
# commit kr32os/kernel/font8x16.c; the kernel build stays freestanding.
add_executable(kr32_font8x16 tools/font8x16/main.cpp)
target_include_directories(kr32_font8x16 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
target_compile_features(kr32_font8x16 PRIVATE cxx_std_20)
target_link_libraries(kr32_font8x16 PRIVATE kr32_warnings)

set(KR32_EMBED_OS_ARGS --output "${KR32_EMBEDDED_OS_SOURCE}")
set(KR32_EMBED_OS_DEPENDS kr32_embed_os)
if(KR32_EMBED_OS)
    list(APPEND KR32_EMBED_OS_ARGS
        --bios "${KR32_OS_RESOURCE_DIR}/bios.k32"
        --kernel "${KR32_OS_RESOURCE_DIR}/kernel.k32")
    list(APPEND KR32_EMBED_OS_DEPENDS
        "${KR32_OS_RESOURCE_DIR}/bios.k32"
        "${KR32_OS_RESOURCE_DIR}/kernel.k32")
else()
    list(APPEND KR32_EMBED_OS_ARGS --empty)
endif()
add_custom_command(
    OUTPUT "${KR32_EMBEDDED_OS_SOURCE}"
    COMMAND kr32_embed_os ${KR32_EMBED_OS_ARGS}
    DEPENDS ${KR32_EMBED_OS_DEPENDS}
    COMMENT "Generating embedded KR32OS images"
    VERBATIM)
kr32_add_library(kr32_os_images "${KR32_EMBEDDED_OS_SOURCE}")

# The guest console header is tracked as include/kr32/cons.h. `con.h` was a
# reserved DOS device name and needed a generator; `cons.h` does not.

# Test images for the C/C++ examples are built through the installed krc
# package, not through `kr32 build`, so the examples still have something to run
# when 3.10b removes the built-in compiler commands.
if(KR32_BUILD_TESTS)
    add_executable(kr32_krc_image tools/krc_image/main.cpp)
    target_link_libraries(kr32_krc_image PRIVATE krc::toolchain kr32_warnings)
    target_compile_features(kr32_krc_image PRIVATE cxx_std_20)
    target_compile_definitions(kr32_krc_image PRIVATE
        KRC_SYSROOT="${KRC_SYSROOT}"
        KRC_KR32_RUNTIME_DIR="${KRC_KR32_RUNTIME_DIR}"
        KR32_GUEST_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/include")
    set(KR32_TEST_IMAGE_DIR "${CMAKE_BINARY_DIR}/test-images")
    file(MAKE_DIRECTORY "${KR32_TEST_IMAGE_DIR}")
    add_custom_target(kr32_test_images ALL
        COMMENT "Building the krc test images")
    function(kr32_add_test_image name source cxx)
        set(image "${KR32_TEST_IMAGE_DIR}/${name}.k32")
        set(arguments --source "${source}" --output "${image}")
        if(cxx)
            list(APPEND arguments --cxx)
        endif()
        add_custom_command(
            OUTPUT "${image}"
            COMMAND kr32_krc_image ${arguments}
            DEPENDS kr32_krc_image "${source}"
            COMMENT "Building test image ${name}.k32"
            VERBATIM)
        add_custom_target("${name}_image" DEPENDS "${image}")
        add_dependencies(kr32_test_images "${name}_image")
    endfunction()
endif()

if(KR32_BUILD_UI)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        set(KR32_SDL2_TARGET SDL2::SDL2)
    elseif(SDL2_FOUND AND TARGET SDL2::SDL2-static)
        set(KR32_SDL2_TARGET SDL2::SDL2-static)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(KR32_SDL2_TARGET PkgConfig::SDL2)
    endif()
    message(STATUS "KR32: SDL2 target ${KR32_SDL2_TARGET}")

    kr32_add_library(kr32_platform src/platform/sdl2/platform.cpp)
    target_compile_definitions(kr32_platform PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(kr32_platform PUBLIC ${KR32_SDL2_TARGET})
    if(WIN32)
        target_link_libraries(kr32_platform PUBLIC comdlg32)
    endif()

    kr32_add_library(kr32_ui src/ui/app.cpp src/ui/canvas.cpp src/ui/debugger_ui.cpp
        src/ui/help.cpp src/resources/embedded_fonts.cpp)
    target_include_directories(kr32_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    target_link_libraries(kr32_ui PUBLIC kr32_platform kr32_debug kr32_dis kr32_container
        kr32_core kr32_os_images)
    file(TO_CMAKE_PATH "${KRC_SYSROOT}" KR32_KRC_SYSROOT_PATH)
    file(TO_CMAKE_PATH "${KRC_KR32_RUNTIME_DIR}" KR32_KRC_RUNTIME_PATH)
    target_link_libraries(kr32_ui PRIVATE krc::toolchain)
    target_compile_definitions(kr32_ui PRIVATE
        KR32_KRC_SYSROOT_PATH="${KR32_KRC_SYSROOT_PATH}"
        KR32_KRC_RUNTIME_PATH="${KR32_KRC_RUNTIME_PATH}"
        KR32_GUEST_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

kr32_add_library(kr32_cli src/cli/cli.cpp)
target_link_libraries(kr32_cli PUBLIC
    kr32_core
    kr32_container
    kr32_dis
    kr32_debug
    kr32_vm
    kr32_dap
    kr32_os_images)

if(KR32_BUILD_UI)
    target_link_libraries(kr32_cli PUBLIC kr32_ui)
    target_compile_definitions(kr32_cli PUBLIC KR32_HAS_UI=1)
endif()

add_executable(kr32 src/main.cpp)
target_link_libraries(kr32 PRIVATE kr32_cli kr32_warnings)
target_compile_features(kr32 PRIVATE cxx_std_20)

if(WIN32 AND KR32_BUILD_UI AND TARGET SDL2::SDL2)
    add_custom_command(TARGET kr32 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:kr32>"
        COMMENT "Copying SDL2.dll next to kr32")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DKR32_SCAN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake
    RESULT_VARIABLE _kr32_boundary_result)
if(NOT _kr32_boundary_result EQUAL 0)
    message(FATAL_ERROR "Platform boundary check failed")
endif()

if(KR32_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS kr32 RUNTIME DESTINATION bin)
install(FILES
    LICENSE
    docs/release-note-0.3.0.md
    docs/release-note-both-toolchains.md
    docs/cxx-gap-signoff.md
    third_party/fonts/OFL-Inter.txt
    third_party/fonts/OFL-JetBrainsMono.txt
    DESTINATION .)

set(CPACK_PACKAGE_NAME "KR32")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "KR32")
set(CPACK_PACKAGE_FILE_NAME "KR32-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
