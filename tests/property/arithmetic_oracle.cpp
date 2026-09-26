// Writes C++L programs for the arithmetic property test
// (e2e/arithmetic_properties.sh, SPEC.md ARITH-006 to ARITH-008).
//
// Each generated function performs one operation on values its precondition
// pins, and states the value the operation computes. Whether the operation is
// defined, and what it computes, is decided here by the host compiler itself:
// the promotions are the host's own (`decltype(+x)`), overflow is its checked
// builtins' verdict, and division is its own native division. Nothing is shared
// with the verifier, whose decision the test then compares: every function this
// oracle calls defined must verify and, run, return the stated value; every one
// it calls undefined must be refused with a definedness diagnostic.
//
// Operands are the edges of every type and values drawn from a seed, which is
// printed so a failure reproduces.
//
// usage: arithmetic_oracle <seed> <defined.cpp> <undefined.cpp> <undefined-names>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

template <typename T> std::string type_name() {
    if constexpr (std::is_same_v<T, signed char>) {
        return "signed char";
    } else if constexpr (std::is_same_v<T, unsigned char>) {
        return "unsigned char";
    } else if constexpr (std::is_same_v<T, short>) {
        return "short";
    } else if constexpr (std::is_same_v<T, unsigned short>) {
        return "unsigned short";
    } else if constexpr (std::is_same_v<T, int>) {
        return "int";
    } else if constexpr (std::is_same_v<T, unsigned>) {
        return "unsigned";
    } else if constexpr (std::is_same_v<T, long long>) {
        return "long long";
    } else {
        static_assert(std::is_same_v<T, unsigned long long>);
        return "unsigned long long";
    }
}

// An expression of exactly type `T` denoting `value`. A literal has no
// negative form, so the least value of a signed type is written as a
// difference, and a narrow type is reached by a cast from an `int` literal.
template <typename T> std::string literal(T value) {
    if constexpr (std::is_same_v<T, int>) {
        if (value == std::numeric_limits<int>::min()) {
            return "(-2147483647 - 1)";
        }
        return value < 0 ? "(" + std::to_string(value) + ")" : std::to_string(value);
    } else if constexpr (std::is_same_v<T, long long>) {
        if (value == std::numeric_limits<long long>::min()) {
            return "(-9223372036854775807LL - 1LL)";
        }
        return value < 0 ? "(" + std::to_string(value) + "LL)" : std::to_string(value) + "LL";
    } else if constexpr (std::is_same_v<T, unsigned>) {
        return std::to_string(value) + "u";
    } else if constexpr (std::is_same_v<T, unsigned long long>) {
        return std::to_string(value) + "ull";
    } else {
        return "static_cast<" + type_name<T>() + ">(" + literal(static_cast<int>(value)) + ")";
    }
}

template <typename T> std::vector<T> edges() {
    using Limits = std::numeric_limits<T>;
    std::vector<T> values{Limits::min(), static_cast<T>(Limits::min() + 1), 0, 1, static_cast<T>(Limits::max() - 1),
                          Limits::max()};
    if constexpr (std::is_signed_v<T>) {
        values.push_back(-1);
    }
    return values;
}

template <typename T> T drawn(Random& random) {
    const std::uint64_t bits = random.next();
    // Half the draws are small, where quotients and remainders vary most.
    if ((bits & 1u) == 0u) {
        const auto small = static_cast<long long>((bits >> 1) % 41u) - 20;
        return static_cast<T>(std::is_signed_v<T> ? small : (small < 0 ? -small : small));
    }
    return static_cast<T>(bits >> 1);
}

struct Result {
    bool defined = false;
    std::string value; // a literal of the result type, when defined
};

// C++'s verdict on `a op b` and its value, from the host.
template <typename T> Result evaluate(char op, T a, T b) {
    using P = decltype(+a);
    const P x = +a;
    const P y = +b;
    P result = 0;
    if (op == '+' || op == '-' || op == '*') {
        if constexpr (std::is_signed_v<P>) {
            const bool overflowed = op == '+'   ? __builtin_add_overflow(x, y, &result)
                                    : op == '-' ? __builtin_sub_overflow(x, y, &result)
                                                : __builtin_mul_overflow(x, y, &result);
            if (overflowed) {
                return {};
            }
        } else {
            result = op == '+' ? static_cast<P>(x + y) : op == '-' ? static_cast<P>(x - y) : static_cast<P>(x * y);
        }
        return {true, literal(result)};
    }
    if (y == 0) {
        return {};
    }
    if constexpr (std::is_signed_v<P>) {
        if (x == std::numeric_limits<P>::min() && y == -1) {
            return {};
        }
    }
    result = op == '/' ? static_cast<P>(x / y) : static_cast<P>(x % y);
    return {true, literal(result)};
}

template <typename T> Result negate(T a) {
    using P = decltype(+a);
    const P x = +a;
    if constexpr (std::is_signed_v<P>) {
        if (x == std::numeric_limits<P>::min()) {
            return {};
        }
        return {true, literal(static_cast<P>(-x))};
    } else {
        return {true, literal(static_cast<P>(P{0} - x))};
    }
}

// A conversion to `To` is defined for every value when `To` is unsigned, and
// otherwise, as C++L requires in every mode, only for a value it holds.
template <typename To, typename From> Result convert(From value) {
    if constexpr (std::is_signed_v<To>) {
        const bool fits =
            std::is_signed_v<From>
                ? static_cast<long long>(value) >= static_cast<long long>(std::numeric_limits<To>::min()) &&
                      static_cast<long long>(value) <= static_cast<long long>(std::numeric_limits<To>::max())
                : static_cast<unsigned long long>(value) <=
                      static_cast<unsigned long long>(std::numeric_limits<To>::max());
        if (!fits) {
            return {};
        }
    }
    return {true, literal(static_cast<To>(value))};
}

class Writer {
  public:
    std::ostringstream defined;
    std::ostringstream undefined;
    std::ostringstream names;
    std::ostringstream calls;
    std::size_t defined_count = 0;
    std::size_t undefined_count = 0;

    // One function: its parameters, its pins, and the operation it returns.
    void function(const std::string& result_type, const std::vector<std::pair<std::string, std::string>>& parameters,
                  const std::vector<std::string>& pins, const std::string& operation, const Result& outcome,
                  bool bounds) {
        std::ostringstream signature;
        std::ostringstream pinned;
        std::ostringstream arguments;
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto& [type, name] = parameters[index];
            if (index != 0) {
                signature << ", ";
                arguments << ", ";
                pinned << " && ";
            }
            signature << type << " " << name;
            arguments << pins[index];
            // An equality is rewritten away; order facts reach linear
            // arithmetic, so half the functions are pinned each way.
            if (bounds) {
                pinned << name << " >= " << pins[index] << " && " << name << " <= " << pins[index];
            } else {
                pinned << name << " == " << pins[index];
            }
        }
        std::ostringstream& out = outcome.defined ? defined : undefined;
        const std::string name =
            outcome.defined ? "d" + std::to_string(defined_count++) : "u" + std::to_string(undefined_count++);
        out << "verified " << result_type << " " << name << "(" << signature.str() << ")\n"
            << "    expects (" << pinned.str() << ")\n"
            << "    ensures (result == " << (outcome.defined ? outcome.value : "result") << ")\n"
            << "{\n    return " << operation << ";\n}\n\n";
        if (outcome.defined) {
            calls << "    failures += " << name << "(" << arguments.str() << ") == " << outcome.value << " ? 0 : 1;\n";
        } else {
            names << name << "\n";
        }
    }
};

template <typename T> void binary_cases(Writer& writer, Random& random) {
    using P = decltype(+T{});
    std::vector<std::pair<T, T>> pairs;
    for (const T a : edges<T>()) {
        for (const T b : edges<T>()) {
            pairs.emplace_back(a, b);
        }
    }
    for (int index = 0; index < 8; ++index) {
        pairs.emplace_back(drawn<T>(random), drawn<T>(random));
    }
    bool bounds = false;
    for (const char op : {'+', '-', '*', '/', '%'}) {
        for (const auto& [a, b] : pairs) {
            // A product, quotient or remainder of two operands pinned only by
            // order facts is not linear: linear arithmetic decides a product
            // only where the operand types bound it, and a division only by a
            // constant (RFC 0019). Their operands are pinned by equality,
            // which rewriting substitutes, so the host's value is what is
            // compared.
            writer.function(type_name<P>(), {{type_name<T>(), "a"}, {type_name<T>(), "b"}}, {literal(a), literal(b)},
                            std::string("a ") + op + " b", evaluate(op, a, b), bounds && (op == '+' || op == '-'));
            bounds = !bounds;
        }
    }
    for (const T a : edges<T>()) {
        writer.function(type_name<P>(), {{type_name<T>(), "a"}}, {literal(a)}, "-a", negate(a), bounds);
        bounds = !bounds;
    }
}

template <typename From, typename To> void conversion_cases(Writer& writer, Random& random) {
    std::vector<From> values = edges<From>();
    for (int index = 0; index < 3; ++index) {
        values.push_back(drawn<From>(random));
    }
    // Values at the edges of the target, as the source holds them (an
    // unsigned source holds a negative edge reduced).
    for (const To edge : edges<To>()) {
        values.push_back(static_cast<From>(edge));
    }
    bool bounds = false;
    for (const From value : values) {
        writer.function(type_name<To>(), {{type_name<From>(), "a"}}, {literal(value)}, "a", convert<To>(value), bounds);
        bounds = !bounds;
    }
}

template <typename From> void conversions_from(Writer& writer, Random& random) {
    conversion_cases<From, signed char>(writer, random);
    conversion_cases<From, unsigned char>(writer, random);
    conversion_cases<From, short>(writer, random);
    conversion_cases<From, unsigned short>(writer, random);
    conversion_cases<From, int>(writer, random);
    conversion_cases<From, unsigned>(writer, random);
    conversion_cases<From, long long>(writer, random);
    conversion_cases<From, unsigned long long>(writer, random);
}

int generate(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: arithmetic_oracle <seed> <defined.cpp> <undefined.cpp> <undefined-names>\n";
        return 2;
    }
    const std::uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    std::cout << "arithmetic oracle seed " << seed << "\n";
    Random random{seed == 0 ? 1u : seed};
    Writer writer;
    binary_cases<signed char>(writer, random);
    binary_cases<unsigned char>(writer, random);
    binary_cases<short>(writer, random);
    binary_cases<unsigned short>(writer, random);
    binary_cases<int>(writer, random);
    binary_cases<unsigned>(writer, random);
    binary_cases<long long>(writer, random);
    binary_cases<unsigned long long>(writer, random);
    conversions_from<int>(writer, random);
    conversions_from<unsigned>(writer, random);
    conversions_from<long long>(writer, random);
    conversions_from<unsigned long long>(writer, random);

    std::ofstream defined(argv[2]);
    defined << "// Generated by arithmetic_oracle, seed " << seed << ". Every operation here is defined.\n\n"
            << writer.defined.str() << "int main() {\n    int failures = 0;\n"
            << writer.calls.str() << "    return failures == 0 ? 0 : 1;\n}\n";
    std::ofstream undefined(argv[3]);
    undefined << "// Generated by arithmetic_oracle, seed " << seed
              << ". Every operation here is undefined, or converts a value its target does not hold.\n\n"
              << writer.undefined.str() << "int main() {\n    return 0;\n}\n";
    std::ofstream names(argv[4]);
    names << writer.names.str();
    std::cout << writer.defined_count << " defined and " << writer.undefined_count << " undefined operations\n";
    return defined && undefined && names ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return generate(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "arithmetic_oracle: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "arithmetic_oracle: unknown exception\n";
    }
    return 1;
}
