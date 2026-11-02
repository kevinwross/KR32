if(NOT DEFINED KR32 OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "KR32, SOURCE_DIR, and BINARY_DIR are required")
endif()

set(_main "${BINARY_DIR}/link-main.o")
set(_func "${BINARY_DIR}/link-func.o")
set(_image "${BINARY_DIR}/link-smoke.k32")

foreach(_pair "examples/link-main.s;${_main}" "examples/link-func.s;${_func}")
    list(GET _pair 0 _source)
    list(GET _pair 1 _output)
    execute_process(
        COMMAND "${KR32}" as "${SOURCE_DIR}/${_source}" --object -o "${_output}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "object assembly failed: ${_source}")
    endif()
endforeach()

execute_process(
    COMMAND "${KR32}" ld "${_main}" "${_func}" -o "${_image}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "link failed")
endif()

execute_process(
    COMMAND "${KR32}" vm "${_image}"
    OUTPUT_VARIABLE _output
    RESULT_VARIABLE _result)
if(NOT _output MATCHES "R0=7")
    message(FATAL_ERROR "linked image did not return 7: ${_output}")
endif()
if(NOT _result EQUAL 0 AND NOT _result EQUAL 7)
    message(FATAL_ERROR "linked image failed with ${_result}: ${_output}")
endif()
