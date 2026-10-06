# Fuzz targets for the code that reads bytes from outside the process
# (docs/CI.md, "Fuzzing").
#
# Each target is one <name>.cpp defining LLVMFuzzerTestOneInput, with its
# inputs in corpus/<name>/: the seeds, and every input that once broke it,
# kept so the break stays fixed.
#
# Every build links each target against replay.cpp and registers
# fuzz_<name>_replay, which runs the target over its corpus. That happens on
# every compiler and under every sanitizer preset.
#
# With CPPL_BUILD_FUZZERS the targets link libFuzzer instead, and
# fuzz_<name>_search looks for new failing inputs: a fixed number of inputs
# from a fixed seed, so a failure repeats when the run does. It starts from the
# corpus, so a saved regression that fails again fails the search first. What
# it finds lands in the build tree, never in the corpus.

set(CPPL_FUZZ_RUNS 200000 CACHE STRING "Inputs each fuzz_<name>_search test tries")
set(CPPL_FUZZ_SEED 1 CACHE STRING "libFuzzer seed of each fuzz_<name>_search test")
# A search does a fixed amount of work, and a kernel search's inputs grow costlier
# as its corpus grows, so it is given the time that work takes rather than the
# time any other test may. -timeout=10 still fails it on any single slow input.
set(CPPL_FUZZ_TIMEOUT 3600 CACHE STRING "Seconds each fuzz_<name>_search test may take")

add_custom_target(fuzzers)

# cppl_add_fuzz_target(<name> LIBRARIES <library>... [MAX_LEN <bytes>]
#                      [MALLOC_LIMIT_MB <megabytes>])
#
# MALLOC_LIMIT_MB fails the search on any single allocation larger than that,
# for a target whose property is that the input bounds what it allocates.
function(cppl_add_fuzz_target name)
    cmake_parse_arguments(PARSE_ARGV 1 FUZZ "" "MAX_LEN;MALLOC_LIMIT_MB" "LIBRARIES")

    if(NOT FUZZ_MAX_LEN)
        set(FUZZ_MAX_LEN 4096)
    endif()

    if(NOT FUZZ_MALLOC_LIMIT_MB)
        set(FUZZ_MALLOC_LIMIT_MB 2048)
    endif()

    set(target fuzz_${name})
    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}")

    if(NOT IS_DIRECTORY "${corpus}")
        message(FATAL_ERROR "fuzz target '${name}' has no corpus at ${corpus}")
    endif()

    add_executable(${target} ${name}.cpp)
    target_link_libraries(${target} PRIVATE ${FUZZ_LIBRARIES} cppl_project_options)
    target_include_directories(${target} PRIVATE
        "$<TARGET_PROPERTY:cppl_testing,INTERFACE_INCLUDE_DIRECTORIES>")
    add_dependencies(fuzzers ${target})

    if(NOT CPPL_BUILD_FUZZERS)
        target_sources(${target} PRIVATE replay.cpp)

        add_test(NAME ${target}_replay
                 COMMAND ${CPPL_BOUNDED_PREFIX} $<TARGET_FILE:${target}> "${corpus}")
        set_tests_properties(${target}_replay PROPERTIES
            LABELS "unit;fuzz-replay"
            TIMEOUT ${CPPL_TEST_TIMEOUT})
        return()
    endif()

    target_link_options(${target} PRIVATE -fsanitize=fuzzer)

    set(work "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    file(MAKE_DIRECTORY "${work}/corpus" "${work}/artifacts")

    add_test(NAME ${target}_search
             COMMAND ${CPPL_BOUNDED_PREFIX} $<TARGET_FILE:${target}>
                     -seed=${CPPL_FUZZ_SEED}
                     -runs=${CPPL_FUZZ_RUNS}
                     -max_len=${FUZZ_MAX_LEN}
                     -malloc_limit_mb=${FUZZ_MALLOC_LIMIT_MB}
                     -rss_limit_mb=2048
                     -timeout=10
                     -print_final_stats=1
                     "-artifact_prefix=${work}/artifacts/"
                     "${work}/corpus"
                     "${corpus}")
    # AddressSanitizer keeps freed memory in its quarantine and its free lists,
    # and keeps every distinct allocation stack it has seen, so a search's
    # resident size climbs with the inputs it has run, not with any one of
    # them, until the RSS limit stops a search no input was too large for.
    # Memory is returned to the system every second, the quarantine is bounded,
    # and an allocation records four frames of its stack: a use after free is
    # still caught until 64 MiB of later frees have passed through it, and the
    # access that faults is still reported with its whole stack.
    set_tests_properties(${target}_search PROPERTIES
        LABELS "fuzz"
        ENVIRONMENT
            "ASAN_OPTIONS=allocator_release_to_os_interval_ms=1000:quarantine_size_mb=64:malloc_context_size=4"
        TIMEOUT ${CPPL_FUZZ_TIMEOUT})
endfunction()

cppl_add_fuzz_target(lsp_json LIBRARIES cppl_lsp MAX_LEN 65536)
cppl_add_fuzz_target(lsp_message LIBRARIES cppl_lsp MAX_LEN 65536 MALLOC_LIMIT_MB 16)
cppl_add_fuzz_target(lsp_uri LIBRARIES cppl_lsp)
cppl_add_fuzz_target(frontend LIBRARIES cppl_frontend cppl_erasure cppl_diagnostics MAX_LEN 16384)
cppl_add_fuzz_target(interface LIBRARIES cppl_artifact MAX_LEN 16384 MALLOC_LIMIT_MB 64)

# The trusted kernel (docs/KERNEL.md, "Testing"): derivations, arithmetic
# certificates, the term operations its rules rest on, and the certificate
# checker alone. Each input is a stream of choices, so any byte string decodes
# to a finite, well-formed sample; the oracle is the independent model in
# tests/support/kernel.
cppl_add_fuzz_target(kernel_proof LIBRARIES cppl_kernel_testing MAX_LEN 1024)
cppl_add_fuzz_target(kernel_arithmetic LIBRARIES cppl_kernel_testing MAX_LEN 1024)
cppl_add_fuzz_target(kernel_terms LIBRARIES cppl_kernel_testing MAX_LEN 512)
cppl_add_fuzz_target(kernel_certificate LIBRARIES cppl_kernel_testing cppl_refutation MAX_LEN 512)
