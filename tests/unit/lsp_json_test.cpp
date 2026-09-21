// The minimal JSON value/parser/serializer used for JSON-RPC framing.

#include "cppl/lsp/json.hpp"
#include "cppl/testing/test.hpp"

using namespace cppl::lsp::json;

CPPL_TEST(parse_object_with_members) {
    Value value = parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
    CPPL_CHECK(value.is_object());
    CPPL_CHECK_EQ(value.find("jsonrpc")->as_string(), "2.0");
    CPPL_CHECK_EQ(value.find("id")->as_number(), 1.0);
    CPPL_CHECK_EQ(value.find("method")->as_string(), "initialize");
}

CPPL_TEST(parse_nested_array_and_object) {
    Value value = parse(R"({"params":{"items":[1,2,3],"ok":true,"nothing":null}})");
    const Value* params = value.find("params");
    CPPL_CHECK(params != nullptr);
    const Value* items = params->find("items");
    CPPL_CHECK(items != nullptr);
    CPPL_CHECK(items->is_array());
    CPPL_CHECK_EQ(items->as_array().size(), 3u);
    CPPL_CHECK(params->find("ok")->as_boolean());
    CPPL_CHECK(params->find("nothing")->is_null());
}

CPPL_TEST(parse_string_escapes) {
    Value value = parse(R"("line1\nline2\t\"quoted\"")");
    CPPL_CHECK_EQ(value.as_string(), "line1\nline2\t\"quoted\"");
}

CPPL_TEST(parse_unicode_escape) {
    Value value = parse(R"("café")");
    CPPL_CHECK_EQ(value.as_string(), "caf\xc3\xa9");
}

CPPL_TEST(parse_negative_and_fractional_numbers) {
    Value value = parse(R"([-1, 2.5, 1e2])");
    const Array& array = value.as_array();
    CPPL_CHECK_EQ(array[0].as_number(), -1.0);
    CPPL_CHECK_EQ(array[1].as_number(), 2.5);
    CPPL_CHECK_EQ(array[2].as_number(), 100.0);
}

CPPL_TEST(parse_malformed_json_throws) {
    bool threw = false;
    try {
        [[maybe_unused]] Value discarded = parse("{not valid json");
    } catch (const Error&) {
        threw = true;
    }
    CPPL_CHECK(threw);
}

CPPL_TEST(find_missing_key_returns_nullptr) {
    Value value = parse(R"({"a":1})");
    CPPL_CHECK(value.find("b") == nullptr);
}

CPPL_TEST(dump_round_trips_object) {
    Value object = Value::object();
    object.set("name", Value("cppl-lsp"));
    object.set("version", Value(1));
    object.set("ok", Value(true));

    std::string text = object.dump();
    Value parsed = parse(text);
    CPPL_CHECK_EQ(parsed.find("name")->as_string(), "cppl-lsp");
    CPPL_CHECK_EQ(parsed.find("version")->as_number(), 1.0);
    CPPL_CHECK(parsed.find("ok")->as_boolean());
}

CPPL_TEST(dump_escapes_special_characters) {
    Value value = Value(std::string("a\"b\\c\nd"));
    std::string text = value.dump();
    Value parsed = parse(text);
    CPPL_CHECK_EQ(parsed.as_string(), "a\"b\\c\nd");
}

CPPL_TEST(find_string_and_number_helpers) {
    Value value = parse(R"({"uri":"file:///a.cpp","version":3})");
    CPPL_CHECK_EQ(value.find_string("uri").value_or(""), "file:///a.cpp");
    CPPL_CHECK_EQ(value.find_number("version").value_or(-1), 3.0);
    CPPL_CHECK(!value.find_string("missing").has_value());
}
