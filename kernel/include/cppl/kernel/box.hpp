#pragma once

#include <memory>
#include <utility>

namespace cppl::kernel {

// Value-semantics indirection for recursive formal structures.
//
// Recursive core data (a proposition inside a quantifier, a proof term inside a
// rule) needs indirection, but pointer identity must never become semantic
// identity. Box copies deeply and compares structurally, so two independently
// constructed equal structures remain equal and no address is observable.
//
// A moved-from Box holds no value and must not be dereferenced.
template <typename T> class Box {
  public:
    explicit Box(T value) : value_(std::make_unique<T>(std::move(value))) {}

    Box(const Box& other) : value_(std::make_unique<T>(*other.value_)) {}

    Box& operator=(const Box& other) {
        if (this != &other) {
            value_ = std::make_unique<T>(*other.value_);
        }
        return *this;
    }

    Box(Box&&) noexcept = default;
    Box& operator=(Box&&) noexcept = default;
    ~Box() = default;

    const T& operator*() const noexcept {
        return *value_;
    }
    const T* operator->() const noexcept {
        return value_.get();
    }
    const T& get() const noexcept {
        return *value_;
    }

    friend bool operator==(const Box& lhs, const Box& rhs) {
        return *lhs.value_ == *rhs.value_;
    }

  private:
    std::unique_ptr<T> value_;
};

} // namespace cppl::kernel
