# The Clang semantic bridge.
#
# Clang is the authority for ordinary C++ semantics: parsing, name lookup,
# overload resolution, conversions and target layout. This target translates
# the facts C++L needs into C++L's own typed representation, and is the only
# place in the project that includes a Clang header (ARCHITECTURE.md 11,
# AGENTS.md 14).
add_library(cppl_clang STATIC
    src/access.cpp
    src/aggregate_effects.cpp
    src/aggregate_values.cpp
    src/bridge.cpp
    src/call_objects.cpp
    src/conversions.cpp
    src/cursors.cpp
    src/default_arguments.cpp
    src/editor.cpp
    src/expressions.cpp
    src/editor_completion.cpp
    src/editor_description.cpp
    src/editor_names.cpp
    src/editor_navigation.cpp
    src/editor_structure.cpp
    src/proof_instantiation.cpp
    src/proof_instantiation_cursors.cpp
    src/proof_instantiation_hazards.cpp
    src/refinements.cpp
    src/sequences.cpp
    src/signature.cpp
    src/statements.cpp
    src/types.cpp
    src/unsafe.cpp
    src/write_scan.cpp)

target_include_directories(cppl_clang PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_clang
    PUBLIC cppl_source
    PRIVATE LibClang::LibClang cppl_project_options)

# libclang lives outside the default loader paths on most installations.
set_target_properties(cppl_clang PROPERTIES
    BUILD_RPATH "${LibClang_LIBRARY_DIR}"
    INSTALL_RPATH "${LibClang_LIBRARY_DIR}")

add_library(cppl::clang ALIAS cppl_clang)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_clang)
