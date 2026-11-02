if(NOT DEFINED KR32_SCAN_DIR)
    message(FATAL_ERROR "KR32_SCAN_DIR must be defined")
endif()

if(IS_DIRECTORY "${KR32_SCAN_DIR}")
    file(GLOB_RECURSE _kr32_boundary_files
        "${KR32_SCAN_DIR}/*.cpp"
        "${KR32_SCAN_DIR}/*.cc"
        "${KR32_SCAN_DIR}/*.cxx"
        "${KR32_SCAN_DIR}/*.h"
        "${KR32_SCAN_DIR}/*.hpp")
else()
    set(_kr32_boundary_files "${KR32_SCAN_DIR}")
endif()

set(_kr32_forbidden_patterns
    "SDL"
    "windows\\.h"
    "X11/"
    "wayland"
    "cocoa/"
    "d3d"
    "vulkan"
    "unistd\\.h")

foreach(_kr32_file IN LISTS _kr32_boundary_files)
    file(STRINGS "${_kr32_file}" _kr32_lines)
    foreach(_kr32_line IN LISTS _kr32_lines)
        if(_kr32_line MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]*)")
            set(_kr32_include "${CMAKE_MATCH_1}")
            foreach(_kr32_pattern IN LISTS _kr32_forbidden_patterns)
                if(_kr32_include MATCHES "${_kr32_pattern}")
                    message(FATAL_ERROR
                        "Platform boundary violation: ${_kr32_file} includes ${_kr32_include}")
                endif()
            endforeach()
        endif()
    endforeach()
endforeach()

message(STATUS "Platform boundary check passed: ${KR32_SCAN_DIR}")
