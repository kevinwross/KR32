if(NOT KR32 OR NOT BINARY_DIR)
    message(FATAL_ERROR "kr32 and BINARY_DIR are required")
endif()

set(image "${BINARY_DIR}/hello-c.k32")
if(NOT EXISTS "${image}")
    message(FATAL_ERROR "missing ${image}; run build_c_hello first")
endif()

set(requests
"{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{}}"
"{\"seq\":2,\"type\":\"request\",\"command\":\"launch\",\"arguments\":{}}"
"{\"seq\":3,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"source\":{\"path\":\"examples/hello.c\"},\"breakpoints\":[{\"line\":5}]}}"
"{\"seq\":4,\"type\":\"request\",\"command\":\"configurationDone\",\"arguments\":{}}"
"{\"seq\":5,\"type\":\"request\",\"command\":\"stackTrace\",\"arguments\":{\"threadId\":1}}"
"{\"seq\":6,\"type\":\"request\",\"command\":\"scopes\",\"arguments\":{\"frameId\":1}}"
"{\"seq\":7,\"type\":\"request\",\"command\":\"variables\",\"arguments\":{\"variablesReference\":1}}"
"{\"seq\":8,\"type\":\"request\",\"command\":\"evaluate\",\"arguments\":{\"expression\":\"message\"}}"
"{\"seq\":9,\"type\":\"request\",\"command\":\"readMemory\",\"arguments\":{\"memoryReference\":\"0x00010000\",\"count\":4}}"
"{\"seq\":10,\"type\":\"request\",\"command\":\"next\",\"arguments\":{\"threadId\":1}}"
"{\"seq\":11,\"type\":\"request\",\"command\":\"continue\",\"arguments\":{\"threadId\":1}}"
"{\"seq\":12,\"type\":\"request\",\"command\":\"disconnect\",\"arguments\":{}}"
)

string(ASCII 13 CR)
string(ASCII 10 LF)
set(payload "")
foreach(request IN LISTS requests)
    string(LENGTH "${request}" length)
    string(APPEND payload "Content-Length: ${length}${CR}${LF}${CR}${LF}${request}")
endforeach()
set(request_file "${BINARY_DIR}/dap-requests.txt")
file(WRITE "${request_file}" "${payload}")

execute_process(
    COMMAND "${KR32}" dap --stdio "${image}"
    INPUT_FILE "${request_file}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "DAP exited ${result}: ${errors}")
endif()
if(NOT output MATCHES "\"event\":\"initialized\"")
    message(FATAL_ERROR "DAP did not initialize: ${output}")
endif()
if(NOT output MATCHES "\"reason\":\"breakpoint\"")
    message(FATAL_ERROR "DAP did not stop at a breakpoint: ${output}")
endif()
if(NOT output MATCHES "\"event\":\"exited\"")
    message(FATAL_ERROR "DAP did not continue to exit: ${output}")
endif()
if(NOT output MATCHES "\"data\":\"")
    message(FATAL_ERROR "DAP readMemory did not return data: ${output}")
endif()
if(NOT output MATCHES "\"result\":\"0x")
    message(FATAL_ERROR "DAP evaluate did not return a value: ${output}")
endif()
message(STATUS "DAP stdio smoke passed")
