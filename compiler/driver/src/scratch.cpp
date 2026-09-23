#include "cppl/driver/scratch.hpp"

#include "cppl/source/digest.hpp"

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <cstdint>
#include <random>
#else
// mkdtemp is declared in <stdlib.h> by glibc, as POSIX specifies, and in
// <unistd.h> by macOS. Both are included so each platform finds its own.
#include <stdlib.h> // IWYU pragma: keep
#include <unistd.h> // IWYU pragma: keep
#endif

namespace cppl::driver {

ScratchDirectory::ScratchDirectory() {
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error) {
        return;
    }
#ifdef _WIN32
    // The per-user temporary directory is already private, so a name no
    // other process holds is enough. create_directory reports false without
    // an error when the name is taken, which is the collision to retry.
    std::mt19937_64 generator{std::random_device{}()};
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string name = "cppl-";
        const std::uint64_t value = generator();
        for (int shift = 60; shift >= 0; shift -= 4) {
            name.push_back("0123456789abcdef"[(value >> shift) & 0xF]);
        }
        const std::filesystem::path candidate = temporary / name;
        std::error_code creation;
        if (std::filesystem::create_directory(candidate, creation)) {
            path_ = candidate;
            return;
        }
        if (creation) {
            return;
        }
    }
#else
    // mkdtemp creates the directory owner-only, so the preprocessed source
    // and the projections are not exposed in a shared temporary directory.
    std::string pattern = (temporary / "cppl-XXXXXX").string();
    if (const char* created = ::mkdtemp(pattern.data())) {
        path_ = created;
    }
#endif
}

ScratchDirectory::~ScratchDirectory() {
    if (!path_.empty()) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
}

std::filesystem::path scratch_directory(const std::filesystem::path& root, const std::string& input) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(input, error);
    const source::Digest digest = source::hash_bytes(absolute.string());
    const std::filesystem::path directory = root / digest.to_short_hex(16);
    std::filesystem::create_directories(directory, error);
    return error ? std::filesystem::path{} : directory;
}

bool write_scratch_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(stream);
}

} // namespace cppl::driver
