# Refutation search for linear integer arithmetic.
#
# Untrusted by construction, like automation: it only proposes a certificate,
# and every consumer hands that certificate to the kernel inside a proof term,
# where it is checked against the system the kernel states for itself. It sits
# below both obligations and automation because both need it: automation to
# close arithmetic goals, obligations to find the combination behind a written
# `contradiction` (GRAMMAR.md 5.6). It depends on nothing but the kernel's types.
add_library(cppl_refutation STATIC
    src/refute.cpp)

target_include_directories(cppl_refutation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_refutation
    PUBLIC cppl_kernel
    PRIVATE cppl_project_options)

add_library(cppl::refutation ALIAS cppl_refutation)

# Each header is analyzed on its own (cmake/HeaderChecks.cmake).
cppl_check_headers(cppl_refutation)
