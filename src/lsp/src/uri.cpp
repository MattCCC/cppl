#include "cppl/lsp/uri.hpp"

#include <cctype>

namespace cppl::lsp {

namespace {

constexpr std::string_view kFileScheme = "file://";

// std::isxdigit already validated `digit` at the call site.
[[nodiscard]] unsigned int hex_digit_value(char digit) {
    if (digit >= '0' && digit <= '9') {
        return static_cast<unsigned int>(digit - '0');
    }
    return static_cast<unsigned int>(std::tolower(static_cast<unsigned char>(digit)) - 'a' + 10);
}

[[nodiscard]] std::optional<std::string> percent_decode(std::string_view text) {
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '%') {
            decoded.push_back(c);
            continue;
        }
        if (i + 2 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            // A malformed escape: fail closed rather than guess (AGENTS.md
            // 23), the caller falls back to the raw URI text.
            return std::nullopt;
        }
        const unsigned int value = (hex_digit_value(text[i + 1]) << 4) | hex_digit_value(text[i + 2]);
        decoded.push_back(static_cast<char>(value));
        i += 2;
    }
    return decoded;
}

[[nodiscard]] bool looks_like_windows_drive(std::string_view text) {
    // "/C:/..." (the leading '/' from the URI authority separator, kept
    // deliberately before the drive letter by the LSP spec's own examples).
    return text.size() >= 3 && text[0] == '/' && std::isalpha(static_cast<unsigned char>(text[1])) && text[2] == ':';
}

[[nodiscard]] char to_hex_digit(unsigned int value) {
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + (value - 10));
}

[[nodiscard]] bool is_unreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':';
}

} // namespace

std::optional<std::string> uri_to_path(const std::string& uri) {
    if (uri.compare(0, kFileScheme.size(), kFileScheme) != 0) {
        return std::nullopt;
    }

    std::string_view rest{uri};
    rest.remove_prefix(kFileScheme.size());

    // An authority (host) component before the path is not something a
    // local file path can express beyond the empty/"localhost" cases LSP
    // clients actually send; strip it if present rather than reject.
    if (const auto slash = rest.find('/'); slash != std::string_view::npos && slash != 0) {
        rest.remove_prefix(slash);
    }

    std::optional<std::string> decoded = percent_decode(rest);
    if (!decoded.has_value()) {
        return std::nullopt;
    }

    if (looks_like_windows_drive(*decoded)) {
        // Drop the leading '/' so "/C:/path" becomes the native "C:/path".
        return decoded->substr(1);
    }

    return decoded;
}

std::string path_to_uri(const std::string& path) {
    std::string encoded;
    encoded.reserve(path.size() + kFileScheme.size());
    encoded += kFileScheme;

    // A Windows drive path ("C:/...") needs a leading '/' to become a valid
    // file:// URI authority-less path ("/C:/...").
    const bool windows_drive = path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
    if (windows_drive) {
        encoded.push_back('/');
    }

    for (const unsigned char c : path) {
        if (c == '\\') {
            encoded.push_back('/');
        } else if (is_unreserved(c)) {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(to_hex_digit((c >> 4) & 0xF));
            encoded.push_back(to_hex_digit(c & 0xF));
        }
    }
    return encoded;
}

} // namespace cppl::lsp
