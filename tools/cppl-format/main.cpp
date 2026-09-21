#include "cppl/formatter/format.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CommandLine {
    std::vector<std::string> files;
    bool in_place = false;
    bool check = false;
    std::string clang_format;
    bool errors = false;
};

CommandLine parse_arguments(int argc, char** argv) {
    CommandLine result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-i" || argument == "--in-place") {
            result.in_place = true;
        } else if (argument == "--check") {
            result.check = true;
        } else if (argument.starts_with("--clang-format=")) {
            result.clang_format = argument.substr(std::string_view("--clang-format=").size());
        } else if (argument.starts_with("-")) {
            std::cerr << "cppl-format: unknown option '" << argument << "'\n";
            result.errors = true;
        } else {
            result.files.push_back(argument);
        }
    }
    if (result.in_place && result.check) {
        std::cerr << "cppl-format: --in-place and --check are mutually exclusive\n";
        result.errors = true;
    }
    if (result.files.empty()) {
        std::cerr << "cppl-format: no input files\n";
        result.errors = true;
    }
    return result;
}

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(stream);
}

std::string apply_edits(std::string text, std::vector<cppl::formatter::FormatEdit> edits) {
    std::ranges::sort(edits, [](const auto& lhs, const auto& rhs) { return lhs.span.offset > rhs.span.offset; });
    for (const auto& edit : edits) {
        text.replace(edit.span.offset, edit.span.length, edit.replacement);
    }
    return text;
}

} // namespace

// A CLI over cppl::formatter, the same engine cppl-lsp's document/range/
// on-type formatting requests call: this tool and the editor always agree
// (tools/cppl-lsp/README.md, tools/cppl-format/README.md).
int main(int argc, char** argv) {
    const CommandLine command_line = parse_arguments(argc, argv);
    if (command_line.errors) {
        return 2;
    }

    bool any_would_change = false;
    bool any_failed = false;

    for (const std::string& path : command_line.files) {
        const std::optional<std::string> text = read_file(path);
        if (!text.has_value()) {
            std::cerr << "cppl-format: could not read '" << path << "'\n";
            any_failed = true;
            continue;
        }

        cppl::formatter::FormatRequest request;
        request.text = *text;
        request.clang_format = command_line.clang_format;
        request.virtual_path = path;

        const cppl::formatter::FormatResult result = cppl::formatter::format_document(request);
        if (!result.ok) {
            std::cerr << "cppl-format: failed to format '" << path << "'\n";
            for (const auto& diagnostic : result.diagnostics) {
                std::cerr << "  " << diagnostic.message << "\n";
            }
            any_failed = true;
            continue;
        }

        if (result.edits.empty()) {
            if (!command_line.in_place && !command_line.check) {
                std::cout << *text;
            }
            continue;
        }

        any_would_change = true;
        if (command_line.check) {
            continue;
        }

        const std::string formatted = apply_edits(*text, result.edits);
        if (command_line.in_place) {
            if (!write_file(path, formatted)) {
                std::cerr << "cppl-format: could not write '" << path << "'\n";
                any_failed = true;
            }
        } else {
            std::cout << formatted;
        }
    }

    if (any_failed) {
        return 1;
    }
    if (command_line.check && any_would_change) {
        return 1;
    }
    return 0;
}
