# Compiler components.
#
# `source` is added by the top-level CMakeLists because the VIR and the Clang
# bridge depend on it and are configured before this directory.
#
# Dependency direction (ARCHITECTURE.md 44):
#
#   driver -> frontend / clang bridge -> elaboration -> vir
#          -> obligations -> automation -> kernel
add_subdirectory(diagnostics)
add_subdirectory(frontend)
add_subdirectory(elaboration)
add_subdirectory(obligations)
add_subdirectory(automation)
add_subdirectory(erasure)
add_subdirectory(driver)
