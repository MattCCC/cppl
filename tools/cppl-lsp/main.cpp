#include "cppl/lsp/server.hpp"
#include "cppl/lsp/transport.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// A minimal command line: cppl-lsp reads LSP JSON-RPC on stdin and writes it
// on stdout, so it takes no positional arguments. `--clang` and `--clang-arg`
// let the editor point the buffer-compile pipeline at a specific Clang and
// forward extra flags (include paths, defines, -std=), mirroring the CLI's
// own `--clang` (cppl::driver::Options::clang).
struct CommandLine {
    std::string clang;
    std::vector<std::string> clang_arguments;
};

CommandLine parse_arguments(int argc, char** argv) {
    CommandLine result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--clang" && i + 1 < argc) {
            result.clang = argv[++i];
        } else if (argument == "--clang-arg" && i + 1 < argc) {
            result.clang_arguments.emplace_back(argv[++i]);
        }
        // An unrecognized argument is ignored rather than rejected: the LSP
        // is normally launched by an editor extension, not typed by hand,
        // and refusing to start over an unknown flag would be a worse
        // failure mode than ignoring it.
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // stdin/stdout must be binary: Windows' default text-mode translation
    // would corrupt the exact byte counts Content-Length promises.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const CommandLine command_line = parse_arguments(argc, argv);
    cppl::lsp::Server server(command_line.clang, command_line.clang_arguments);

    // stdout is the JSON-RPC channel exclusively; every diagnostic message
    // this process itself wants to report (malformed input, internal
    // errors) goes to stderr instead (tools/cppl-lsp/README.md, task spec).
    const int exit_code = cppl::lsp::run_transport(server, std::cin, std::cout, std::cerr);
    return exit_code;
}
