// file:// URI <-> native path conversion must handle percent-encoding and
// Windows drive letters correctly, not a naive substr(7).

#include "cppl/lsp/uri.hpp"
#include "cppl/testing/test.hpp"

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
