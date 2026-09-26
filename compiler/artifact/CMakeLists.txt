# Verification interfaces: the artifact one translation unit records of the
# contracts it proved, and the strict reader every other unit uses before
# believing any of it (ARCHITECTURE.md 63, 64, 68; SPEC.md TUBOUND-002).
#
# A leaf above `source`: it knows the format and nothing about what a contract
# means, so the reader can be fuzzed without the rest of the compiler, and the
# kernel never depends on it.
add_library(cppl_artifact STATIC
    src/interface.cpp)

target_include_directories(cppl_artifact PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_artifact
    PUBLIC cppl_source
    PRIVATE cppl_project_options)

add_library(cppl::artifact ALIAS cppl_artifact)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_artifact)
