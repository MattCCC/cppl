# cmake/Linker.cmake
#
# Which linker the project's executables are linked with.
#
# A one-file change relinks every executable that depends on it, which is most
# of the test binaries, so an incremental build is mostly linking. GNU ld, the
# default the compiler driver picks on Linux, takes about a second for each of
# them with debug information; LLVM's lld does the same link several times
# faster. It reads the same options (`-z relro`, `-z now`, `-pie`, CET notes),
# so what is linked, and what `architecture_hardening` reads back from it, is
# the same program.
#
#   cppl_select_linker()
#
# lld is used where the compiler driver can find it, which is probed rather
# than assumed: Clang finds `ld.lld` beside itself, GCC finds it on the PATH. A
# toolchain without it keeps its default linker and links exactly as before.
# Only ELF targets are considered. ld64 and link.exe are already their
# platform's native linkers, and their lld counterparts accept different
# options.
#
# CPPL_LINK_WITH_LLD=OFF keeps the default linker everywhere.

include_guard(GLOBAL)

function(cppl_select_linker)
    set(CPPL_LINKER "default" PARENT_SCOPE)

    if(NOT CPPL_LINK_WITH_LLD)
        return()
    endif()

    if(MSVC OR APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|GNU)$")
        return()
    endif()

    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=lld" CPPL_HAVE_LLD)

    if(CPPL_HAVE_LLD)
        add_link_options(-fuse-ld=lld)
        set(CPPL_LINKER "lld" PARENT_SCOPE)
    endif()
endfunction()
