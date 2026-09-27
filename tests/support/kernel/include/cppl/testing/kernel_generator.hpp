#pragma once

// Generates kernel inputs that are mostly valid derivations, with deliberate
// defects mixed in, from a stream of choices (docs/KERNEL.md, "Testing").
//
// Random proof terms are almost all rejected, and a rejection proves nothing
// about soundness. So the generator builds derivations bottom-up, one rule at a
// time, taking each premise either from a derivation of it or from a supposed
// hypothesis, and at any node it may break the step: a wrong argument, a wrong
// side, a swapped case, a restatement that differs, a claimed conclusion the
// step does not give. The kernel must reject every broken step whose claim is
// false; kernel_oracle checks whatever it accepts against kernel_model.
//
// The choices come from a fuzz input's bytes or from a seeded generator. Every
// choice has a terminating default (0), so an exhausted input still produces a
// finite sample, and the same choices always produce the same sample.

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cppl::testing::kernel_generator {

class Choices {
  public:
    Choices() = default;
    Choices(const Choices&) = delete;
    Choices& operator=(const Choices&) = delete;
    Choices(Choices&&) = delete;
    Choices& operator=(Choices&&) = delete;
    virtual ~Choices() = default;

    // A choice in [0, bound). `bound` is at least one.
    [[nodiscard]] virtual std::uint32_t below(std::uint32_t bound) = 0;

    // True with probability about 1 / `odds`.
    [[nodiscard]] bool one_in(std::uint32_t odds) {
        return below(odds) == 0;
    }
};

// The bytes of a fuzz input, consumed front to back. Once they run out every
// choice is 0.
class ByteChoices final : public Choices {
  public:
    explicit ByteChoices(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    [[nodiscard]] std::uint32_t below(std::uint32_t bound) override;

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

// A xorshift generator from a fixed seed.
class SeededChoices final : public Choices {
  public:
    explicit SeededChoices(std::uint64_t seed) : state_(seed == 0 ? 0x9e3779b97f4a7c15ULL : seed) {}
    [[nodiscard]] std::uint32_t below(std::uint32_t bound) override;

  private:
    std::uint64_t state_;
};

enum class Mode : std::uint8_t {
    // A derivation built rule by rule, possibly broken.
    Derivation,
    // Facts and a goal over machine arithmetic, closed by a certificate the
    // untrusted refutation search proposes (or by a corrupted one).
    Arithmetic,
    // A goal whose evidence the untrusted automation proposes.
    Automation,
};

inline constexpr std::uint32_t kModeCount = 3;

std::string describe(Mode mode);

struct Sample {
    kernel::Context context;
    kernel::Proposition goal;
    kernel::ProofTerm proof;
    Mode mode = Mode::Derivation;
};

[[nodiscard]] Sample generate(Choices& choices, Mode mode);

// A term and a proposition under some binders, and a term that may replace
// the innermost of them, for the properties of normalization, substitution and
// shifting.
struct TermSample {
    kernel::Context context;
    // Outermost first. Never empty.
    std::vector<kernel::Type> locals;
    // Well typed under `locals`.
    kernel::Term term;
    kernel::Proposition proposition;
    // Of the innermost binder's type, well typed under the others.
    kernel::Term argument;
};

[[nodiscard]] TermSample generate_term(Choices& choices);

// Raises every hypothesis index at or above `cutoff` by `amount`: the same
// evidence, placed under `amount` more premises than it was built under.
[[nodiscard]] kernel::ProofTerm lift(const kernel::ProofTerm& proof, std::uint32_t amount, std::uint32_t cutoff = 0);

} // namespace cppl::testing::kernel_generator
