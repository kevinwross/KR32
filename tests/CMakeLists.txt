add_executable(kr32_tests
    unit/test_main.cpp
    unit/test_encoding.cpp
    unit/test_version.cpp
    unit/test_machine.cpp
    unit/test_assembler.cpp
    unit/test_disassembler.cpp)

target_link_libraries(kr32_tests PRIVATE
    kr32_core
    kr32_asm
    kr32_dis
    kr32_warnings)
target_include_directories(kr32_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party")
target_compile_features(kr32_tests PRIVATE cxx_std_20)

# doctest 2.4.11 includes <ciso646>, which libstdc++ 15 flags with #warning.
# It is a dependency diagnostics issue, not a KR32 warning.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kr32_tests PRIVATE "-Wno-#warnings")
endif()

add_test(NAME unit COMMAND kr32_tests)

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

if(KR32_BUILD_UI)
    add_test(NAME ui_smoke COMMAND $<TARGET_FILE:kr32> --smoke-ui)
    set_tests_properties(ui_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")
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
