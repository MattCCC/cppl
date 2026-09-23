// Content-Length framing against a hostile stream. The editor process is on
// the other end of the language server's standard input, and nothing it
// writes may cost more memory than it actually sends, or be read as a length
// it did not state (tests/fuzz/lsp_message.cpp searches for more).

#include "cppl/lsp/json.hpp"
#include "cppl/lsp/transport.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>

using namespace cppl::lsp;

namespace {

// Reads one message, reporting a refusal as nullopt plus `refused`.
std::optional<std::string> read(const std::string& stream, bool& refused) {
    std::istringstream input(stream);
    refused = false;
    try {
        return read_message(input);
    } catch (const json::Error&) {
        refused = true;
        return std::nullopt;
    }
}

bool refuses(const std::string& stream) {
    bool refused = false;
    static_cast<void>(read(stream, refused));
    return refused;
}

} // namespace

CPPL_TEST(a_length_over_the_limit_is_refused_before_anything_is_allocated) {
    CPPL_CHECK(refuses("Content-Length: 67108865\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length: 18446744073709551615\r\n\r\n{}"));
}

CPPL_TEST(a_length_past_the_integer_range_is_refused_not_wrapped) {
    CPPL_CHECK(refuses("Content-Length: 18446744073709551616\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length: 99999999999999999999999\r\n\r\n{}"));
}

CPPL_TEST(a_negative_length_is_refused_not_read_as_a_huge_one) {
    CPPL_CHECK(refuses("Content-Length: -1\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length: +2\r\n\r\n{}"));
}

CPPL_TEST(a_length_with_anything_but_digits_is_refused) {
    CPPL_CHECK(refuses("Content-Length: 2abc\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length: 0x2\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length: 2 2\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length:\r\n\r\n{}"));
}

CPPL_TEST(a_second_content_length_is_refused_rather_than_either_one_believed) {
    CPPL_CHECK(refuses("Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}"));
    CPPL_CHECK(refuses("Content-Length: 2\r\nContent-Length: 40\r\n\r\n{}"));
}

CPPL_TEST(a_header_line_longer_than_the_limit_is_refused) {
    CPPL_CHECK(refuses("X-Padding: " + std::string(8192, 'a') + "\r\nContent-Length: 2\r\n\r\n{}"));
    CPPL_CHECK(refuses(std::string(100000, 'a')));
}

CPPL_TEST(a_header_line_at_the_limit_is_read) {
    bool refused = false;
    // 8192 bytes before the line terminator, "\r" included.
    const auto body = read("X: " + std::string(8188, 'a') + "\r\nContent-Length: 2\r\n\r\n{}", refused);
    CPPL_CHECK(!refused);
    CPPL_CHECK(body.has_value());
    CPPL_CHECK_EQ(body.value_or(""), "{}");
}

CPPL_TEST(the_limit_itself_is_a_length_that_can_be_stated) {
    bool refused = false;
    // Stated but never sent: truncated, and not refused.
    const auto body = read("Content-Length: 67108864\r\n\r\n{}", refused);
    CPPL_CHECK(!refused);
    CPPL_CHECK(!body.has_value());
}

CPPL_TEST(blanks_around_the_length_are_allowed) {
    bool refused = false;
    const auto body = read("Content-Length: \t 2 \t\r\n\r\n{}", refused);
    CPPL_CHECK(!refused);
    CPPL_CHECK_EQ(body.value_or(""), "{}");
}

CPPL_TEST(an_empty_body_is_a_message) {
    bool refused = false;
    const auto body = read("Content-Length: 0\r\n\r\n", refused);
    CPPL_CHECK(!refused);
    CPPL_CHECK(body.has_value());
    CPPL_CHECK_EQ(body.value_or("x"), "");
}

CPPL_TEST(a_body_longer_than_one_read_arrives_whole) {
    const std::string payload(200000, 'p');
    bool refused = false;
    const auto body = read("Content-Length: 200000\r\n\r\n" + payload, refused);
    CPPL_CHECK(!refused);
    CPPL_CHECK(body == payload);
}

CPPL_TEST(a_body_cut_short_after_several_reads_is_truncated) {
    bool refused = false;
    const auto body = read("Content-Length: 200001\r\n\r\n" + std::string(200000, 'p'), refused);
    CPPL_CHECK(!refused);
    CPPL_CHECK(!body.has_value());
}

CPPL_TEST(consecutive_messages_are_read_in_turn) {
    std::istringstream input("Content-Length: 2\r\n\r\n{}Content-Type: x\nContent-Length: 3\n\n[1]");
    CPPL_CHECK_EQ(read_message(input).value_or(""), "{}");
    CPPL_CHECK_EQ(read_message(input).value_or(""), "[1]");
    CPPL_CHECK(!read_message(input).has_value());
}
