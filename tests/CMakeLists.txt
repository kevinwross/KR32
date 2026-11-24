include(ProcessorCount)
ProcessorCount(KR32_TEST_JOBS)
if(KR32_TEST_JOBS EQUAL 0)
    set(KR32_TEST_JOBS 8)
endif()

set(KR32_TEST_SOURCES
    unit/test_main.cpp
    unit/test_encoding.cpp
    unit/test_golden_vectors.cpp
    unit/test_version.cpp
    unit/test_machine.cpp
    unit/test_privilege.cpp
    unit/test_fp.cpp
    unit/test_matrix.cpp
    unit/test_container.cpp
    unit/test_terminal.cpp
    unit/test_graphics.cpp
    unit/test_guest_headers.cpp
    unit/test_embedded_os.cpp)

add_executable(kr32_tests ${KR32_TEST_SOURCES})

target_link_libraries(kr32_tests PRIVATE
    kr32_core
    kr32_dis
    kr32_container
    kr32_debug
    kr32_vm
    kr32_os_images
    krc::toolchain
    kr32_warnings)
target_compile_definitions(kr32_tests PRIVATE
    KR32_HAS_EMBEDDED_OS=$<BOOL:${KR32_EMBED_OS}>
    KR32_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    KR32_TEST_KRC_SYSROOT="${KRC_SYSROOT}"
    KR32_TEST_KRC_RUNTIME_DIR="${KRC_KR32_RUNTIME_DIR}")
target_include_directories(kr32_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party")
target_include_directories(kr32_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(kr32_tests PRIVATE cxx_std_20)

if(TARGET kr32_ui)
    target_sources(kr32_tests PRIVATE unit/test_ui.cpp)
    target_link_libraries(kr32_tests PRIVATE kr32_ui)
    add_test(NAME unit_test_ui
        COMMAND kr32_tests "--source-file=*test_ui.cpp")
    set_tests_properties(unit_test_ui PROPERTIES
        LABELS "unit"
        # The codegen buckets are CPU-heavy: test_cxx_codegen_2 alone takes about 214 s
# and crosses 300 s when several buckets run in parallel on a busy machine. This
# is a watchdog, not an assertion, so leave room for the slowest bucket.
TIMEOUT 600)
    if(WIN32 AND TARGET SDL2::SDL2)
        add_custom_command(TARGET kr32_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>"
                "$<TARGET_FILE_DIR:kr32_tests>")
    endif()
endif()

# doctest 2.4.11 includes <ciso646>, which libstdc++ 15 flags with #warning.
# It is a dependency diagnostics issue, not a KR32 warning.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kr32_tests PRIVATE "-Wno-#warnings")
endif()

# Register each test translation unit separately so `ctest -j` can run the
# independent suites in parallel. doctest's --source-file filter selects the
# cases compiled from that file; this avoids a monolithic 4-5 minute `unit`.
foreach(test_source IN LISTS KR32_TEST_SOURCES)
    if(test_source STREQUAL "unit/test_main.cpp")
        continue()
    endif()
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_test(NAME "unit_${test_name}"
        COMMAND kr32_tests "--source-file=*${test_name}.cpp")
    set_tests_properties("unit_${test_name}" PROPERTIES
        LABELS "unit"
        # The codegen buckets are CPU-heavy: test_cxx_codegen_2 alone takes about 214 s
# and crosses 300 s when several buckets run in parallel on a busy machine. This
# is a watchdog, not an assertion, so leave room for the slowest bucket.
TIMEOUT 600)
endforeach()

# krc-built images for the examples. No test invokes `kr32 build`; the image is
# produced once at build time by the installed krc package.
kr32_add_test_image(hello_c ${CMAKE_SOURCE_DIR}/examples/hello.c FALSE)
kr32_add_test_image(hello_cxx ${CMAKE_SOURCE_DIR}/examples/hello.cpp TRUE)
kr32_add_test_image(cxx_libc ${CMAKE_SOURCE_DIR}/examples/cxx_libc.cpp TRUE)
kr32_add_test_image(matrix_cxx ${CMAKE_SOURCE_DIR}/examples/matrix.cpp TRUE)
kr32_add_test_image(balanced_tree_cxx ${CMAKE_SOURCE_DIR}/examples/balanced-tree.cpp TRUE)
kr32_add_test_image(cxx_tls ${CMAKE_SOURCE_DIR}/examples/cxx_tls.cpp TRUE)
kr32_add_test_image(types_c ${CMAKE_SOURCE_DIR}/examples/types.c FALSE)
kr32_add_test_image(floats_c ${CMAKE_SOURCE_DIR}/examples/floats.c FALSE)
kr32_add_test_image(varargs_c ${CMAKE_SOURCE_DIR}/examples/varargs.c FALSE)
kr32_add_test_image(libc_c ${CMAKE_SOURCE_DIR}/examples/libc-test.c FALSE)
kr32_add_test_image(strings_c ${CMAKE_SOURCE_DIR}/examples/strings.c FALSE)
kr32_add_test_image(binarytree_c ${CMAKE_SOURCE_DIR}/examples/binarytree.c FALSE)
kr32_add_test_image(fileio_c ${CMAKE_SOURCE_DIR}/examples/fileio.c FALSE)

add_test(NAME cli_version COMMAND $<TARGET_FILE:kr32> version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "kr32 0\\.3\\.0")

add_test(NAME cli_help COMMAND $<TARGET_FILE:kr32> help)
set_tests_properties(cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage:")

add_test(NAME run_c_hello
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/hello_c.k32)
set_tests_properties(run_c_hello PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello, KR32!")

add_test(NAME run_cxx_demo
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/hello_cxx.k32)
set_tests_properties(run_cxx_demo PROPERTIES PASS_REGULAR_EXPRESSION "HALTED exit=42")

add_test(NAME run_cxx_libc
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/cxx_libc.k32)
set_tests_properties(run_cxx_libc PROPERTIES
    PASS_REGULAR_EXPRESSION "C\\+\\+ via C libc: shared libc")

add_test(NAME run_cxx_matrix
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/matrix_cxx.k32)
set_tests_properties(run_cxx_matrix PROPERTIES
    PASS_REGULAR_EXPRESSION "HALTED exit=42")

add_test(NAME run_cxx_balanced_tree
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/balanced_tree_cxx.k32)
set_tests_properties(run_cxx_balanced_tree PROPERTIES
    PASS_REGULAR_EXPRESSION "Engineers in sorted order \\(30\\)")

add_test(NAME run_cxx_tls
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/cxx_tls.k32)
set_tests_properties(run_cxx_tls PROPERTIES
    PASS_REGULAR_EXPRESSION "HALTED exit=42")

add_test(NAME build_c_hello
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/hello_c.k32)
set_tests_properties(build_c_hello PROPERTIES PASS_REGULAR_EXPRESSION "Hello, KR32!")

add_test(NAME build_c_types
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/types_c.k32)
set_tests_properties(build_c_types PROPERTIES
    PASS_REGULAR_EXPRESSION "unsigned int 10 squared = 100")

add_test(NAME run_c_floats_variadic
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/floats_c.k32)
set_tests_properties(run_c_floats_variadic PROPERTIES
    PASS_REGULAR_EXPRESSION "fpCrazyTown result = -20.142857")

add_test(NAME build_c_varargs
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/varargs_c.k32)
set_tests_properties(build_c_varargs PROPERTIES
    PASS_REGULAR_EXPRESSION "mixed varargs 4 \\+ 2.5 \\+ 1.25 = 7")

add_test(NAME build_c_libc
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/libc_c.k32)
set_tests_properties(build_c_libc PROPERTIES PASS_REGULAR_EXPRESSION "heap walk=0")

add_test(NAME build_c_strings
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/strings_c.k32)
set_tests_properties(build_c_strings PROPERTIES
    PASS_REGULAR_EXPRESSION "memmove: ababcd")

add_test(NAME build_c_binarytree
    COMMAND $<TARGET_FILE:kr32> vm ${KR32_TEST_IMAGE_DIR}/binarytree_c.k32)
set_tests_properties(build_c_binarytree PROPERTIES
    PASS_REGULAR_EXPRESSION "AVL tree of engineers \\(height 6, 30 names\\)")

add_test(NAME debug_dump
    COMMAND $<TARGET_FILE:kr32> debug ${KR32_TEST_IMAGE_DIR}/hello_c.k32)
set_tests_properties(debug_dump PROPERTIES
    PASS_REGULAR_EXPRESSION "function main")

add_test(NAME host_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DIMAGE=${KR32_TEST_IMAGE_DIR}/fileio_c.k32
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunHostSmoke.cmake)

add_test(NAME dap_smoke
    COMMAND ${CMAKE_COMMAND}
        -DKR32=$<TARGET_FILE:kr32>
        -DIMAGE=${KR32_TEST_IMAGE_DIR}/hello_c.k32
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/RunDapSmoke.cmake)

if(KR32_EMBED_OS)
    add_test(NAME run_os_embedded
        COMMAND ${CMAKE_COMMAND}
            -DKR32=$<TARGET_FILE:kr32>
            -DBINARY_DIR=${CMAKE_BINARY_DIR} -DSKIP_BUILD=1
            -P ${CMAKE_SOURCE_DIR}/cmake/RunOsEmbedded.cmake)
endif()

# The OS images are the shipped prebuilt pair, not a build of kr32os.
if(WIN32)
    set(KR32_OS_FIXTURE_DIR "${CMAKE_SOURCE_DIR}/src/resources/os/windows")
else()
    set(KR32_OS_FIXTURE_DIR "${CMAKE_SOURCE_DIR}/src/resources/os/linux")
endif()
add_test(NAME run_os_fixture
    COMMAND $<TARGET_FILE:kr32> os
            "${KR32_OS_FIXTURE_DIR}/kernel.k32"
            --bios "${KR32_OS_FIXTURE_DIR}/bios.k32"
            --max-steps 12000000 --serial-input "exit 0\n")
set_tests_properties(run_os_fixture PROPERTIES
    LABELS "integration"
    PASS_REGULAR_EXPRESSION "HALTED exit=0")

add_test(NAME boundary_clean
    COMMAND ${CMAKE_COMMAND}
        -DKR32_SCAN_DIR=${CMAKE_SOURCE_DIR}/src/ui
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake)

add_test(NAME boundary_bad
    COMMAND ${CMAKE_COMMAND}
        -DKR32_SCAN_DIR=${CMAKE_SOURCE_DIR}/tests/boundary/bad_ui.cpp
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake)
set_tests_properties(boundary_bad PROPERTIES WILL_FAIL TRUE)

if(KR32_BUILD_UI)
    add_test(NAME ui_smoke COMMAND $<TARGET_FILE:kr32> --smoke-ui)
    set_tests_properties(ui_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

    add_test(NAME ui_headless
        COMMAND $<TARGET_FILE:kr32> gui --headless
            ${KR32_TEST_IMAGE_DIR}/hello_c.k32
            --screenshot ${CMAKE_BINARY_DIR}/hello-ui.ppm)
    set_tests_properties(ui_headless PROPERTIES
        PASS_REGULAR_EXPRESSION "ui-render")

    add_test(NAME ui_headless_cxx
        COMMAND $<TARGET_FILE:kr32> gui --headless
            ${CMAKE_SOURCE_DIR}/examples/hello.cpp
            --screenshot ${CMAKE_BINARY_DIR}/hello-cxx-ui.ppm)
    set_tests_properties(ui_headless_cxx PROPERTIES
        PASS_REGULAR_EXPRESSION "ui-render")

    add_test(NAME ui_headless_os
        COMMAND $<TARGET_FILE:kr32> gui --os --embedded --headless
            --screenshot ${CMAKE_BINARY_DIR}/os-ui.ppm)
    set_tests_properties(ui_headless_os PROPERTIES
        PASS_REGULAR_EXPRESSION "ui-render")

    add_test(NAME ui_headless_os_files
        COMMAND $<TARGET_FILE:kr32> gui --os
            "${KR32_OS_FIXTURE_DIR}/kernel.k32"
            --bios "${KR32_OS_FIXTURE_DIR}/bios.k32" --headless
            --screenshot ${CMAKE_BINARY_DIR}/os-ui-files.ppm)
    set_tests_properties(ui_headless_os_files PROPERTIES
        PASS_REGULAR_EXPRESSION "ui-render")
endif()
# Convenience targets:
#   check-unit : only the doctest suites, parallel
#   check      : the whole CTest suite, parallel
add_custom_target(check-unit
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -C $<CONFIG> -L unit --output-on-failure -j${KR32_TEST_JOBS}
    DEPENDS kr32_tests
    USES_TERMINAL)
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -C $<CONFIG> --output-on-failure -j${KR32_TEST_JOBS}
    DEPENDS kr32 kr32_tests
    USES_TERMINAL)
# The embedded OS images are build output: this row rebuilds them with the
# compiler and compares, so a hand edit or an address-moving change fails here.
# Registered only when the gate names a compiler, because a row that silently
# vanishes is worse than one that says it was not asked for.
if(KR32_BUILD_OS_IMAGES)
    add_test(NAME kr32_os_images_match_source
        COMMAND ${CMAKE_COMMAND}
            -DKRC=${KRG_KRC}
            -DROOT=${CMAKE_SOURCE_DIR}
            -DSCRATCH=${CMAKE_BINARY_DIR}/os-images
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckOsImages.cmake)
endif()
