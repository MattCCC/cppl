#pragma once

// A minimal JSON value type, parser and serializer, scoped to exactly what
// JSON-RPC 2.0 framing over stdio needs (tools/cppl-lsp/main.cpp).
//
// This is deliberately not a general-purpose JSON library: it is a small,
// self-contained transport-layer utility, entirely outside the proof kernel
// and the C++L semantic pipeline (AGENTS.md 28). Reusing an external JSON
// library was considered; this repository has no existing JSON dependency
// and pulling one in via network fetch at configure time is avoided here so
// the build stays reproducible without a network dependency. The transport
// grammar LSP needs -- objects, arrays, strings, numbers, booleans, null --
// is small enough that a compact hand-written implementation is easier to
// audit than a large third-party dependency would be to vet.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::lsp::json {

class Value;

enum class Type : std::uint8_t {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

// Thrown by parse() on malformed input, and by the narrowing accessors
// (as_string(), etc.) when the value is not of the requested type. The
// server never lets this escape past request handling: malformed input is
// logged and the request/notification is dropped rather than crashing the
// process (tools/cppl-lsp/README.md would call this "fail closed").
class Error : public std::runtime_error {
  public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

// An ordered object: JSON-RPC does not require member order to be
// preserved, but keeping insertion order makes serialized output stable and
// easy to eyeball in logs/tests.
using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

class Value {
  public:
    Value() : type_(Type::Null) {}
    Value(std::nullptr_t) : type_(Type::Null) {}
    Value(bool value) : type_(Type::Boolean), bool_(value) {}
    Value(double value) : type_(Type::Number), number_(value) {}
    Value(int value) : type_(Type::Number), number_(value) {}
    Value(std::int64_t value) : type_(Type::Number), number_(static_cast<double>(value)) {}
    Value(std::uint32_t value) : type_(Type::Number), number_(static_cast<double>(value)) {}
    Value(std::string value) : type_(Type::String), string_(std::move(value)) {}
    Value(const char* value) : type_(Type::String), string_(value) {}
    Value(Array value) : type_(Type::Array), array_(std::make_shared<Array>(std::move(value))) {}
    Value(Object value) : type_(Type::Object), object_(std::make_shared<Object>(std::move(value))) {}

    [[nodiscard]] Type type() const noexcept {
        return type_;
    }
    [[nodiscard]] bool is_null() const noexcept {
        return type_ == Type::Null;
    }
    [[nodiscard]] bool is_string() const noexcept {
        return type_ == Type::String;
    }
    [[nodiscard]] bool is_number() const noexcept {
        return type_ == Type::Number;
    }
    [[nodiscard]] bool is_object() const noexcept {
        return type_ == Type::Object;
    }
    [[nodiscard]] bool is_array() const noexcept {
        return type_ == Type::Array;
    }
    [[nodiscard]] bool is_boolean() const noexcept {
        return type_ == Type::Boolean;
    }

    [[nodiscard]] const std::string& as_string() const {
        if (type_ != Type::String) {
            throw Error("expected a JSON string");
        }
        return string_;
    }
    [[nodiscard]] double as_number() const {
        if (type_ != Type::Number) {
            throw Error("expected a JSON number");
        }
        return number_;
    }
    [[nodiscard]] bool as_boolean() const {
        if (type_ != Type::Boolean) {
            throw Error("expected a JSON boolean");
        }
        return bool_;
    }
    [[nodiscard]] const Array& as_array() const {
        if (type_ != Type::Array) {
            throw Error("expected a JSON array");
        }
        return *array_;
    }
    [[nodiscard]] const Object& as_object() const {
        if (type_ != Type::Object) {
            throw Error("expected a JSON object");
        }
        return *object_;
    }

    // Object member lookup. Returns nullptr when this is not an object or
    // has no such member, so callers can handle a missing/optional field
    // instead of throwing (the LSP spec has many optional request fields).
    [[nodiscard]] const Value* find(const std::string& key) const noexcept {
        if (type_ != Type::Object) {
            return nullptr;
        }
        for (const auto& [member_key, member_value] : *object_) {
            if (member_key == key) {
                return &member_value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<std::string> find_string(const std::string& key) const {
        const Value* value = find(key);
        if (value == nullptr || !value->is_string()) {
            return std::nullopt;
        }
        return value->as_string();
    }

    [[nodiscard]] std::optional<double> find_number(const std::string& key) const {
        const Value* value = find(key);
        if (value == nullptr || !value->is_number()) {
            return std::nullopt;
        }
        return value->as_number();
    }

    static Value object() {
        return {Object{}};
    }
    static Value array() {
        return {Array{}};
    }

    // Mutating helpers for building a response/notification payload.
    void set(std::string key, Value value) {
        if (type_ != Type::Object) {
            type_ = Type::Object;
            object_ = std::make_shared<Object>();
        }
        object_->emplace_back(std::move(key), std::move(value));
    }
    void push_back(Value value) {
        if (type_ != Type::Array) {
            type_ = Type::Array;
            array_ = std::make_shared<Array>();
        }
        array_->push_back(std::move(value));
    }

    // Serializes to compact JSON text.
    [[nodiscard]] std::string dump() const;

  private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    // shared_ptr rather than a direct member: Value must be a complete type
    // to be stored in a vector/map of itself.
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

// Parses one JSON document from `text`. Throws Error on malformed input;
// the caller (the transport loop) always catches this.
[[nodiscard]] Value parse(std::string_view text);

} // namespace cppl::lsp::json
