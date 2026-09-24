#include "cppl/lsp/compile_commands.hpp"

#include "cppl/lsp/json.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cppl::lsp {

namespace {

// Larger than any build's database; a file past it is not read at all.
constexpr std::uintmax_t kMaxDatabase = std::uintmax_t{512} * 1024 * 1024;

// Flags followed by a path, as a separate argument or joined to the flag.
constexpr auto kPathFlags = std::to_array<std::string_view>(
    {"-isystem", "-iquote", "-idirafter", "-iframework", "-isysroot", "-include", "-imacros", "-I", "-F"});

// Flags followed by a value that is not a path.
constexpr auto kValueFlags = std::to_array<std::string_view>({"-D", "-U"});

// Flags whose value is joined by `=`, kept whole.
constexpr auto kJoinedFlags = std::to_array<std::string_view>(
    {"-std=", "-stdlib=", "--target=", "--sysroot=", "-march=", "-mcpu=", "-mtune=", "-fms-compatibility-version="});

// Switches kept as they are: each defines a macro or changes the language.
constexpr auto kSwitches = std::to_array<std::string_view>({"-m32",
                                                            "-m64",
                                                            "-pthread",
                                                            "-nostdinc",
                                                            "-nostdinc++",
                                                            "-nostdlibinc",
                                                            "-ffreestanding",
                                                            "-fno-builtin",
                                                            "-fexceptions",
                                                            "-fno-exceptions",
                                                            "-fcxx-exceptions",
                                                            "-fno-cxx-exceptions",
                                                            "-frtti",
                                                            "-fno-rtti",
                                                            "-fchar8_t",
                                                            "-fno-char8_t",
                                                            "-fsigned-char",
                                                            "-funsigned-char",
                                                            "-fshort-wchar",
                                                            "-fms-extensions",
                                                            "-fms-compatibility",
                                                            "-fno-ms-compatibility",
                                                            "-fopenmp",
                                                            "-fno-operator-names",
                                                            "-fsized-deallocation",
                                                            "-fno-sized-deallocation",
                                                            "-faligned-allocation",
                                                            "-fno-aligned-allocation"});

// Flags whose value is a separate argument that is dropped with them.
constexpr auto kDroppedWithValue =
    std::to_array<std::string_view>({"-o", "-MF", "-MT", "-MQ", "-MJ", "-x", "-arch", "-Xclang", "-Xlinker"});

bool one_of(std::string_view word, const auto& words) {
    return std::ranges::find(words, word) != words.end();
}

std::string absolute(std::string_view path, const std::filesystem::path& directory) {
    const std::filesystem::path given(path);
    return (given.is_absolute() ? given : directory / given).lexically_normal().string();
}

std::filesystem::path normal(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path whole = std::filesystem::absolute(path, error);
    return (error ? path : whole).lexically_normal();
}

std::vector<std::string> read_arguments(const json::Value& entry) {
    if (const json::Value* listed = entry.find("arguments"); listed != nullptr && listed->is_array()) {
        std::vector<std::string> arguments;
        for (const json::Value& argument : listed->as_array()) {
            if (argument.is_string()) {
                arguments.push_back(argument.as_string());
            }
        }
        return arguments;
    }
    if (const std::optional<std::string> command = entry.find_string("command")) {
        return split_command(*command);
    }
    return {};
}

// How alike two files are for sharing flags: the same name with another
// extension first, then the number of directories their paths share.
std::size_t likeness(const std::filesystem::path& file, const std::filesystem::path& other) {
    std::size_t shared = 0;
    const std::filesystem::path directory = file.parent_path();
    const std::filesystem::path other_directory = other.parent_path();
    for (auto left = directory.begin(), right = other_directory.begin();
         left != directory.end() && right != other_directory.end() && *left == *right; ++left, ++right) {
        ++shared;
    }
    constexpr std::size_t kSameName = 1U << 16U;
    return shared + (file.stem() == other.stem() ? kSameName : 0);
}

} // namespace

std::vector<std::string> split_command(std::string_view command) {
    std::vector<std::string> words;
    std::string word;
    bool in_word = false;
    char quote = 0;
    for (std::size_t index = 0; index < command.size(); ++index) {
        const char character = command[index];
        if (quote == '\'') {
            if (character == '\'') {
                quote = 0;
            } else {
                word.push_back(character);
            }
            continue;
        }
        if (character == '\\' && index + 1 < command.size()) {
            word.push_back(command[++index]);
            in_word = true;
            continue;
        }
        if (quote == '"') {
            if (character == '"') {
                quote = 0;
            } else {
                word.push_back(character);
            }
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            in_word = true;
            continue;
        }
        if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
            if (in_word) {
                words.push_back(std::move(word));
                word.clear();
                in_word = false;
            }
            continue;
        }
        word.push_back(character);
        in_word = true;
    }
    if (in_word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::vector<std::string> reading_flags(const std::vector<std::string>& arguments,
                                       const std::filesystem::path& directory) {
    std::vector<std::string> kept;
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        const bool has_next = index + 1 < arguments.size();
        if (argument == "--") {
            break; // only inputs follow
        }
        if (one_of(argument, kDroppedWithValue)) {
            ++index;
            continue;
        }
        if (one_of(argument, kPathFlags) || one_of(argument, kValueFlags)) {
            if (has_next) {
                kept.emplace_back(argument);
                kept.push_back(one_of(argument, kPathFlags) ? absolute(arguments[index + 1], directory)
                                                            : arguments[index + 1]);
                ++index;
            }
            continue;
        }
        if (argument == "-target" || argument == "--sysroot") {
            if (has_next) {
                kept.emplace_back(argument);
                kept.push_back(argument == "--sysroot" ? absolute(arguments[index + 1], directory)
                                                       : arguments[index + 1]);
                ++index;
            }
            continue;
        }
        const auto joined_path =
            std::ranges::find_if(kPathFlags, [argument](std::string_view flag) { return argument.starts_with(flag); });
        if (joined_path != kPathFlags.end()) {
            kept.push_back(std::string(*joined_path) + absolute(argument.substr(joined_path->size()), directory));
            continue;
        }
        if (argument.starts_with("--sysroot=")) {
            kept.push_back("--sysroot=" + absolute(argument.substr(std::string_view("--sysroot=").size()), directory));
            continue;
        }
        const bool joined_value =
            std::ranges::any_of(kValueFlags, [argument](std::string_view flag) { return argument.starts_with(flag); });
        const bool joined =
            std::ranges::any_of(kJoinedFlags, [argument](std::string_view flag) { return argument.starts_with(flag); });
        if (joined_value || joined || one_of(argument, kSwitches) ||
            (argument.starts_with("-O") && argument.size() <= 3)) {
            kept.emplace_back(argument);
        }
    }
    return kept;
}

const CompileCommands::Database* CompileCommands::database_at(const std::filesystem::path& file) {
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(file, error);
    if (error) {
        databases_.erase(file);
        return nullptr;
    }
    if (const auto known = databases_.find(file); known != databases_.end() && known->second.written == written) {
        return &known->second;
    }
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    std::ifstream stream(file, std::ios::binary);
    if (error || size > kMaxDatabase || !stream) {
        return nullptr;
    }
    std::ostringstream text;
    text << stream.rdbuf();

    Database database;
    database.written = written;
    try {
        const json::Value parsed = json::parse(text.str());
        if (parsed.is_array()) {
            for (const json::Value& entry : parsed.as_array()) {
                const std::optional<std::string> source = entry.find_string("file");
                const std::optional<std::string> directory = entry.find_string("directory");
                if (!source.has_value() || !directory.has_value()) {
                    continue;
                }
                const std::filesystem::path in = normal(*directory);
                database.entries.push_back(Entry{absolute(*source, in), in, read_arguments(entry)});
            }
        }
    } catch (const json::Error&) {
        // A database a build tool is still writing, or a malformed one, gives
        // no flags; the next request reads it again.
        return nullptr;
    }
    return &(databases_[file] = std::move(database));
}

std::vector<std::string> CompileCommands::flags_for(const std::string& path) {
    if (path.empty() || !std::filesystem::path(path).is_absolute()) {
        return {}; // not a file on disk, so in no build
    }
    const std::filesystem::path file = normal(path);
    for (std::filesystem::path directory = file.parent_path();; directory = directory.parent_path()) {
        for (const std::filesystem::path& candidate :
             {directory / "compile_commands.json", directory / "build" / "compile_commands.json"}) {
            const Database* database = database_at(candidate);
            if (database == nullptr || database->entries.empty()) {
                continue;
            }
            const Entry* chosen = &database->entries.front();
            std::size_t best = likeness(file, chosen->file);
            for (const Entry& entry : database->entries) {
                if (entry.file == file) {
                    chosen = &entry;
                    break;
                }
                if (const std::size_t score = likeness(file, entry.file); score > best) {
                    chosen = &entry;
                    best = score;
                }
            }
            return reading_flags(chosen->arguments, chosen->directory);
        }
        if (directory == directory.parent_path() || directory.empty()) {
            return {};
        }
    }
}

} // namespace cppl::lsp
