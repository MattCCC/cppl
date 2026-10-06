# Proof-obligation generation.
#
# This is where VIR meets the formal core: it lowers proof-relevant values into
# core terms and states Laws as core propositions. It depends on the kernel;
# the kernel does not depend on it.
add_library(cppl_obligations STATIC
    src/aggregates.cpp
    src/generate.cpp
    src/contracts.cpp
    src/contracts_build.cpp
    src/contracts_conditions.cpp
    src/contracts_evaluation.cpp
    src/contracts_routes.cpp
    src/contracts_termination.cpp
    src/contradiction.cpp
    src/interface.cpp
    src/definedness.cpp
    src/library.cpp
    src/status.cpp
    src/trust.cpp
    src/walks.cpp)

target_include_directories(cppl_obligations PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_obligations
    PUBLIC cppl_kernel cppl_vir cppl_decomposition cppl_elaboration cppl_source cppl_diagnostics cppl_artifact
    PRIVATE cppl_refutation cppl_project_options)

add_library(cppl::obligations ALIAS cppl_obligations)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_obligations)
