// file:// URI <-> native path conversion must handle percent-encoding and
// Windows drive letters correctly, not a naive substr(7).

#include "cppl/lsp/uri.hpp"
#include "cppl/testing/test.hpp"

#include <string>

using namespace cppl::lsp;

CPPL_TEST(uri_to_path_simple_unix_path) {
    auto path = uri_to_path("file:///Users/dev/project/main.cpp");
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(*path, "/Users/dev/project/main.cpp");
}

CPPL_TEST(uri_to_path_percent_encoded_space) {
    auto path = uri_to_path("file:///Users/dev/my%20project/main.cpp");
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(*path, "/Users/dev/my project/main.cpp");
}

CPPL_TEST(uri_to_path_windows_drive_letter) {
    auto path = uri_to_path("file:///C:/Users/dev/project/main.cpp");
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(*path, "C:/Users/dev/project/main.cpp");
}

CPPL_TEST(uri_to_path_windows_drive_with_encoded_space) {
    auto path = uri_to_path("file:///C:/Users/dev/my%20project/main.cpp");
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(*path, "C:/Users/dev/my project/main.cpp");
}

CPPL_TEST(uri_to_path_non_file_scheme_returns_nullopt) {
    auto path = uri_to_path("untitled:Untitled-1");
    CPPL_CHECK(!path.has_value());
}

CPPL_TEST(uri_to_path_malformed_percent_escape_returns_nullopt) {
    auto path = uri_to_path("file:///Users/dev/broken%zzpath");
    CPPL_CHECK(!path.has_value());
}

CPPL_TEST(path_to_uri_round_trips_simple_path) {
    std::string uri = path_to_uri("/Users/dev/project/main.cpp");
    auto path = uri_to_path(uri);
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(*path, "/Users/dev/project/main.cpp");
}

CPPL_TEST(path_to_uri_round_trips_space) {
    std::string uri = path_to_uri("/Users/dev/my project/main.cpp");
    auto path = uri_to_path(uri);
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(*path, "/Users/dev/my project/main.cpp");
}

CPPL_TEST(path_to_uri_windows_drive) {
    std::string uri = path_to_uri("C:/Users/dev/project/main.cpp");
    CPPL_CHECK_EQ(uri, "file:///C:/Users/dev/project/main.cpp");
}

// A C API stops at the first NUL, so "/safe.cppl\0/../../etc/passwd" would be
// checked as one path and opened as another.
CPPL_TEST(uri_to_path_refuses_an_encoded_nul) {
    CPPL_CHECK(!uri_to_path("file:///tmp/safe.cppl%00/../../etc/passwd").has_value());
    CPPL_CHECK(!uri_to_path("file:///tmp/a%00").has_value());
}

CPPL_TEST(uri_to_path_refuses_a_literal_nul) {
    CPPL_CHECK(!uri_to_path(std::string("file:///tmp/a\0b", 15)).has_value());
}

CPPL_TEST(uri_to_path_refuses_an_authority_without_a_path) {
    CPPL_CHECK(!uri_to_path("file://").has_value());
    CPPL_CHECK(!uri_to_path("file://host").has_value());
    // The encoded slash is part of the authority, not a path.
    CPPL_CHECK(!uri_to_path("file://a%2Fb").has_value());
}

CPPL_TEST(uri_to_path_drops_a_localhost_authority) {
    auto path = uri_to_path("file://localhost/tmp/main.cppl");
    CPPL_CHECK(path.has_value());
    CPPL_CHECK_EQ(path.value_or(""), "/tmp/main.cppl");
}

CPPL_TEST(path_to_uri_backslash_is_a_separator_only_on_windows) {
    const std::string uri = path_to_uri("/tmp/a\\b.cppl");
#ifdef _WIN32
    CPPL_CHECK_EQ(uri, "file:///tmp/a/b.cppl");
#else
    // An ordinary file-name byte: the URI names the same file again.
    CPPL_CHECK_EQ(uri, "file:///tmp/a%5Cb.cppl");
    CPPL_CHECK_EQ(uri_to_path(uri).value_or(""), "/tmp/a\\b.cppl");
#endif
}
