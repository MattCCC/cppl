# The Clang semantic bridge.
#
# Clang is the authority for ordinary C++ semantics: parsing, name lookup,
# overload resolution, conversions and target layout. This target translates
# the facts C++L needs into C++L's own typed representation, and is the only
# place in the project that includes a Clang header (ARCHITECTURE.md 11,
# AGENTS.md 14).
add_library(cppl_clang STATIC
    src/bridge.cpp)

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
