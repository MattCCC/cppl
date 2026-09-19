# The trusted proof kernel.
#
# This target deliberately links nothing. It must not gain dependencies on the
# frontend, the Clang bridge, VIR, solvers, diagnostics rendering or editor
# tooling (ARCHITECTURE.md 23, TRUST.md). tests/architecture enforces this.
add_library(cppl_kernel STATIC
    src/arithmetic.cpp
    src/check.cpp
    src/context.cpp
    src/linear.cpp
    src/proof.cpp
    src/proposition.cpp
    src/substitution.cpp
    src/term.cpp
    src/types.cpp)

target_include_directories(cppl_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_kernel PRIVATE cppl_project_options)

add_library(cppl::kernel ALIAS cppl_kernel)
