if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "KR32, SOURCE_DIR, and BINARY_DIR are required")
endif()

set(_rom "${BINARY_DIR}/boot-halt.bin")
execute_process(
    COMMAND "${KR32}" as "${SOURCE_DIR}/examples/boot-halt.asm"
            -o "${_rom}" --base 0xFFFF0000
    RESULT_VARIABLE _assemble_result)
if(NOT _assemble_result EQUAL 0)
    message(FATAL_ERROR "boot-halt assembly failed")
endif()

execute_process(
    COMMAND "${KR32}" vm "${SOURCE_DIR}/examples/demo.asm" --bios "${_rom}"
    OUTPUT_VARIABLE _output
    RESULT_VARIABLE _run_result)
if(NOT _run_result EQUAL 0)
    message(FATAL_ERROR "BIOS run failed: ${_output}")
endif()
if(NOT _output MATCHES "HALTED")
    message(FATAL_ERROR "BIOS run did not halt: ${_output}")
endif()
