# Verifies that every checked-in artefact matches the provenance manifest.
#
#   cmake -DROOT=<repo> -DMANIFEST=<repo>/third_party/provenance.json -P cmake/CheckProvenance.cmake
#
# A manifest entry names the file, its sha256 and where it came from. The check
# fails when a listed file is missing, when its hash differs, or when a file is
# present in the directories the manifest covers and is not listed -- a replaced
# or added binary is the failure this row exists for.

if(NOT DEFINED ROOT OR NOT DEFINED MANIFEST)
    message(FATAL_ERROR "CheckProvenance: ROOT and MANIFEST are required")
endif()
if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "CheckProvenance: no manifest at ${MANIFEST}")
endif()

file(READ "${MANIFEST}" _json)
string(JSON _count ERROR_VARIABLE _json_error LENGTH "${_json}" entries)
if(_json_error)
    message(FATAL_ERROR "CheckProvenance: ${MANIFEST} is not valid JSON: ${_json_error}")
endif()

set(_failures "")
set(_listed "")
math(EXPR _last "${_count} - 1")
foreach(_index RANGE ${_last})
    string(JSON _path GET "${_json}" entries ${_index} path)
    string(JSON _sha GET "${_json}" entries ${_index} sha256)
    string(JSON _source GET "${_json}" entries ${_index} source)
    list(APPEND _listed "${_path}")

    set(_file "${ROOT}/${_path}")
    if(NOT EXISTS "${_file}")
        string(APPEND _failures "  ${_path} is listed but missing\n")
        continue()
    endif()
    file(SHA256 "${_file}" _actual)
    if(NOT _actual STREQUAL _sha)
        string(APPEND _failures
            "  ${_path} hashes to ${_actual}, manifest records ${_sha}\n")
    endif()
endforeach()

# Anything in a covered directory that the manifest does not name is a file whose
# origin nobody recorded -- unless the manifest lists it under `ignore`. That list
# exists for a dependency fetched into the source tree and ignored by git: the
# SDL2 SDK the UI links against has no checked-in origin to attest. It is named
# explicitly rather than inferred, so a new unchecked-in tree is a visible
# decision in the manifest rather than a silent exemption.
set(_ignores "")
string(JSON _ignore_count ERROR_VARIABLE _ignore_error LENGTH "${_json}" ignore)
if(NOT _ignore_error)
    math(EXPR _ignore_last "${_ignore_count} - 1")
    foreach(_index RANGE ${_ignore_last})
        string(JSON _ignore GET "${_json}" ignore ${_index})
        list(APPEND _ignores "${_ignore}")
    endforeach()
endif()
string(JSON _dirs ERROR_VARIABLE _dirs_error LENGTH "${_json}" directories)
if(NOT _dirs_error)
    math(EXPR _dir_last "${_dirs} - 1")
    foreach(_index RANGE ${_dir_last})
        string(JSON _dir GET "${_json}" directories ${_index})
        file(GLOB_RECURSE _present RELATIVE "${ROOT}" "${ROOT}/${_dir}/*")
        foreach(_file IN LISTS _present)
            if("${_file}" STREQUAL "third_party/provenance.json")
                continue()
            endif()
            set(_ignored FALSE)
            foreach(_ignore IN LISTS _ignores)
                string(FIND "${_file}" "${_ignore}" _at)
                if(_at EQUAL 0)
                    set(_ignored TRUE)
                    break()
                endif()
            endforeach()
            if(_ignored)
                continue()
            endif()
            list(FIND _listed "${_file}" _known)
            if(_known EQUAL -1)
                string(APPEND _failures "  ${_file} is present but not in the manifest\n")
            endif()
        endforeach()
    endforeach()
endif()

if(NOT _failures STREQUAL "")
    message(FATAL_ERROR "provenance check failed:\n${_failures}")
endif()
message(STATUS "CheckProvenance: ${_count} artefacts match ${MANIFEST}")
