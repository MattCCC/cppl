// Runs a fuzz target over the inputs in the given directories, in name order,
// without libFuzzer. Every build links its fuzz targets against this, so each
// seed and each saved regression is replayed on every compiler, including the
// ones libFuzzer does not support.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

int replay(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));

    std::vector<std::filesystem::path> inputs;
    for (const char* directory : arguments.subspan(1)) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (entry.is_regular_file()) {
                inputs.push_back(entry.path());
            }
        }
        if (error) {
            std::cerr << directory << ": " << error.message() << '\n';
            return 2;
        }
    }
    std::ranges::sort(inputs);

    // A replay of nothing would pass without testing anything.
    if (inputs.empty()) {
        std::cerr << "no inputs to replay\n";
        return 2;
    }

    for (const auto& path : inputs) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            std::cerr << path.string() << ": cannot be read\n";
            return 2;
        }
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::vector<std::uint8_t> bytes(text.size());
        std::ranges::transform(text, bytes.begin(), [](const char c) { return static_cast<std::uint8_t>(c); });

        std::cout << path.filename().string() << '\n';
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    }

    std::cout << "replayed " << inputs.size() << " inputs\n";
    return 0;
}

} // namespace

// An exception out of a target is a failure of that input, reported as one
// rather than as std::terminate.
int main(int argc, char** argv) {
    try {
        return replay(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "uncaught exception: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "uncaught exception\n";
    }
    return 1;
}
