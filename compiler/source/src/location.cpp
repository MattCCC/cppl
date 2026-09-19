#include "cppl/source/location.hpp"

namespace cppl::source {

std::string describe(const SourceLocation& location) {
    if (!location.is_valid()) {
        return "<unknown location>";
    }
    std::string text = location.file;
    text += ":";
    text += std::to_string(location.line);
    if (location.column != 0) {
        text += ":";
        text += std::to_string(location.column);
    }
    return text;
}

}  // namespace cppl::source
