# cmake/Hardening.cmake
#
# Exploit mitigations for the binaries C++L ships (docs/CI.md, "Hardening").
#
# They make a memory-safety defect harder to turn into control of the process.
# They fix no defect: that is what warnings, sanitizers, lint and fuzzing are
# for. Every flag here is one the compiler and linker already understand and
# that changes no language semantics, so a hardened build computes exactly
# what an unhardened one does.
#
#   cppl_enable_hardening(<interface target>)
#
# Each platform gets its own mechanisms. Flags are never copied between
# toolchains: an ELF linker option means nothing to ld64 or link.exe.

include_guard(GLOBAL)

function(cppl_enable_hardening target)
    # Windows, MSVC and clang-cl. /GS, /DYNAMICBASE, /NXCOMPAT and
    # /HIGHENTROPYVA are already the defaults for x64; Control Flow Guard and
    # CET shadow-stack compatibility are not.
    if(MSVC)
        target_compile_options(${target} INTERFACE /guard:cf)
        target_link_options(${target} INTERFACE /guard:cf /CETCOMPAT)
        return()
    endif()

    # Stack canaries on every function with a local array or an address-taken
    # local.
    target_compile_options(${target} INTERFACE -fstack-protector-strong)

    # Hardware control-flow integrity where the ABI defines it: CET on x86-64,
    # BTI and return-address signing on AArch64 ELF. Apple's AArch64 uses
    # pointer authentication through the arm64e ABI instead, which is not a
    # flag an ordinary executable opts into.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        target_compile_options(${target} INTERFACE -fcf-protection=full)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND NOT APPLE)
        target_compile_options(${target} INTERFACE -mbranch-protection=standard)
    endif()

    # Bounds and precondition checks inside the standard library: operator[],
    # front(), back(), optional's operator*. Each macro is ignored by the other
    # library, so both are always defined. Neither changes the ABI.
    target_compile_definitions(
        ${target}
        INTERFACE
            _GLIBCXX_ASSERTIONS
            _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST
    )

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Darwin executables are always position-independent, Darwin already
        # fortifies libc calls when optimizing, and its linker has no RELRO:
        # __DATA_CONST is made read-only by dyld itself.
        return()
    endif()

    # A position-independent executable, so ASLR moves its code as well as its
    # libraries.
    target_compile_options(${target} INTERFACE -fPIE)
    target_link_options(${target} INTERFACE -pie)

    # A stack allocation large enough to jump the guard page probes each page
    # instead. AppleClang accepts the flag for arm64 and ignores it, so it is
    # given only where GCC and Clang both implement it.
    target_compile_options(${target} INTERFACE -fstack-clash-protection)

    # Checked variants of libc's memory and string functions. glibc warns when
    # fortification is requested without optimization, which -Werror turns into
    # a failed build, so a Debug build is left unfortified. Any level the
    # compiler already defines is replaced rather than redefined.
    target_compile_options(
        ${target}
        INTERFACE
            "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=3>"
    )

    # Relocations resolved at load and then made read-only, and a stack that
    # cannot be executed.
    target_link_options(
        ${target}
        INTERFACE
            LINKER:-z,relro
            LINKER:-z,now
            LINKER:-z,noexecstack
    )
endfunction()
