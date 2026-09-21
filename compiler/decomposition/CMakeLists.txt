# Proof decomposition.
#
# The representation-independent decomposition model and the providers that
# describe one C++ representation family each (SPEC.md 20.5, RFC 0013).
#
# It depends on the VIR and on nothing further down the pipeline: a provider
# states what Clang already resolved, and knows nothing about proof obligations,
# evidence or the kernel.
# The label vocabulary alone, with no dependency at all. The parser classifies a
# label before any semantic information exists and must agree with the engine
# that resolves it afterwards, so both read one header.
add_library(cppl_case_labels INTERFACE)
target_include_directories(cppl_case_labels INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
add_library(cppl::case_labels ALIAS cppl_case_labels)

add_library(cppl_decomposition STATIC
    src/structural.cpp
    src/registry.cpp
    src/scoped_enum.cpp)

target_include_directories(cppl_decomposition PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_decomposition
    PUBLIC cppl_vir cppl_source cppl_case_labels
    PRIVATE cppl_project_options)

add_library(cppl::decomposition ALIAS cppl_decomposition)
