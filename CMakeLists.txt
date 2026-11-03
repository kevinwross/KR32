cmake_minimum_required(VERSION 3.25)

project(KR32 VERSION 0.1.0 LANGUAGES CXX)

option(KR32_BUILD_UI "Build the SDL2 windowed debugger" ON)
option(KR32_BUILD_TESTS "Build tests" ON)
option(KR32_BUILD_CXX_TOOL "Build the standalone kr32-cxx C++ driver" ON)
option(KR32_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(KR32_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

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

kr32_add_library(kr32_asm src/asm/assembler.cpp src/asm/object_assembler.cpp)
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

kr32_add_library(kr32_debug src/debug/debug.cpp)
target_link_libraries(kr32_debug PUBLIC kr32_core kr32_container)

kr32_add_library(kr32_vm src/vm/vm.cpp src/vm/host.cpp src/vm/threads.cpp)
target_link_libraries(kr32_vm PUBLIC kr32_core)

kr32_add_library(kr32_dap src/dap/dap.cpp)
target_link_libraries(kr32_dap PUBLIC kr32_debug kr32_vm)
if(WIN32)
    target_link_libraries(kr32_dap PUBLIC ws2_32)
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
        kr32_core kr32_cc kr32_asm kr32_link)
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
    kr32_dap)

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
if(KR32_BUILD_CXX_TOOL)
    install(TARGETS kr32_cxx_tool RUNTIME DESTINATION bin)
endif()

set(CPACK_PACKAGE_NAME "KR32")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "KR32")
set(CPACK_GENERATOR "ZIP")
include(CPack)
