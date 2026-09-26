# Every source file under the code roots is classified: it lies in exactly one
# component of the semantic list, which the verifier semantics digest covers, or
# of the non-semantic list, each entry of which states why it cannot change what
# a verification result means (cmake/VerifierSemanticsSources.cmake).
#
#   cmake -DROOT=<source root> -P CheckVerifierSemantics.cmake
#
# A source of a new component, or a component moved, fails here until it is
# classified, so nothing semantic can fall outside the digest unnoticed.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "CheckVerifierSemantics.cmake needs -DROOT=<source root>")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VerifierSemanticsSources.cmake")

set(problems)
foreach(component IN LISTS CPPL_SEMANTIC_COMPONENTS CPPL_NON_SEMANTIC_COMPONENTS)
    if(NOT IS_DIRECTORY "${ROOT}/${component}")
        list(APPEND problems "the classified component '${component}' does not exist")
    endif()
endforeach()
foreach(file IN LISTS CPPL_NON_SEMANTIC_FILES)
    if(NOT EXISTS "${ROOT}/${file}")
        list(APPEND problems "the classified file '${file}' does not exist")
    endif()
endforeach()

cppl_component_sources("${ROOT}" "${CPPL_SOURCE_ROOTS}" every)
cppl_component_sources("${ROOT}" "${CPPL_SEMANTIC_COMPONENTS}" semantic)
cppl_component_sources("${ROOT}" "${CPPL_NON_SEMANTIC_COMPONENTS}" excluded)
list(APPEND excluded ${CPPL_NON_SEMANTIC_FILES})

foreach(relative IN LISTS every)
    list(FIND semantic "${relative}" in_semantic)
    list(FIND excluded "${relative}" in_excluded)
    if(in_semantic EQUAL -1 AND in_excluded EQUAL -1)
        list(APPEND problems "'${relative}' is in no classified component")
    elseif(NOT in_semantic EQUAL -1 AND NOT in_excluded EQUAL -1)
        list(APPEND problems "'${relative}' is classified both as semantic and as not")
    endif()
endforeach()

if(problems)
    list(JOIN problems "\n  " listed)
    message(FATAL_ERROR "Verifier sources are not all classified (cmake/VerifierSemanticsSources.cmake):\n  "
                        "${listed}")
endif()

list(LENGTH every total)
list(LENGTH semantic digested)
message(STATUS "${total} sources classified, ${digested} of them in the verifier semantics digest")
