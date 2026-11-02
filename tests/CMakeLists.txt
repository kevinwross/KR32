add_executable(kr32_tests
    unit/test_main.cpp
    unit/test_encoding.cpp
    unit/test_version.cpp
    unit/test_machine.cpp
    unit/test_privilege.cpp
    unit/test_boot.cpp
    unit/test_fp.cpp
    unit/test_matrix.cpp
    unit/test_devices.cpp
    unit/test_container.cpp
    unit/test_object_assembler.cpp
    unit/test_linker.cpp
    unit/test_cc.cpp
    unit/test_debug.cpp
    unit/test_host.cpp
    unit/test_assembler.cpp
    unit/test_disassembler.cpp
    cxx/test_cxx_abi.cpp
    cxx/test_cxx_api.cpp
    cxx/test_cxx_parser.cpp)

target_link_libraries(kr32_tests PRIVATE
    kr32_core
    kr32_asm
    kr32_dis
    kr32_container
    kr32_link
    kr32_cc
    kr32_cxx
    kr32_debug
    kr32_vm
    kr32_warnings)
target_include_directories(kr32_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party")
target_include_directories(kr32_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(kr32_tests PRIVATE cxx_std_20)

if(TARGET kr32_ui)
    target_sources(kr32_tests PRIVATE unit/test_ui.cpp)
    target_link_libraries(kr32_tests PRIVATE kr32_ui)
endif()

# doctest 2.4.11 includes <ciso646>, which libstdc++ 15 flags with #warning.
# It is a dependency diagnostics issue, not a KR32 warning.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kr32_tests PRIVATE "-Wno-#warnings")
endif()

add_test(NAME unit COMMAND kr32_tests)

if(TARGET kr32_cxx_tool)
    add_test(NAME cxx_tool_help COMMAND kr32_cxx_tool --help)
    set_tests_properties(cxx_tool_help PROPERTIES
        PASS_REGULAR_EXPRESSION "kr32-cxx")

    # Driver error paths are part of the harness contract: a bad invocation must
    # exit non-zero and must not try to compile the input.
    add_test(NAME cxx_tool_missing_input COMMAND kr32_cxx_tool)
    set_tests_properties(cxx_tool_missing_input PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cxx_tool_bad_emit COMMAND kr32_cxx_tool --emit elf input.cpp)
    set_tests_properties(cxx_tool_bad_emit PROPERTIES WILL_FAIL TRUE)

    add_test(NAME cxx_tool_missing_output_path COMMAND kr32_cxx_tool -o)
    set_tests_properties(cxx_tool_missing_output_path PROPERTIES WILL_FAIL TRUE)
endif()

add_test(NAME cli_version COMMAND $<TARGET_FILE:kr32> version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "kr32 0\\.1\\.0")

add_test(NAME cli_help COMMAND $<TARGET_FILE:kr32> help)
set_tests_properties(cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage:")

add_test(NAME cli_stub COMMAND $<TARGET_FILE:kr32> build)
set_tests_properties(cli_stub PROPERTIES WILL_FAIL TRUE)

add_test(NAME run_demo
    COMMAND $<TARGET_FILE:kr32> run ${CMAKE_SOURCE_DIR}/examples/demo.asm)
set_tests_properties(run_demo PROPERTIES
    PASS_REGULAR_EXPRESSION "R1=12;R3=30")

add_test(NAME fp_demo
    COMMAND $<TARGET_FILE:kr32> run ${CMAKE_SOURCE_DIR}/examples/fp-demo.asm)
set_tests_properties(fp_demo PROPERTIES PASS_REGULAR_EXPRESSION "R1=3")

add_test(NAME matrix_demo
    COMMAND $<TARGET_FILE:kr32> run ${CMAKE_SOURCE_DIR}/examples/matrix-demo.asm)
set_tests_properties(matrix_demo PROPERTIES PASS_REGULAR_EXPRESSION "R1=256")

add_test(NAME bios_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunBiosSmoke.cmake)

add_test(NAME build_hello
    COMMAND $<TARGET_FILE:kr32> build
        ${CMAKE_SOURCE_DIR}/examples/hello.s
        -o ${CMAKE_BINARY_DIR}/hello.k32 --run)
set_tests_properties(build_hello PROPERTIES PASS_REGULAR_EXPRESSION "HALTED")

add_test(NAME link_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunLinkSmoke.cmake)

add_test(NAME build_c_hello
    COMMAND $<TARGET_FILE:kr32> build
        ${CMAKE_SOURCE_DIR}/examples/hello.c
        -o ${CMAKE_BINARY_DIR}/hello-c.k32 --run)
set_tests_properties(build_c_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello, KR32!")

add_test(NAME debug_dump
    COMMAND $<TARGET_FILE:kr32> debug ${CMAKE_BINARY_DIR}/hello-c.k32)
set_tests_properties(debug_dump PROPERTIES
    DEPENDS build_c_hello
    PASS_REGULAR_EXPRESSION "function main")

add_test(NAME host_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunHostSmoke.cmake)

add_test(NAME dap_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunDapSmoke.cmake)
set_tests_properties(dap_smoke PROPERTIES DEPENDS build_c_hello)

add_test(NAME shell_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunShellSmoke.cmake)

if(KR32_BUILD_UI)
    add_test(NAME ui_smoke COMMAND $<TARGET_FILE:kr32> --smoke-ui)
    set_tests_properties(ui_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

    add_test(NAME ui_headless
        COMMAND $<TARGET_FILE:kr32> gui --headless
            ${CMAKE_BINARY_DIR}/hello-c.k32
            --screenshot ${CMAKE_BINARY_DIR}/hello-ui.ppm)
    set_tests_properties(ui_headless PROPERTIES
        DEPENDS build_c_hello
        PASS_REGULAR_EXPRESSION "ui-render")
endif()

add_test(NAME boundary_clean
    COMMAND ${CMAKE_COMMAND}
        -DKR32_SCAN_DIR=${CMAKE_SOURCE_DIR}/src/ui
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake)

add_test(NAME boundary_bad
    COMMAND ${CMAKE_COMMAND}
        -DKR32_SCAN_DIR=${CMAKE_SOURCE_DIR}/tests/boundary/bad_ui.cpp
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake)
set_tests_properties(boundary_bad PROPERTIES WILL_FAIL TRUE)
