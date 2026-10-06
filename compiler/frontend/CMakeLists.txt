# The C++L extension frontend.
#
# It owns exactly the syntax C++ does not already own, and nothing else: it is
# not a second C++ parser (ARCHITECTURE.md 7). It runs on preprocessed text, so
# macros are already expanded and constructs contributed by headers are visible
# (SPEC.md 3.2).
add_library(cppl_frontend STATIC
    src/admissible.cpp
    src/formal_projection.cpp
    src/lexer.cpp
    src/projection.cpp
    src/projection_declarations.cpp
    src/projection_paths.cpp
    src/projection_refinements.cpp
    src/projection_scopes.cpp
    src/recognizer.cpp
    src/recognizer_declarators.cpp
    src/recognizer_functions.cpp
    src/recognizer_laws.cpp
    src/recognizer_proofs.cpp
    src/recognizer_scan.cpp
    src/recognizer_settle.cpp
    src/recognizer_statements.cpp
    src/structure.cpp
    src/words.cpp)

target_include_directories(cppl_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_frontend
    PUBLIC cppl_source cppl_diagnostics cppl_case_labels
    PRIVATE cppl_project_options)

add_library(cppl::frontend ALIAS cppl_frontend)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_frontend)
