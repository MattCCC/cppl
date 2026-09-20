# Compiler components.
#
# `source` is added by the top-level CMakeLists because the VIR and the Clang
# bridge depend on it and are configured before this directory.
#
# Dependency direction (ARCHITECTURE.md 44):
#
#   driver -> frontend / clang bridge -> elaboration -> vir
#          -> obligations -> automation -> kernel
#
# `decomposition` sits beside elaboration on top of the VIR: it states what a
# C++ representation's proof-visible states are, and is consumed by elaboration
# and obligations (ARCHITECTURE.md 44).
add_subdirectory(diagnostics)
add_subdirectory(decomposition)
add_subdirectory(frontend)
add_subdirectory(elaboration)
add_subdirectory(obligations)
add_subdirectory(automation)
add_subdirectory(erasure)
add_subdirectory(driver)
