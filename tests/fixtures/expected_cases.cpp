#include <cstdio>
#include <expected>
#include <optional>
#include <tuple>
#include <variant>

// 5. std::expected models its public semantics: two states, each with a
// payload. Nothing here depends on how a standard library lays it out.
proof expected_states(std::expected<unsigned, bool> e)
    proves (Eq<bool>(true, true))
{
    cases e {
        value(payload) => {
            refl;
        }

        error(reason) => {
            refl;
        }
    }
}

// The error payload is bound in its own arm, like any other case binding.
proof expected_error_payload(std::expected<int, int> e)
    proves (Eq<bool>(true, true))
{
    cases e {
        value(good) => {
            refl;
        }

        error(bad) => {
            refl;
        }
    }
}

// 33. Composition with the other tagged-sum providers.
proof expected_of_optional(std::expected<std::optional<int>, bool> e)
    proves (Eq<bool>(true, true))
{
    cases e {
        value(inner) => {
            cases inner {
                some(payload) => {
                    refl;
                }

                none => {
                    refl;
                }
            }
        }

        error(reason) => {
            refl;
        }
    }
}

proof optional_of_expected(std::optional<std::expected<int, bool>> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(inner) => {
            cases inner {
                value(good) => {
                    refl;
                }

                error(bad) => {
                    refl;
                }
            }
        }

        none => {
            refl;
        }
    }
}

// 33/35. Each binder is handed to a lemma that accepts exactly one type, so a
// value bound where the error belongs, or a component bound out of order, does
// not type-check. `expected_error_bound_as_value.cpp` in `fixtures/negative/`
// is the refused half.
struct Left {
    int l;
};

struct Right {
    bool r;
};

struct Fault {
    unsigned code;
};

proof is_left(Left x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof is_right(Right x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof is_fault(Fault x)
    proves (Eq<bool>(true, true))
{
    refl;
}

// 33. A sum inside a product.
proof tuple_of_expected(std::tuple<std::expected<Left, Fault>, Right> t)
    proves (Eq<bool>(true, true))
{
    decompose t {
        components(outcome, flag) => {
            cases outcome {
                value(good) => {
                    exact is_left(good);
                }

                error(bad) => {
                    exact is_fault(bad);
                }
            }
        }
    }
}

// 33. A variant as the value payload.
proof expected_of_variant(std::expected<std::variant<Left, Right>, Fault> e)
    proves (Eq<bool>(true, true))
{
    cases e {
        value(choice) => {
            cases choice {
                alternative<0>(left) => {
                    exact is_left(left);
                }

                alternative<1>(right) => {
                    exact is_right(right);
                }

                valueless => {
                    refl;
                }
            }
        }

        error(bad) => {
            exact is_fault(bad);
        }
    }
}

// 35. Aliases and alias templates denote the same std::expected.
using Result = std::expected<Left, Fault>;

template <typename T> using Outcome = std::expected<T, Fault>;

proof expected_alias(const Result& r)
    proves (Eq<bool>(true, true))
{
    cases r {
        value(good) => {
            exact is_left(good);
        }

        error(bad) => {
            exact is_fault(bad);
        }
    }
}

proof expected_template(Outcome<Right> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        value(good) => {
            exact is_right(good);
        }

        error(bad) => {
            exact is_fault(bad);
        }
    }
}

int main() {
    std::printf("%d\n", 5);
}
