cmake_minimum_required(VERSION 3.25)

project(KR32 VERSION 0.2.0 LANGUAGES CXX)

option(KR32_BUILD_UI "Build the SDL2 windowed debugger" ON)
option(KR32_BUILD_TESTS "Build tests" ON)
option(KR32_BUILD_CXX_TOOL "Build the standalone kr32-cxx C++ driver" ON)
option(KR32_EMBED_OS "Embed the default KR32OS BIOS and kernel" ON)
option(KR32_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(KR32_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Which compiler the window's BUILD uses for a .c or .cpp source. `builtin` is
# the in-tree C and C++ front end that ships in this repository; `krc` links the
# published krc::toolchain package instead. Only the krc branch calls
# find_package, so a default build needs nothing outside the tree.
set(KR32_TOOLCHAIN "builtin" CACHE STRING "Toolchain for the window's BUILD: builtin or krc")
set_property(CACHE KR32_TOOLCHAIN PROPERTY STRINGS builtin krc)
if(NOT KR32_TOOLCHAIN STREQUAL "builtin" AND NOT KR32_TOOLCHAIN STREQUAL "krc")
    message(FATAL_ERROR
        "KR32_TOOLCHAIN must be builtin or krc, not '${KR32_TOOLCHAIN}'")
endif()

# The tests build their fixture images through the installed krc package in both
# toolchain configurations, so the package is required whenever tests are built.
if(KR32_TOOLCHAIN STREQUAL "krc" OR KR32_BUILD_TESTS)
    # The installed package owns the target sysroot and runtime and exports both
    # paths, so a prefix is the whole external input and no KRCompiler build tree
    # is needed. Each required file is checked here: an incompletely installed
    # package fails at configure, not at link time or at a later GUI BUILD.
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
                "KR32_TOOLCHAIN=krc: KRC_SYSROOT ('${KRC_SYSROOT}') is missing "
                "'${_kr32_krc_sysroot_file}'")
        endif()
    endforeach()
    foreach(_kr32_krc_runtime_file IN ITEMS libkrcrt.kro crt0.s fileio.s eh.s)
        if(NOT EXISTS "${KRC_KR32_RUNTIME_DIR}/${_kr32_krc_runtime_file}")
            message(FATAL_ERROR
                "KR32_TOOLCHAIN=krc: KRC_KR32_RUNTIME_DIR "
                "('${KRC_KR32_RUNTIME_DIR}') is missing "
                "'${_kr32_krc_runtime_file}'")
        endif()
    endforeach()
    message(STATUS "KR32: KRC_SYSROOT=${KRC_SYSROOT}")
    message(STATUS "KR32: KRC_KR32_RUNTIME_DIR=${KRC_KR32_RUNTIME_DIR}")
endif()

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

kr32_add_library(kr32_asm src/asm/assembler.cpp src/asm/object_assembler.cpp
                         src/asm/optimizer.cpp)
target_link_libraries(kr32_asm PUBLIC kr32_core kr32_container)

kr32_add_library(kr32_container src/container/container.cpp)
target_link_libraries(kr32_container PUBLIC kr32_core)

kr32_add_library(kr32_link src/link/linker.cpp)
target_link_libraries(kr32_link PUBLIC kr32_core kr32_container)

kr32_add_library(kr32_cc src/cc/cc.cpp)
target_link_libraries(kr32_cc PUBLIC kr32_asm kr32_container)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/cc/libc.c" KR32_LIBC_SOURCE)
set(KR32_LIBC_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${KR32_LIBC_GENERATED_DIR}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/cc/libc_runtime.h.in"
               "${KR32_LIBC_GENERATED_DIR}/libc_runtime.h" @ONLY)
target_include_directories(kr32_cc PRIVATE "${KR32_LIBC_GENERATED_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/src/cc/libc.c")

kr32_add_library(kr32_cxx
    src/cxx/cxx.cpp
    src/cxx/ast.cpp
    src/cxx/lexer.cpp
    src/cxx/preprocessor.cpp
    src/cxx/parser.cpp
    src/cxx/codegen.cpp)
target_link_libraries(kr32_cxx PUBLIC kr32_asm kr32_container)
target_include_directories(kr32_cxx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(KR32_BUILD_CXX_TOOL)
    add_executable(kr32_cxx_tool src/cxx/tool/main.cpp)
    target_link_libraries(kr32_cxx_tool PRIVATE kr32_cxx kr32_warnings)
    target_compile_features(kr32_cxx_tool PRIVATE cxx_std_20)
    set_target_properties(kr32_cxx_tool PROPERTIES OUTPUT_NAME kr32-cxx)
endif()

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
        kr32_core kr32_cc kr32_cxx kr32_asm kr32_link kr32_os_images)
    if(KR32_TOOLCHAIN STREQUAL "krc")
        file(TO_CMAKE_PATH "${KRC_SYSROOT}" KR32_KRC_SYSROOT_PATH)
        file(TO_CMAKE_PATH "${KRC_KR32_RUNTIME_DIR}" KR32_KRC_RUNTIME_PATH)
        target_link_libraries(kr32_ui PRIVATE krc::toolchain)
        target_compile_definitions(kr32_ui PRIVATE
            KR32_TOOLCHAIN_KRC=1
            KR32_KRC_SYSROOT_PATH="${KR32_KRC_SYSROOT_PATH}"
            KR32_KRC_RUNTIME_PATH="${KR32_KRC_RUNTIME_PATH}"
            KR32_GUEST_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
endif()

kr32_add_library(kr32_cli src/cli/cli.cpp)
target_link_libraries(kr32_cli PUBLIC
    kr32_core
    kr32_asm
    kr32_link
    kr32_container
    kr32_cc
    kr32_cxx
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
    docs/release-note-both-toolchains.md
    docs/cxx-gap-signoff.md
    third_party/fonts/OFL-Inter.txt
    third_party/fonts/OFL-JetBrainsMono.txt
    DESTINATION .)
if(KR32_BUILD_CXX_TOOL)
    install(TARGETS kr32_cxx_tool RUNTIME DESTINATION bin)
endif()

set(CPACK_PACKAGE_NAME "KR32")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "KR32")
set(CPACK_PACKAGE_FILE_NAME "KR32-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
