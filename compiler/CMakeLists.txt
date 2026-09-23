# Compiler components.
#
# `source` is added by the top-level CMakeLists because the VIR and the Clang
# bridge depend on it and are configured before this directory.
#
# Dependency direction (ARCHITECTURE.md 95):
#
#   driver -> frontend / clang bridge -> elaboration -> vir
#          -> obligations -> automation -> kernel
#
# `decomposition` sits beside elaboration on top of the VIR: it states what a
# C++ representation's proof-visible states are, and is consumed by elaboration
# and obligations (ARCHITECTURE.md 47).
#
# `refutation` is the arithmetic refutation search, below both obligations and
# automation and above nothing but the kernel's types. It is untrusted: what it
# proposes reaches the kernel inside a proof term (ARCHITECTURE.md 95).
add_subdirectory(diagnostics)
add_subdirectory(decomposition)
add_subdirectory(frontend)
add_subdirectory(elaboration)
add_subdirectory(refutation)
add_subdirectory(obligations)
add_subdirectory(automation)
add_subdirectory(erasure)
add_subdirectory(analysis)
add_subdirectory(driver)
add_subdirectory(formatter)
