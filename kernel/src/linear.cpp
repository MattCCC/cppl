#include "cppl/kernel/linear.hpp"

#include "cppl/kernel/arithmetic.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::kernel {

namespace {

std::unexpected<CoreError> fail(std::string detail) {
    return std::unexpected(CoreError{CoreErrorKind::MalformedPrimitive, std::move(detail)});
}

[[nodiscard]] bool add(Wide& into, Wide value) {
    return !__builtin_add_overflow(into, value, &into);
}

[[nodiscard]] bool multiply(Wide lhs, Wide rhs, Wide& out) {
    return !__builtin_mul_overflow(lhs, rhs, &out);
}

std::uint64_t mask(const IntType& type) {
    return type.width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << type.width) - 1u;
}

Wide modulus(const IntType& type) {
    return Wide{1} << type.width;
}

Wide lowest(const IntType& type) {
    return type.signedness == Signedness::Signed ? -(modulus(type) / 2) : Wide{0};
}

Wide highest(const IntType& type) {
    return type.signedness == Signedness::Signed ? modulus(type) / 2 - 1 : modulus(type) - 1;
}

// The representative of a residue closest to zero, so a coefficient of -1 is
// -1 rather than 2^width - 1, whichever signedness the type has.
Wide centered(const IntType& type, std::uint64_t residue) {
    const Wide value{residue & mask(type)};
    return value > modulus(type) / 2 ? value - modulus(type) : value;
}

struct TypedTerm {
    IntType type;
    Term term;
};

struct TypedTermOrder {
    bool operator()(const TypedTerm& lhs, const TypedTerm& rhs) const {
        if (lhs.type.width != rhs.type.width) {
            return lhs.type.width < rhs.type.width;
        }
        if (lhs.type.signedness != rhs.type.signedness) {
            return lhs.type.signedness < rhs.type.signedness;
        }
        return compare(lhs.term, rhs.term) < 0;
    }
};

// A linear expression over the system's variables.
struct Expression {
    std::map<std::uint32_t, Wide> terms;
    Wide constant = 0;
};

class Builder {
  public:
    Builder(const Context& context, std::span<const Type> locals, const CoreLimits& limits)
        : context_(context),
          locals_(locals),
          limits_(limits) {}

    std::expected<void, CoreError> fact(const Proposition& proposition) {
        const auto* equality = std::get_if<Eq>(&proposition.node);
        if (equality == nullptr || !equality->type.is_integer()) {
            return fail("an arithmetic fact must be an equality of integers");
        }
        const IntType type = equality->type.integer_type();
        if (type == kBoolean) {
            auto lhs = normal(equality->lhs);
            if (!lhs) {
                return std::unexpected(lhs.error());
            }
            auto rhs = normal(equality->rhs);
            if (!rhs) {
                return std::unexpected(rhs.error());
            }
            if (const auto* literal = std::get_if<Literal>(&rhs->node)) {
                return truth(*lhs, literal->value != 0);
            }
            if (const auto* literal = std::get_if<Literal>(&lhs->node)) {
                return truth(*rhs, literal->value != 0);
            }
        }
        auto lhs = value(equality->lhs, type);
        if (!lhs) {
            return std::unexpected(lhs.error());
        }
        auto rhs = value(equality->rhs, type);
        if (!rhs) {
            return std::unexpected(rhs.error());
        }
        return equal(*lhs, *rhs);
    }

    // The goal's negation: the goal fails to hold. `False` always fails to hold,
    // so its negation constrains nothing and the facts must be refuted alone.
    std::expected<void, CoreError> refuted(const Proposition& goal) {
        if (std::holds_alternative<Falsity>(goal.node)) {
            return {};
        }
        const auto* equality = std::get_if<Eq>(&goal.node);
        if (equality == nullptr || !equality->type.is_integer()) {
            return fail("linear arithmetic establishes an equality of integers");
        }
        const IntType type = equality->type.integer_type();
        if (type == kBoolean) {
            auto lhs = normal(equality->lhs);
            if (!lhs) {
                return std::unexpected(lhs.error());
            }
            auto rhs = normal(equality->rhs);
            if (!rhs) {
                return std::unexpected(rhs.error());
            }
            // A boolean is zero or one, so failing to be one value is being
            // the other.
            if (const auto* literal = std::get_if<Literal>(&rhs->node)) {
                return truth(*lhs, literal->value == 0);
            }
            if (const auto* literal = std::get_if<Literal>(&lhs->node)) {
                return truth(*rhs, literal->value == 0);
            }
        }
        auto lhs = value(equality->lhs, type);
        if (!lhs) {
            return std::unexpected(lhs.error());
        }
        auto rhs = value(equality->rhs, type);
        if (!rhs) {
            return std::unexpected(rhs.error());
        }
        return differ(*lhs, *rhs);
    }

    ArithmeticSystem take() {
        return std::move(system_);
    }

  private:
    std::expected<Term, CoreError> normal(const Term& term) {
        return normalize(context_, term, limits_);
    }

    // `condition`, a normal boolean term, evaluates to `holds`.
    std::expected<void, CoreError> truth(const Term& condition, bool holds) {
        if (const auto* literal = std::get_if<Literal>(&condition.node)) {
            if ((literal->value != 0) == holds) {
                return {};
            }
            return constrain(Expression{}, Wide{1}); // 1 <= 0
        }
        const auto* primitive = std::get_if<Prim>(&condition.node);
        if (primitive != nullptr && primitive->op == PrimOp::Not && primitive->arguments.size() == 1) {
            return truth(primitive->arguments[0], !holds);
        }
        if (primitive != nullptr && is_comparison(primitive->op) && primitive->arguments.size() == 2) {
            auto lhs = value(primitive->arguments[0], primitive->type);
            if (!lhs) {
                return std::unexpected(lhs.error());
            }
            auto rhs = value(primitive->arguments[1], primitive->type);
            if (!rhs) {
                return std::unexpected(rhs.error());
            }
            switch (primitive->op) {
                case PrimOp::Less:
                    return holds ? before(*lhs, *rhs, 1) : before(*rhs, *lhs, 0);
                case PrimOp::LessEqual:
                    return holds ? before(*lhs, *rhs, 0) : before(*rhs, *lhs, 1);
                case PrimOp::Greater:
                    return holds ? before(*rhs, *lhs, 1) : before(*lhs, *rhs, 0);
                case PrimOp::GreaterEqual:
                    return holds ? before(*rhs, *lhs, 0) : before(*lhs, *rhs, 1);
                case PrimOp::Equal:
                    return holds ? equal(*lhs, *rhs) : differ(*lhs, *rhs);
                case PrimOp::NotEqual:
                    return holds ? differ(*lhs, *rhs) : equal(*lhs, *rhs);
                default:
                    break;
            }
        }
        // Representability: the unbounded integer result lies within the type,
        // or outside it. Where that integer is not linear -- a product of two
        // unknowns -- the condition is a boolean like any other (RFC 0019).
        if (primitive != nullptr && is_representability(primitive->op) && primitive->arguments.size() == 2) {
            auto exact = unbounded(*primitive);
            if (!exact) {
                return std::unexpected(exact.error());
            }
            if (exact->has_value()) {
                return holds ? bound(**exact, primitive->type) : outside(**exact, primitive->type);
            }
            // A product of two unknowns is not linear. Where the ranges their
            // variables always lie in keep every product within the type, as
            // for two values promoted from narrower types, it holds whatever
            // they are: holding states nothing, and failing is impossible.
            auto always = product_always_fits(*primitive);
            if (!always) {
                return std::unexpected(always.error());
            }
            if (*always) {
                return holds ? std::expected<void, CoreError>{} : constrain(Expression{}, Wide{1});
            }
        }
        // Any other boolean is a value like any other.
        auto boolean = value(condition, kBoolean);
        if (!boolean) {
            return std::unexpected(boolean.error());
        }
        Expression expected;
        expected.constant = holds ? 1 : 0;
        return equal(*boolean, expected);
    }

    // The machine value of `term` at `type`, as a linear expression.
    std::expected<Expression, CoreError> value(const Term& term, const IntType& type) {
        auto normalized = normal(term);
        if (!normalized) {
            return std::unexpected(normalized.error());
        }
        const TypedTerm key{type, *normalized};
        if (const auto cached = values_.find(key); cached != values_.end()) {
            return cached->second;
        }
        auto read = polynomial(*normalized, type, limits_);
        if (!read) {
            return std::unexpected(read.error());
        }

        Expression expression;
        if (read->monomials.empty()) {
            expression.constant = value_of(type, read->constant);
        } else if (read->monomials.size() == 1 && read->monomials.front().coefficient == 1u && read->constant == 0u) {
            auto factor = variable(type, read->monomials.front().factors);
            if (!factor) {
                return std::unexpected(factor.error());
            }
            expression.terms.emplace(*factor, Wide{1});
        } else {
            // The polynomial read over the integers differs from the machine
            // value by some multiple of 2^width. Which multiple is a fresh
            // integer variable, and bounding the value by its type pins it.
            for (const Monomial& monomial : read->monomials) {
                auto factor = variable(type, monomial.factors);
                if (!factor) {
                    return std::unexpected(factor.error());
                }
                expression.terms.emplace(*factor, centered(type, monomial.coefficient));
            }
            expression.constant = centered(type, read->constant);
            const auto [least, greatest] = wrap_range(expression, type);
            const auto wrap = static_cast<std::uint32_t>(system_.variables.size());
            system_.variables.push_back(ArithmeticVariable{VariableRole::Wrap, type, *normalized, least, greatest});
            expression.terms.emplace(wrap, -modulus(type));
            if (auto bounded = bound(expression, type); !bounded) {
                return std::unexpected(bounded.error());
            }
        }
        values_.emplace(key, expression);
        return expression;
    }

    // The multiples of 2^width an expression over bounded factors can need.
    // A hint for producers only; where it cannot be computed it is left empty
    // (least above greatest), which asserts nothing.
    std::pair<Wide, Wide> wrap_range(const Expression& expression, const IntType& type) const {
        Wide least = expression.constant;
        Wide greatest = expression.constant;
        for (const auto& [factor, coefficient] : expression.terms) {
            const IntType& factor_type = system_.variables[factor].type;
            Wide low = 0;
            Wide high = 0;
            if (!multiply(coefficient, coefficient >= 0 ? lowest(factor_type) : highest(factor_type), low) ||
                !multiply(coefficient, coefficient >= 0 ? highest(factor_type) : lowest(factor_type), high) ||
                !add(least, low) || !add(greatest, high)) {
                return {1, 0};
            }
        }
        // value = sum - 2^width * wrap, with value within the type, so
        // wrap lies within [ceil((least - highest) / 2^width),
        //                   floor((greatest - lowest) / 2^width)].
        Wide low_numerator = least;
        Wide high_numerator = greatest;
        if (!add(low_numerator, -highest(type)) || !add(high_numerator, -lowest(type))) {
            return {1, 0};
        }
        return {-floor_divide(-low_numerator, modulus(type)), floor_divide(high_numerator, modulus(type))};
    }

    static Wide floor_divide(Wide numerator, Wide denominator) {
        Wide quotient = divide(numerator, denominator);
        if (remainder(numerator, denominator) != 0 && (numerator < 0) != (denominator < 0)) {
            --quotient;
        }
        return quotient;
    }

    // The variable standing for a product of factors: its machine value.
    std::expected<std::uint32_t, CoreError> variable(const IntType& type, const std::vector<Term>& factors) {
        TypedTerm key{type, render_factors(type, factors)};
        if (const auto found = variables_.find(key); found != variables_.end()) {
            return found->second;
        }
        const auto index = static_cast<std::uint32_t>(system_.variables.size());
        system_.variables.push_back(ArithmeticVariable{VariableRole::Value, type, key.term, 0, 0});
        const Term term = key.term;
        variables_.emplace(std::move(key), index);
        Expression single;
        single.terms.emplace(index, Wide{1});
        if (auto bounded = bound(single, type); !bounded) {
            return std::unexpected(bounded.error());
        }
        // A factor that is one of RFC 0019's primitives is also stated by what
        // defines it. The variable is registered first, so a definition that
        // reads the same term again finds it rather than recursing.
        if (const auto* primitive = std::get_if<Prim>(&term.node);
            factors.size() == 1 && primitive != nullptr && primitive->type == type) {
            if (primitive->op == PrimOp::Convert && primitive->arguments.size() == 1) {
                if (auto defined = define_conversion(index, *primitive); !defined) {
                    return std::unexpected(defined.error());
                }
            } else if ((primitive->op == PrimOp::Quotient || primitive->op == PrimOp::Remainder) &&
                       primitive->arguments.size() == 2) {
                if (auto defined = define_division(*primitive); !defined) {
                    return std::unexpected(defined.error());
                }
            }
        }
        return index;
    }

    // The integer an operation computes on its operands' values, with no bound,
    // as a linear expression; none for a product of two non-constants.
    std::expected<std::optional<Expression>, CoreError> unbounded(const Prim& primitive) {
        auto lhs = value(primitive.arguments[0], primitive.type);
        if (!lhs) {
            return std::unexpected(lhs.error());
        }
        auto rhs = value(primitive.arguments[1], primitive.type);
        if (!rhs) {
            return std::unexpected(rhs.error());
        }
        if (primitive.op == PrimOp::AddFits) {
            auto sum = subtract(*lhs, negated(*rhs));
            if (!sum) {
                return std::unexpected(sum.error());
            }
            return std::optional<Expression>{std::move(*sum)};
        }
        if (primitive.op == PrimOp::SubFits) {
            auto difference = subtract(*lhs, *rhs);
            if (!difference) {
                return std::unexpected(difference.error());
            }
            return std::optional<Expression>{std::move(*difference)};
        }
        const Expression* varying = lhs->terms.empty() ? &*rhs : rhs->terms.empty() ? &*lhs : nullptr;
        if (varying == nullptr) {
            return std::optional<Expression>{};
        }
        auto product = scaled(*varying, lhs->terms.empty() ? lhs->constant : rhs->constant);
        if (!product) {
            return std::unexpected(product.error());
        }
        return std::optional<Expression>{std::move(*product)};
    }

    // Whether every product of a value of the first operand and a value of the
    // second lies within the type, judged from the ranges their variables are
    // always in: a value's type, or the narrower type a widening conversion
    // took it from. A product's extremes are among the products of the
    // operands' extremes.
    std::expected<bool, CoreError> product_always_fits(const Prim& primitive) {
        if (primitive.op != PrimOp::MulFits || primitive.arguments.size() != 2) {
            return false;
        }
        auto lhs = value(primitive.arguments[0], primitive.type);
        if (!lhs) {
            return std::unexpected(lhs.error());
        }
        auto rhs = value(primitive.arguments[1], primitive.type);
        if (!rhs) {
            return std::unexpected(rhs.error());
        }
        const auto left = static_range(*lhs);
        const auto right = static_range(*rhs);
        if (!left || !right) {
            return false;
        }
        for (const Wide a : {left->first, left->second}) {
            for (const Wide b : {right->first, right->second}) {
                Wide product = 0;
                if (!multiply(a, b, product) || product < lowest(primitive.type) || product > highest(primitive.type)) {
                    return false;
                }
            }
        }
        return true;
    }

    // The least and greatest value an expression can take, from the range each
    // of its variables is always in. None where a variable's range is not
    // fixed by its construction, as a multiple of 2^width's is not.
    [[nodiscard]] std::optional<std::pair<Wide, Wide>> static_range(const Expression& expression) const {
        Wide least = expression.constant;
        Wide greatest = expression.constant;
        for (const auto& [variable, coefficient] : expression.terms) {
            const ArithmeticVariable& known = system_.variables[variable];
            if (known.role != VariableRole::Value) {
                return std::nullopt;
            }
            Wide low = lowest(known.type);
            Wide high = highest(known.type);
            if (const auto narrowed = ranges_.find(variable); narrowed != ranges_.end()) {
                low = narrowed->second.first;
                high = narrowed->second.second;
            }
            Wide first = 0;
            Wide second = 0;
            if (!multiply(coefficient, coefficient >= 0 ? low : high, first) ||
                !multiply(coefficient, coefficient >= 0 ? high : low, second) || !add(least, first) ||
                !add(greatest, second)) {
                return std::nullopt;
            }
        }
        return std::pair{least, greatest};
    }

    // The expression lies below the least value of `type` or above its
    // greatest: one of two constraints holds.
    std::expected<void, CoreError> outside(const Expression& expression, const IntType& type) {
        auto below = finish(expression, Wide{1} - lowest(type));
        if (!below) {
            return std::unexpected(below.error());
        }
        auto above = finish(negated(expression), highest(type) + 1);
        if (!above) {
            return std::unexpected(above.error());
        }
        system_.disjunctions.push_back({std::move(*below), std::move(*above)});
        return {};
    }

    // `variable` is the value of a conversion of the one argument of
    // `primitive` into the conversion's type. Where every value of the source
    // type is one of the target, the two are equal; otherwise they differ by a
    // fresh multiple of 2^width, which bounding `variable` by its type pins.
    std::expected<void, CoreError> define_conversion(std::uint32_t variable, const Prim& primitive) {
        const IntType& target = primitive.type;
        auto source = type_of(context_, locals_, primitive.arguments[0], limits_);
        if (!source) {
            return std::unexpected(source.error());
        }
        if (!source->is_integer()) {
            return fail("a conversion's operand must be an integer");
        }
        const IntType from = source->integer_type();
        auto operand = value(primitive.arguments[0], from);
        if (!operand) {
            return std::unexpected(operand.error());
        }
        Expression converted;
        converted.terms.emplace(variable, Wide{1});
        if (lowest(from) >= lowest(target) && highest(from) <= highest(target)) {
            // The value is its operand's, so it is always within the source
            // type's range, which a product's range is judged from.
            ranges_.emplace(variable, std::pair{lowest(from), highest(from)});
            return equal(converted, *operand);
        }
        // converted = operand - 2^width * wrap, so the wrap lies within
        // [ceil((lowest(from) - highest) / 2^width), floor((highest(from) - lowest) / 2^width)].
        const Wide least = -floor_divide(highest(target) - lowest(from), modulus(target));
        const Wide greatest = floor_divide(highest(from) - lowest(target), modulus(target));
        const auto wrap = static_cast<std::uint32_t>(system_.variables.size());
        system_.variables.push_back(ArithmeticVariable{VariableRole::Wrap, target,
                                                       Term::primitive(PrimOp::Convert, target, primitive.arguments),
                                                       least, greatest});
        Expression reduced = *operand;
        reduced.terms.emplace(wrap, -modulus(target));
        return equal(converted, reduced);
    }

    // A quotient and a remainder of one dividend by one divisor. Both terms
    // are given variables, and what relates them to the operands is stated
    // once for the pair.
    //
    // By a constant c, |c| >= 2 (normalization folds 0, 1 and -1): the dividend
    // is c * quotient + remainder, the remainder is below |c| in magnitude, and
    // it is never of the opposite sign to the dividend. That is exactly
    // truncating division, and the quotient cannot leave the type.
    //
    // By an unknown divisor only the remainder is bounded: below the divisor
    // where the divisor is positive, of the dividend's sign, and for an
    // unsigned type never above the dividend. Each holds for a zero divisor
    // too, whose remainder is the dividend. The quotient is left unknown.
    std::expected<void, CoreError> define_division(const Prim& primitive) {
        const IntType& type = primitive.type;
        const Term quotient_term = Term::primitive(PrimOp::Quotient, type, primitive.arguments);
        if (!divisions_.insert(TypedTerm{type, quotient_term}).second) {
            return {};
        }
        auto quotient = variable(type, {quotient_term});
        if (!quotient) {
            return std::unexpected(quotient.error());
        }
        auto rest = variable(type, {Term::primitive(PrimOp::Remainder, type, primitive.arguments)});
        if (!rest) {
            return std::unexpected(rest.error());
        }
        auto dividend = value(primitive.arguments[0], type);
        if (!dividend) {
            return std::unexpected(dividend.error());
        }
        const bool is_signed = type.signedness == Signedness::Signed;
        Expression remainder;
        remainder.terms.emplace(*rest, Wide{1});
        // Either member of a disjunction: `first + a <= 0` or `second + b <= 0`.
        const auto either = [this](const Expression& first, Wide a, const Expression& second,
                                   Wide b) -> std::expected<void, CoreError> {
            auto one = finish(first, a);
            if (!one) {
                return std::unexpected(one.error());
            }
            auto other = finish(second, b);
            if (!other) {
                return std::unexpected(other.error());
            }
            system_.disjunctions.push_back({std::move(*one), std::move(*other)});
            return {};
        };
        const auto same_sign = [&]() -> std::expected<void, CoreError> {
            // dividend >= 0 -> remainder >= 0, and dividend <= -1 -> remainder <= 0.
            if (auto nonnegative = either(*dividend, 1, negated(remainder), 0); !nonnegative) {
                return nonnegative;
            }
            return either(negated(*dividend), 0, remainder, 0);
        };

        const auto* literal = std::get_if<Literal>(&primitive.arguments[1].node);
        if (literal != nullptr && literal->type == type) {
            const Wide divisor = value_of(type, bits_of(type, literal->value));
            if (divisor >= -1 && divisor <= 1) {
                return {};
            }
            Expression identity;
            identity.terms.emplace(*quotient, divisor);
            identity.terms.emplace(*rest, Wide{1});
            if (auto divided = equal(*dividend, identity); !divided) {
                return divided;
            }
            const Wide largest = (divisor < 0 ? -divisor : divisor) - 1;
            if (auto below = constrain(remainder, -largest); !below) {
                return below;
            }
            if (!is_signed) {
                return {};
            }
            if (auto above = constrain(negated(remainder), -largest); !above) {
                return above;
            }
            return same_sign();
        }

        auto divisor = value(primitive.arguments[1], type);
        if (!divisor) {
            return std::unexpected(divisor.error());
        }
        // divisor >= 1 -> remainder <= divisor - 1.
        auto excess = subtract(remainder, *divisor);
        if (!excess) {
            return std::unexpected(excess.error());
        }
        if (auto below = either(*divisor, 0, *excess, 1); !below) {
            return below;
        }
        if (!is_signed) {
            // An unsigned remainder never exceeds its dividend.
            auto within = subtract(remainder, *dividend);
            if (!within) {
                return std::unexpected(within.error());
            }
            return constrain(*within, 0);
        }
        // divisor >= 1 -> remainder >= 1 - divisor.
        auto shortfall = subtract(negated(remainder), *divisor);
        if (!shortfall) {
            return std::unexpected(shortfall.error());
        }
        if (auto above = either(*divisor, 0, *shortfall, 1); !above) {
            return above;
        }
        return same_sign();
    }

    // Every coefficient and the constant multiplied by `factor`, refused where
    // the product leaves the core's integers.
    static std::expected<Expression, CoreError> scaled(const Expression& expression, Wide factor) {
        Expression result;
        for (const auto& [variable, coefficient] : expression.terms) {
            Wide product = 0;
            if (!multiply(coefficient, factor, product)) {
                return fail("an arithmetic coefficient overflows the core's integers");
            }
            if (product != 0) {
                result.terms.emplace(variable, product);
            }
        }
        if (!multiply(expression.constant, factor, result.constant)) {
            return fail("an arithmetic constant overflows the core's integers");
        }
        return result;
    }

    // lowest(type) <= expression <= highest(type)
    std::expected<void, CoreError> bound(const Expression& expression, const IntType& type) {
        Expression below = negated(expression);
        if (auto lower = constrain(below, lowest(type)); !lower) {
            return lower;
        }
        Wide top = 0;
        if (!multiply(highest(type), -1, top)) {
            return fail("an arithmetic bound overflows the core's integers");
        }
        return constrain(expression, top);
    }

    // lhs + margin <= rhs
    std::expected<void, CoreError> before(const Expression& lhs, const Expression& rhs, Wide margin) {
        auto difference = subtract(lhs, rhs);
        if (!difference) {
            return std::unexpected(difference.error());
        }
        return constrain(*difference, margin);
    }

    std::expected<void, CoreError> equal(const Expression& lhs, const Expression& rhs) {
        if (auto first = before(lhs, rhs, 0); !first) {
            return first;
        }
        return before(rhs, lhs, 0);
    }

    std::expected<void, CoreError> differ(const Expression& lhs, const Expression& rhs) {
        auto below = linear(lhs, rhs, 1);
        if (!below) {
            return std::unexpected(below.error());
        }
        auto above = linear(rhs, lhs, 1);
        if (!above) {
            return std::unexpected(above.error());
        }
        system_.disjunctions.push_back({std::move(*below), std::move(*above)});
        return {};
    }

    // expression + extra <= 0
    std::expected<void, CoreError> constrain(const Expression& expression, Wide extra) {
        auto constraint = finish(expression, extra);
        if (!constraint) {
            return std::unexpected(constraint.error());
        }
        system_.constraints.push_back(std::move(*constraint));
        return {};
    }

    // lhs - rhs + margin <= 0
    std::expected<LinearConstraint, CoreError> linear(const Expression& lhs, const Expression& rhs, Wide margin) {
        auto difference = subtract(lhs, rhs);
        if (!difference) {
            return std::unexpected(difference.error());
        }
        return finish(*difference, margin);
    }

    static Expression negated(const Expression& expression) {
        Expression result;
        for (const auto& [variable, coefficient] : expression.terms) {
            result.terms.emplace(variable, -coefficient);
        }
        result.constant = -expression.constant;
        return result;
    }

    static std::expected<Expression, CoreError> subtract(const Expression& lhs, const Expression& rhs) {
        Expression result = lhs;
        for (const auto& [variable, coefficient] : rhs.terms) {
            Wide& entry = result.terms[variable];
            if (!add(entry, -coefficient)) {
                return fail("an arithmetic coefficient overflows the core's integers");
            }
            if (entry == 0) {
                result.terms.erase(variable);
            }
        }
        if (!add(result.constant, -rhs.constant)) {
            return fail("an arithmetic constant overflows the core's integers");
        }
        return result;
    }

    static std::expected<LinearConstraint, CoreError> finish(const Expression& expression, Wide extra) {
        LinearConstraint constraint;
        for (const auto& [variable, coefficient] : expression.terms) {
            if (coefficient != 0) {
                constraint.terms.emplace_back(variable, coefficient);
            }
        }
        constraint.constant = expression.constant;
        if (!add(constraint.constant, extra)) {
            return fail("an arithmetic constant overflows the core's integers");
        }
        return constraint;
    }

    const Context& context_;
    std::span<const Type> locals_;
    const CoreLimits& limits_;
    ArithmeticSystem system_;
    std::map<TypedTerm, std::uint32_t, TypedTermOrder> variables_;
    std::map<TypedTerm, Expression, TypedTermOrder> values_;
    // The quotients whose division identity is already stated, by their term.
    std::set<TypedTerm, TypedTermOrder> divisions_;
    // The range a widening conversion's value is always in: its source type's.
    std::map<std::uint32_t, std::pair<Wide, Wide>> ranges_;
};

// Checks a certificate against the constraints standing at each node: the
// system's own, and those the splits and cases above the node added.
class Checker {
  public:
    Checker(const ArithmeticSystem& system, const CoreLimits& limits)
        : system_(system),
          limits_(limits),
          active_(system.constraints) {}

    std::expected<void, std::string> check(const ArithmeticCertificate& certificate, std::uint32_t depth) {
        if (++nodes_ > limits_.max_certificate_nodes) {
            return std::unexpected("the certificate has more than " + std::to_string(limits_.max_certificate_nodes) +
                                   " nodes");
        }
        if (depth > limits_.max_term_depth) {
            return std::unexpected("the certificate nests deeper than the core allows");
        }

        if (const auto* sum = std::get_if<FarkasSum>(&certificate.node)) {
            return contradiction(*sum);
        }

        if (const auto* split = std::get_if<IntegerSplit>(&certificate.node)) {
            LinearConstraint form;
            form.constant = split->constant;
            for (std::size_t index = 0; index < split->terms.size(); ++index) {
                const auto& [variable, coefficient] = split->terms[index];
                if (variable >= system_.variables.size() || coefficient == 0 ||
                    (index > 0 && variable <= split->terms[index - 1].first)) {
                    return std::unexpected("a split names its variables in increasing order, each once, each "
                                           "with a nonzero coefficient");
                }
                form.terms.emplace_back(variable, Wide{coefficient});
            }
            // form <= 0, or form >= 1, which is -form + 1 <= 0.
            LinearConstraint opposite;
            for (const auto& [variable, coefficient] : form.terms) {
                opposite.terms.emplace_back(variable, -coefficient);
            }
            opposite.constant = -form.constant;
            if (!add(opposite.constant, 1)) {
                return std::unexpected("a split constant overflows the core's integers");
            }
            if (auto below = descend(std::move(form), *split->at_most_zero, depth); !below) {
                return below;
            }
            return descend(std::move(opposite), *split->at_least_one, depth);
        }

        const auto& cases = std::get<DisjunctionCases>(certificate.node);
        if (cases.disjunction >= system_.disjunctions.size()) {
            return std::unexpected("the certificate names a disjunction the system does not have");
        }
        const auto& members = system_.disjunctions[cases.disjunction];
        if (auto first = descend(members[0], *cases.first, depth); !first) {
            return first;
        }
        return descend(members[1], *cases.second, depth);
    }

  private:
    std::expected<void, std::string> descend(LinearConstraint added, const ArithmeticCertificate& certificate,
                                             std::uint32_t depth) {
        active_.push_back(std::move(added));
        auto result = check(certificate, depth + 1);
        active_.pop_back();
        return result;
    }

    // The multiples are summed here, and the sum must leave no variable and a
    // positive constant: then the constraints cannot all hold.
    std::expected<void, std::string> contradiction(const FarkasSum& sum) {
        if (sum.multipliers.empty()) {
            return std::unexpected("a contradiction must combine at least one constraint");
        }
        std::map<std::uint32_t, Wide> coefficients;
        Wide constant = 0;
        for (std::size_t index = 0; index < sum.multipliers.size(); ++index) {
            const auto& [position, multiplier] = sum.multipliers[index];
            if (position >= active_.size() || multiplier <= 0 ||
                (index > 0 && position <= sum.multipliers[index - 1].first)) {
                return std::unexpected("a contradiction names standing constraints in increasing order, each once, "
                                       "each with a positive multiplier");
            }
            const LinearConstraint& constraint = active_[position];
            for (const auto& [variable, coefficient] : constraint.terms) {
                Wide scaled = 0;
                if (!multiply(coefficient, multiplier, scaled) || !add(coefficients[variable], scaled)) {
                    return std::unexpected("the combination overflows the core's integers");
                }
            }
            Wide scaled = 0;
            if (!multiply(constraint.constant, multiplier, scaled) || !add(constant, scaled)) {
                return std::unexpected("the combination overflows the core's integers");
            }
        }
        for (const auto& [variable, coefficient] : coefficients) {
            if (coefficient != 0) {
                return std::unexpected("the combination leaves variable " + std::to_string(variable) + " standing");
            }
        }
        if (constant <= 0) {
            return std::unexpected("the combination sums to a constant that is not positive, which is no "
                                   "contradiction");
        }
        return {};
    }

    const ArithmeticSystem& system_;
    const CoreLimits& limits_;
    std::vector<LinearConstraint> active_;
    std::size_t nodes_ = 0;
};

} // namespace

std::expected<ArithmeticSystem, CoreError> arithmetic_system(const Context& context, std::span<const Proposition> facts,
                                                             const Proposition& goal, const CoreLimits& limits,
                                                             std::span<const Type> locals) {
    if (facts.size() > limits.max_arithmetic_facts) {
        return fail("an arithmetic step uses more than " + std::to_string(limits.max_arithmetic_facts) + " facts");
    }
    Builder builder(context, locals, limits);
    for (const Proposition& fact : facts) {
        if (auto added = builder.fact(fact); !added) {
            return std::unexpected(added.error());
        }
    }
    if (auto negated = builder.refuted(goal); !negated) {
        return std::unexpected(negated.error());
    }
    return builder.take();
}

std::expected<void, std::string> refutes(const ArithmeticSystem& system, const ArithmeticCertificate& certificate,
                                         const CoreLimits& limits) {
    Checker checker(system, limits);
    return checker.check(certificate, 0);
}

} // namespace cppl::kernel
