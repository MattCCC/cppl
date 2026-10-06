#pragma once

// Requests, edit checks and fixtures the formatter tests (formatter_test)
// are written with.

#include "cppl/diagnostics/diagnostic.hpp"
#include "cppl/formatter/format.hpp"
#include "cppl/frontend/syntax.hpp"
#include "cppl/frontend/token.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

namespace formatter_test_detail {

using namespace cppl;

[[noreturn]] inline void fail_test(const std::string& message) {
    ::cppl::testing::fail(__FILE__, __LINE__, message);
}

inline void check_edits_well_formed(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    std::size_t previous_end = 0;
    bool first = true;

    for (const formatter::FormatEdit& edit : edits) {
        if (edit.span.offset > text.size()) {
            fail_test("format edit starts past end of original input");
        }
        if (edit.span.length > text.size() - edit.span.offset) {
            fail_test("format edit extends past end of original input");
        }
        if (!first && edit.span.offset < previous_end) {
            fail_test("format edits are not sorted and non-overlapping");
        }

        previous_end = edit.span.offset + edit.span.length;
        first = false;
    }
}

inline std::string apply_edits(const std::string& text, const std::vector<formatter::FormatEdit>& edits) {
    check_edits_well_formed(text, edits);

    std::string result;
    std::size_t cursor = 0;
    for (const formatter::FormatEdit& edit : edits) {
        result.append(text, cursor, edit.span.offset - cursor);
        result += edit.replacement;
        cursor = edit.span.offset + edit.span.length;
    }
    result.append(text, cursor, text.size() - cursor);
    return result;
}

inline formatter::FormatRequest make_request(const std::string& text) {
    formatter::FormatRequest request;
    request.text = text;
    return request;
}

inline formatter::FormatResult document_result(const std::string& text) {
    formatter::FormatRequest request = make_request(text);
    formatter::FormatResult result = formatter::format_document(request);
    if (result.ok) {
        check_edits_well_formed(text, result.edits);
    }
    return result;
}

inline std::string format_text(const std::string& text) {
    const formatter::FormatResult result = document_result(text);
    CPPL_CHECK(result.ok);
    return apply_edits(text, result.edits);
}

inline std::string format_ranges_text(const std::string& text, const std::vector<source::ByteSpan>& ranges) {
    formatter::FormatRequest request = make_request(text);
    const formatter::FormatResult result = formatter::format_ranges(request, ranges);
    CPPL_CHECK(result.ok);
    check_edits_well_formed(text, result.edits);
    return apply_edits(text, result.edits);
}

inline std::string format_on_type_text(const std::string& text, std::size_t position, const std::string& trigger) {
    formatter::FormatRequest request = make_request(text);
    const formatter::FormatResult result = formatter::format_on_type(request, position, trigger);
    CPPL_CHECK(result.ok);
    check_edits_well_formed(text, result.edits);
    return apply_edits(text, result.edits);
}

inline std::vector<diagnostics::Diagnostic> style_diagnostics(const std::string& text) {
    const frontend::TokenStream tokens = frontend::lex(text, "style.cpp");
    diagnostics::Engine engine;
    const frontend::Syntax syntax = frontend::recognize(tokens, engine);
    return formatter::check_style(tokens, syntax);
}

inline bool has_style_message(const std::vector<diagnostics::Diagnostic>& diagnostics, const std::string& needle) {
    for (const diagnostics::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.category == diagnostics::Category::Style &&
            diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

inline std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        fail_test("could not read '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace formatter_test_detail
