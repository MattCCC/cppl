# Evidence production.
#
# Automation is untrusted by construction: it can only hand a proof term to the
# kernel, and the kernel decides. Solvers, tactics and search would be added
# here, behind the same boundary (ARCHITECTURE.md 21).
add_library(cppl_automation STATIC
    src/evidence.cpp
    src/composition.cpp)

target_include_directories(cppl_automation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_automation
    PUBLIC cppl_kernel cppl_obligations cppl_diagnostics
    PRIVATE cppl_project_options)

add_library(cppl::automation ALIAS cppl_automation)
