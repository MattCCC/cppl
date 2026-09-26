# Which sources decide what a verification result means.
#
# A verification interface records what one unit proved, and another unit uses
# it only when both were produced by a verifier with the same meaning (SPEC.md
# TUBOUND-005). The manual verification semantics version
# (`obligations::kVerificationSemanticsVersion`) says so by declaration; the
# verifier semantics digest says so mechanically: it is computed from every
# source of the components below, so a change to any of them makes interfaces
# of the earlier sources unusable, whether or not someone remembered to bump the
# version (TRUST.md TCB-XTU-008). The digest guards the version; it does not
# replace it.
#
# Included here, deliberately over-inclusive within the components that take
# part in verification: a reworded message inside one costs a rebuild of the
# interfaces that depend on it, which is the conservative direction.
#
# Read by the digest script (ComputeVerifierSemantics.cmake), by the build that
# runs it, and by the classification check (CheckVerifierSemantics.cmake). Every
# source file under the code roots must lie in exactly one component of one of
# the two lists, so a new component cannot fall outside the digest unnoticed.
#
# Only C and C++ sources and component CMakeLists are read: documentation, file
# names alone, modification times and absolute paths never enter the digest.

set(CPPL_SEMANTIC_COMPONENTS
    # The proof kernel and its formal core.
    kernel
    # The verification IR.
    vir
    # The Clang bridge: correspondence code deciding what C++ means to the verifier.
    clang
    # Standard-library verification support.
    stdlib
    # Source identities, locations, digests and representation kinds.
    compiler/source
    # Diagnostics decide whether a unit verified: an error fails it.
    compiler/diagnostics
    # The verification-interface format, its reader and writer.
    compiler/artifact
    # Decomposition providers: the states a representation has.
    compiler/decomposition
    # Recognition and projection of C++L syntax.
    compiler/frontend
    # Elaboration into VIR.
    compiler/elaboration
    # The analysis the driver runs the bridge through.
    compiler/analysis
    # The untrusted refutation search whose certificates the kernel checks.
    compiler/refutation
    # Obligations: contracts, definedness, library summaries, trust closure.
    compiler/obligations
    # Automation producing the evidence the kernel checks.
    compiler/automation
    # The driver: the pipeline, and interface reading and writing.
    compiler/driver)

set(CPPL_NON_SEMANTIC_COMPONENTS
    # Runs after verification and decides only the runtime program, which its
    # own validator and the erasure-equivalence tests check.
    compiler/erasure
    # Source layout only.
    compiler/formatter
    # The editor server; it never writes or reads an interface.
    src/lsp
    # Command-line tools (the formatter, the language server and the rule
    # checker executables) and CI wrappers; none verifies a unit.
    tools)

# Single files under the code roots outside every component, each with why it
# is not semantic.
set(CPPL_NON_SEMANTIC_FILES
    # Adds the components in dependency order; each is classified on its own.
    compiler/CMakeLists.txt)

# The roots every source file lies under, whichever list classifies it.
set(CPPL_SOURCE_ROOTS compiler kernel vir clang stdlib src tools)

# What counts as a source: a C or C++ file, or a component's CMakeLists.
set(CPPL_SOURCE_PATTERNS *.c *.cc *.cpp *.cxx *.h *.hh *.hpp *.hxx *.inc *.def CMakeLists.txt)

# The files of `components` under `root`, as root-relative paths in byte order.
# In a configured project the glob is re-evaluated on every build, so a file
# added to a semantic component changes the digest without a reconfigure.
function(cppl_component_sources root components out_var)
    set(refresh)
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set(refresh CONFIGURE_DEPENDS)
    endif()
    set(found)
    foreach(component IN LISTS components)
        foreach(pattern IN LISTS CPPL_SOURCE_PATTERNS)
            file(GLOB_RECURSE matched LIST_DIRECTORIES false ${refresh} RELATIVE "${root}"
                 "${root}/${component}/${pattern}")
            list(APPEND found ${matched})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES found)
    list(SORT found COMPARE STRING)
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()
