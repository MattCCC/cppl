#include "cppl/driver/dependencies.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::driver {

namespace {

bool blank(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

// The length of the line break at `position`, or 0 when there is none there.
std::size_t line_break(std::string_view text, std::size_t position) {
    if (position < text.size() && text[position] == '\n') {
        return 1;
    }
    if (position + 1 < text.size() && text[position] == '\r' && text[position + 1] == '\n') {
        return 2;
    }
    return 0;
}

} // namespace

std::expected<std::vector<std::string>, std::string> make_prerequisites(std::string_view text,
                                                                        std::string_view target) {
    if (!text.starts_with(target) || text.size() == target.size() || text[target.size()] != ':') {
        return std::unexpected("it is not a rule for '" + std::string(target) + "'");
    }

    std::vector<std::string> files;
    std::string name;
    bool in_name = false;
    const auto finish = [&files, &name, &in_name] {
        if (in_name) {
            files.push_back(std::move(name));
            name.clear();
            in_name = false;
        }
    };

    std::size_t position = target.size() + 1;
    while (position < text.size()) {
        const char character = text[position];
        if (character == '\\') {
            std::size_t run = 0;
            while (position + run < text.size() && text[position + run] == '\\') {
                ++run;
            }
            const std::size_t after = position + run;
            const char next = after < text.size() ? text[after] : '\0';
            if (const std::size_t joined = line_break(text, after); joined != 0) {
                // The last backslash joins the lines; any before it are part
                // of a name.
                name.append(run - 1, '\\');
                in_name = in_name || run > 1;
                finish();
                position = after + joined;
                continue;
            }
            if (next == ' ') {
                // Clang doubles the backslashes before a space it escapes, so
                // an odd run ends in an escaped space, and an even one is a
                // name's own backslashes before a separator.
                name.append(run / 2, '\\');
                in_name = true;
                if (run % 2 == 1) {
                    name.push_back(' ');
                    position = after + 1;
                } else {
                    position = after;
                }
                continue;
            }
            if (next == '#') {
                name.append(run - 1, '\\');
                name.push_back('#');
                in_name = true;
                position = after + 1;
                continue;
            }
            name.append(run, '\\');
            in_name = true;
            position = after;
            continue;
        }
        if (character == '$' && position + 1 < text.size() && text[position + 1] == '$') {
            name.push_back('$');
            in_name = true;
            position += 2;
            continue;
        }
        if (const std::size_t ended = line_break(text, position); ended != 0) {
            finish();
            // The rule ends here, and nothing may follow it.
            for (std::size_t rest = position + ended; rest < text.size(); ++rest) {
                if (!blank(text[rest])) {
                    return std::unexpected("it holds more than the one rule for '" + std::string(target) + "'");
                }
            }
            return files;
        }
        if (blank(character)) {
            finish();
            ++position;
            continue;
        }
        name.push_back(character);
        in_name = true;
        ++position;
    }
    finish();
    return files;
}

} // namespace cppl::driver
