#include <cstdio>
#include <expected>
#include <optional>

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

int main() {
    std::printf("%d\n", 5);
}
