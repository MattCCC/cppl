# The Verification IR: proof-relevant meaning derived from resolved C++
# semantics.
#
# VIR depends on source provenance and nothing else. It must not depend on the
# kernel (the kernel is the authority above it), on Clang, or on any editor or
# diagnostic presentation layer (ARCHITECTURE.md 18, 44).
add_library(cppl_vir STATIC
    src/vir.cpp)

target_include_directories(cppl_vir PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_vir
    PUBLIC cppl_source
    PRIVATE cppl_project_options)

add_library(cppl::vir ALIAS cppl_vir)
