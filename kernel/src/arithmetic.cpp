#include "cppl/kernel/arithmetic.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace cppl::kernel {

namespace {

std::unexpected<CoreError> fail(CoreErrorKind kind, std::string detail) {
    return std::unexpected(CoreError{kind, std::move(detail)});
}

std::uint64_t mask(const IntType& type) {
    return type.width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << type.width) - 1u;
}

// 2^(width-1): the residues above it are the negative ones.
std::uint64_t half(const IntType& type) {
    return std::uint64_t{1} << (type.width - 1u);
}

Term literal_of(const IntType& type, std::uint64_t bits) {
    return Term::literal(type, wrap_into(type, static_cast<Wide>(bits)));
}

Wide lowest(const IntType& type) {
    return type.signedness == Signedness::Signed ? -Wide{half(type)} : Wide{0};
}

Wide highest(const IntType& type) {
    return type.signedness == Signedness::Signed ? Wide{half(type)} - 1 : Wide{mask(type)};
}

std::strong_ordering compare_types(const IntType& lhs, const IntType& rhs) {
    if (const auto order = lhs.width <=> rhs.width; order != 0) {
        return order;
    }
    return static_cast<std::uint8_t>(lhs.signedness) <=> static_cast<std::uint8_t>(rhs.signedness);
}

std::strong_ordering compare_lists(const std::vector<Term>& lhs, const std::vector<Term>& rhs) {
    const std::size_t common = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < common; ++index) {
        if (const auto order = compare(lhs[index], rhs[index]); order != 0) {
            return order;
        }
    }
    return lhs.size() <=> rhs.size();
}

// Monomials are ordered by degree, then by their factors.
struct FactorOrder {
    bool operator()(const std::vector<Term>& lhs, const std::vector<Term>& rhs) const {
        if (lhs.size() != rhs.size()) {
            return lhs.size() < rhs.size();
        }
        return compare_lists(lhs, rhs) < 0;
    }
};

// A polynomial while it is being built: distinct monomials keyed by their
// factors, so like terms combine as they are added.
struct Work {
    IntType type;
    std::map<std::vector<Term>, std::uint64_t, FactorOrder> terms;
    std::uint64_t constant = 0;
};

class Reader {
  public:
    Reader(IntType type, const CoreLimits& limits, std::uint64_t& steps)
        : type_(type),
          limits_(limits),
          steps_(steps) {}

    std::expected<Work, CoreError> read(const Term& term, std::uint32_t depth) {
        if (depth > limits_.max_term_depth) {
            return fail(CoreErrorKind::DepthLimitExceeded, "arithmetic nests deeper than the core allows");
        }
        if (auto spent = spend(1); !spent) {
            return std::unexpected(spent.error());
        }

        Work work{type_, {}, 0};
        if (const auto* literal = std::get_if<Literal>(&term.node); literal != nullptr && literal->type == type_) {
            work.constant = bits_of(type_, literal->value);
            return work;
        }
        if (const auto* primitive = std::get_if<Prim>(&term.node);
            primitive != nullptr && is_arithmetic(primitive->op) && primitive->type == type_ &&
            primitive->arguments.size() == 2) {
            auto lhs = read(primitive->arguments[0], depth + 1);
            if (!lhs) {
                return lhs;
            }
            auto rhs = read(primitive->arguments[1], depth + 1);
            if (!rhs) {
                return rhs;
            }
            switch (primitive->op) {
                case PrimOp::AddWrap:
                    return sum(std::move(*lhs), *rhs, 1u);
                case PrimOp::SubWrap:
                    // Subtracting is adding the additive inverse, -1 * rhs.
                    return sum(std::move(*lhs), *rhs, mask(type_));
                default:
                    return product(*lhs, *rhs);
            }
        }
        // Anything else of this type is opaque: a factor of degree one.
        work.terms.emplace(std::vector<Term>{term}, 1u);
        return work;
    }

    std::expected<Work, CoreError> sum(Work into, const Work& added, std::uint64_t scale) {
        for (const auto& [factors, coefficient] : added.terms) {
            if (auto added_term = accumulate(into, factors, coefficient * scale); !added_term) {
                return std::unexpected(added_term.error());
            }
        }
        into.constant = (into.constant + added.constant * scale) & mask(type_);
        return into;
    }

    std::expected<Work, CoreError> product(const Work& lhs, const Work& rhs) {
        Work result{type_, {}, 0};
        const auto each = [](const Work& work) {
            std::vector<std::pair<std::vector<Term>, std::uint64_t>> terms(work.terms.begin(), work.terms.end());
            if (work.constant != 0) {
                terms.emplace_back(std::vector<Term>{}, work.constant);
            }
            return terms;
        };
        const auto left = each(lhs);
        const auto right = each(rhs);
        for (const auto& [left_factors, left_coefficient] : left) {
            for (const auto& [right_factors, right_coefficient] : right) {
                if (auto spent = spend(1); !spent) {
                    return std::unexpected(spent.error());
                }
                if (left_factors.size() + right_factors.size() > limits_.max_monomial_degree) {
                    return fail(CoreErrorKind::NormalizationBudgetExhausted,
                                "a monomial has more than " + std::to_string(limits_.max_monomial_degree) + " factors");
                }
                std::vector<Term> factors;
                factors.reserve(left_factors.size() + right_factors.size());
                std::ranges::merge(left_factors, right_factors, std::back_inserter(factors),
                                   [](const Term& a, const Term& b) { return compare(a, b) < 0; });
                if (auto added = accumulate(result, factors, left_coefficient * right_coefficient); !added) {
                    return std::unexpected(added.error());
                }
            }
        }
        return result;
    }

  private:
    std::expected<void, CoreError> spend(std::uint64_t amount) {
        steps_ += amount;
        if (steps_ > limits_.max_normalization_steps) {
            return fail(CoreErrorKind::NormalizationBudgetExhausted, "normalization exceeded its step budget");
        }
        return {};
    }

    std::expected<void, CoreError> accumulate(Work& into, const std::vector<Term>& factors, std::uint64_t coefficient) {
        coefficient &= mask(type_);
        if (coefficient == 0) {
            return {};
        }
        if (factors.empty()) {
            into.constant = (into.constant + coefficient) & mask(type_);
            return {};
        }
        auto [entry, inserted] = into.terms.try_emplace(factors, 0u);
        entry->second = (entry->second + coefficient) & mask(type_);
        if (entry->second == 0) {
            into.terms.erase(entry);
        }
        if (into.terms.size() > limits_.max_polynomial_terms) {
            return fail(CoreErrorKind::NormalizationBudgetExhausted,
                        "a polynomial has more than " + std::to_string(limits_.max_polynomial_terms) + " terms");
        }
        return {};
    }

    IntType type_;
    const CoreLimits& limits_;
    std::uint64_t& steps_;
};

Polynomial finish(const Work& work) {
    Polynomial polynomial{work.type, {}, work.constant};
    polynomial.monomials.reserve(work.terms.size());
    for (const auto& [factors, coefficient] : work.terms) {
        polynomial.monomials.push_back(Monomial{factors, coefficient});
    }
    return polynomial;
}

Polynomial negated(const Polynomial& polynomial) {
    Polynomial result = polynomial;
    const std::uint64_t modulus_mask = mask(polynomial.type);
    for (Monomial& monomial : result.monomials) {
        monomial.coefficient = (~monomial.coefficient + 1u) & modulus_mask;
    }
    result.constant = (~result.constant + 1u) & modulus_mask;
    return result;
}

Term negate(Term condition) {
    if (const auto* literal = std::get_if<Literal>(&condition.node)) {
        return Term::literal(kBoolean, literal->value == 0 ? 1 : 0);
    }
    if (const auto* primitive = std::get_if<Prim>(&condition.node);
        primitive != nullptr && primitive->op == PrimOp::Not && primitive->arguments.size() == 1) {
        return primitive->arguments[0];
    }
    return Term::primitive(PrimOp::Not, kBoolean, {std::move(condition)});
}

// Canonical `lhs < rhs`. Order is not cancellative under wrapping, so no
// arithmetic is moved across it; it is decided only where the machine type
// decides it: between literals, between identical terms, and at the bounds of
// the type.
Term less(const IntType& type, Term lhs, Term rhs) {
    const auto* left = std::get_if<Literal>(&lhs.node);
    const auto* right = std::get_if<Literal>(&rhs.node);
    const auto value = [&type](const Literal& literal) {
        return value_of(type, bits_of(type, literal.value));
    };
    if (left != nullptr && right != nullptr) {
        return Term::literal(kBoolean, value(*left) < value(*right) ? 1 : 0);
    }
    if (lhs == rhs) {
        return Term::literal(kBoolean, 0);
    }
    if (right != nullptr && value(*right) == lowest(type)) {
        return Term::literal(kBoolean, 0);
    }
    if (left != nullptr && value(*left) == highest(type)) {
        return Term::literal(kBoolean, 0);
    }
    return Term::primitive(PrimOp::Less, type, {std::move(lhs), std::move(rhs)});
}

} // namespace

std::strong_ordering compare(const Term& lhs, const Term& rhs) {
    if (const auto order = lhs.node.index() <=> rhs.node.index(); order != 0) {
        return order;
    }
    return std::visit(
        [&rhs](const auto& left) -> std::strong_ordering {
            using Node = std::decay_t<decltype(left)>;
            const auto& right = std::get<Node>(rhs.node);
            if constexpr (std::is_same_v<Node, Var>) {
                return left.index.value <=> right.index.value;
            } else if constexpr (std::is_same_v<Node, Literal>) {
                if (const auto order = compare_types(left.type, right.type); order != 0) {
                    return order;
                }
                return left.value <=> right.value;
            } else if constexpr (std::is_same_v<Node, Call>) {
                if (const auto order = left.callee.value <=> right.callee.value; order != 0) {
                    return order;
                }
                return compare_lists(left.arguments, right.arguments);
            } else if constexpr (std::is_same_v<Node, Projection>) {
                if (const auto order = describe(left.domain) <=> describe(right.domain); order != 0)
                    return order;
                if (const auto order = left.index <=> right.index; order != 0)
                    return order;
                return compare_lists(left.arguments, right.arguments);
            } else if constexpr (std::is_same_v<Node, Element>) {
                // The index is an argument, so two observations of one array
                // order by their index terms and are equal only when those
                // terms are identical. Nothing here decides whether two
                // different index terms denote the same element.
                if (const auto order = describe(left.domain) <=> describe(right.domain); order != 0)
                    return order;
                return compare_lists(left.arguments, right.arguments);
            } else {
                if (const auto order = static_cast<std::uint8_t>(left.op) <=> static_cast<std::uint8_t>(right.op);
                    order != 0) {
                    return order;
                }
                if (const auto order = compare_types(left.type, right.type); order != 0) {
                    return order;
                }
                return compare_lists(left.arguments, right.arguments);
            }
        },
        lhs.node);
}

Wide value_of(const IntType& type, std::uint64_t bits) {
    bits &= mask(type);
    if (type.signedness == Signedness::Signed && bits >= half(type)) {
        return Wide{bits} - (Wide{1} << type.width);
    }
    return Wide{bits};
}

std::uint64_t bits_of(const IntType& type, Wide value) {
    return static_cast<std::uint64_t>(static_cast<WideUnsigned>(value)) & mask(type);
}

std::expected<Polynomial, CoreError> polynomial(const Term& normal, IntType type, const CoreLimits& limits) {
    if (!is_supported(type)) {
        return fail(CoreErrorKind::MalformedType, "polynomial over an unsupported integer type");
    }
    std::uint64_t steps = 0;
    Reader reader(type, limits, steps);
    auto work = reader.read(normal, 0);
    if (!work) {
        return std::unexpected(work.error());
    }
    return finish(*work);
}

Term render_factors(const IntType& type, const std::vector<Term>& factors) {
    Term product = factors.front();
    for (std::size_t index = 1; index < factors.size(); ++index) {
        product = Term::primitive(PrimOp::MulWrap, type, {std::move(product), factors[index]});
    }
    return product;
}

Term render(const Polynomial& polynomial) {
    const IntType& type = polynomial.type;
    const auto negative = [&type](std::uint64_t coefficient) {
        return coefficient > half(type);
    };
    const auto magnitude = [&type](std::uint64_t coefficient) {
        return (~coefficient + 1u) & mask(type);
    };
    const auto scaled = [&type](const std::vector<Term>& factors, std::uint64_t coefficient) {
        Term product = render_factors(type, factors);
        if (coefficient == 1u) {
            return product;
        }
        return Term::primitive(PrimOp::MulWrap, type, {literal_of(type, coefficient), std::move(product)});
    };

    // Terms with a positive coefficient are added, in monomial order; then the
    // others are subtracted, in monomial order; the constant comes last. The
    // shape is a function of the polynomial alone.
    std::optional<Term> sum;
    for (const Monomial& monomial : polynomial.monomials) {
        if (negative(monomial.coefficient)) {
            continue;
        }
        Term term = scaled(monomial.factors, monomial.coefficient);
        sum = sum ? Term::primitive(PrimOp::AddWrap, type, {std::move(*sum), std::move(term)}) : std::move(term);
    }
    for (const Monomial& monomial : polynomial.monomials) {
        if (!negative(monomial.coefficient)) {
            continue;
        }
        Term term = scaled(monomial.factors, magnitude(monomial.coefficient));
        Term minuend = sum ? std::move(*sum) : literal_of(type, 0u);
        sum = Term::primitive(PrimOp::SubWrap, type, {std::move(minuend), std::move(term)});
    }
    if (polynomial.constant != 0u) {
        if (!sum) {
            return literal_of(type, polynomial.constant);
        }
        if (negative(polynomial.constant)) {
            return Term::primitive(PrimOp::SubWrap, type,
                                   {std::move(*sum), literal_of(type, magnitude(polynomial.constant))});
        }
        return Term::primitive(PrimOp::AddWrap, type, {std::move(*sum), literal_of(type, polynomial.constant)});
    }
    return sum ? std::move(*sum) : literal_of(type, 0u);
}

std::expected<Term, CoreError> normalize_primitive(PrimOp op, IntType type, std::vector<Term> operands,
                                                   const CoreLimits& limits, std::uint64_t& steps) {
    const std::size_t arity = op == PrimOp::Select ? 3u : op == PrimOp::Not ? 1u : 2u;
    if (operands.size() != arity || !is_supported(type)) {
        // Only well-typed terms reach normalization through the checker; a
        // malformed one is left as it stands, which folds nothing.
        return Term::primitive(op, type, std::move(operands));
    }

    if (is_arithmetic(op)) {
        Reader reader(type, limits, steps);
        auto work = reader.read(Term::primitive(op, type, std::move(operands)), 0);
        if (!work) {
            return std::unexpected(work.error());
        }
        return render(finish(*work));
    }

    switch (op) {
        case PrimOp::Not:
            return negate(std::move(operands[0]));

        case PrimOp::Select: {
            if (const auto* condition = std::get_if<Literal>(&operands[0].node);
                condition != nullptr && condition->type == kBoolean &&
                (condition->value == 0 || condition->value == 1)) {
                return std::move(operands[condition->value == 1 ? 1 : 2]);
            }
            if (operands[1] == operands[2]) {
                return std::move(operands[1]);
            }
            if (const auto* inverted = std::get_if<Prim>(&operands[0].node);
                inverted != nullptr && inverted->op == PrimOp::Not && inverted->arguments.size() == 1) {
                return Term::primitive(PrimOp::Select, type,
                                       {inverted->arguments[0], std::move(operands[2]), std::move(operands[1])});
            }
            return Term::primitive(PrimOp::Select, type, std::move(operands));
        }

        case PrimOp::Equal:
        case PrimOp::NotEqual: {
            // a == b exactly when a - b is zero in the ring, so the comparison
            // is stated of the difference. Of the difference and its negation,
            // which state the same comparison, the lesser in term order is kept.
            Reader reader(type, limits, steps);
            auto lhs = reader.read(operands[0], 0);
            if (!lhs) {
                return std::unexpected(lhs.error());
            }
            auto rhs = reader.read(operands[1], 0);
            if (!rhs) {
                return std::unexpected(rhs.error());
            }
            auto difference = reader.sum(std::move(*lhs), *rhs, mask(type));
            if (!difference) {
                return std::unexpected(difference.error());
            }
            const Polynomial polynomial = finish(*difference);
            Term equal = Term::literal(kBoolean, polynomial.constant == 0 ? 1 : 0);
            if (!polynomial.monomials.empty()) {
                Term positive = render(polynomial);
                Term opposite = render(negated(polynomial));
                equal = Term::primitive(PrimOp::Equal, type,
                                        {compare(positive, opposite) <= 0 ? std::move(positive) : std::move(opposite),
                                         literal_of(type, 0u)});
            }
            return op == PrimOp::Equal ? equal : negate(std::move(equal));
        }

        case PrimOp::Less:
            return less(type, std::move(operands[0]), std::move(operands[1]));
        case PrimOp::Greater:
            return less(type, std::move(operands[1]), std::move(operands[0]));
        case PrimOp::LessEqual:
            return negate(less(type, std::move(operands[1]), std::move(operands[0])));
        case PrimOp::GreaterEqual:
            return negate(less(type, std::move(operands[0]), std::move(operands[1])));

        default:
            return Term::primitive(op, type, std::move(operands));
    }
}

} // namespace cppl::kernel
