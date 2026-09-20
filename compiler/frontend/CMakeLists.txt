# The C++L extension frontend.
#
# It owns exactly the syntax C++ does not already own, and nothing else: it is
# not a second C++ parser (ARCHITECTURE.md 8). It runs on preprocessed text, so
# macros are already expanded and constructs contributed by headers are visible
# (SPEC.md 3.2).
add_library(cppl_frontend STATIC
    src/formal_projection.cpp
    src/lexer.cpp
    src/projection.cpp
    src/recognizer.cpp)

target_include_directories(cppl_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_frontend
    PUBLIC cppl_source cppl_diagnostics
    PRIVATE cppl_project_options)

add_library(cppl::frontend ALIAS cppl_frontend)
