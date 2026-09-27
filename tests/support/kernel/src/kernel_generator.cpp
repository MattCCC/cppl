#include "cppl/testing/kernel_generator.hpp"

#include "cppl/automation/evidence.hpp"
#include "cppl/kernel/box.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/refutation/refute.hpp"
#include "cppl/testing/kernel_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::testing::kernel_generator {

namespace k = kernel;
using kernel_model::Wide;

std::uint32_t ByteChoices::below(std::uint32_t bound) {
    if (bound <= 1) {
        return 0;
    }
    std::uint32_t value = 0;
    // One byte for a small choice, two for a larger one, so a byte stream of
    // a given length always decides about the same amount.
    const std::size_t width = bound <= 256 ? 1 : 2;
    for (std::size_t byte = 0; byte < width; ++byte) {
        if (position_ >= bytes_.size()) {
            return 0;
        }
        value = (value << 8) | bytes_[position_++];
    }
    return value % bound;
}

std::uint32_t SeededChoices::below(std::uint32_t bound) {
    if (bound <= 1) {
        return 0;
    }
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return static_cast<std::uint32_t>(state_ % bound);
}

std::string describe(Mode mode) {
    switch (mode) {
        case Mode::Derivation:
            return "derivation";
        case Mode::Arithmetic:
            return "arithmetic";
        case Mode::Automation:
            return "automation";
    }
    return "invalid";
}

k::ProofTerm lift(const k::ProofTerm& proof, std::uint32_t amount, std::uint32_t cutoff) {
    const auto again = [&](const k::Box<k::ProofTerm>& inner, std::uint32_t at) {
        return k::Box<k::ProofTerm>{lift(*inner, amount, at)};
    };
    return std::visit(
        [&](const auto& node) -> k::ProofTerm {
            using Node = std::decay_t<decltype(node)>;
            Node copy = node;
            if constexpr (std::is_same_v<Node, k::Hypothesis>) {
                if (copy.index.value >= cutoff) {
                    copy.index.value += amount;
                }
            } else if constexpr (std::is_same_v<Node, k::ForallIntroduction>) {
                copy.body = again(node.body, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ForallElimination>) {
                copy.evidence = again(node.evidence, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ImplicationIntroduction>) {
                // The premise this introduces is index 0 underneath it.
                copy.body = again(node.body, cutoff + 1);
            } else if constexpr (std::is_same_v<Node, k::ImplicationElimination>) {
                copy.evidence = again(node.evidence, cutoff);
                copy.premise = again(node.premise, cutoff);
            } else if constexpr (std::is_same_v<Node, k::EqualityElimination>) {
                copy.equality = again(node.equality, cutoff);
                copy.evidence = again(node.evidence, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ConditionalElimination>) {
                copy.true_case = again(node.true_case, cutoff);
                copy.false_case = again(node.false_case, cutoff);
            } else if constexpr (std::is_same_v<Node, k::LinearArithmetic>) {
                for (k::ArithmeticFact& fact : copy.facts) {
                    fact.evidence = again(fact.evidence, cutoff);
                }
            } else if constexpr (std::is_same_v<Node, k::ConjunctionIntroduction>) {
                copy.left = again(node.left, cutoff);
                copy.right = again(node.right, cutoff);
            } else if constexpr (std::is_same_v<Node, k::ConjunctionElimination> ||
                                 std::is_same_v<Node, k::DisjunctionIntroduction> ||
                                 std::is_same_v<Node, k::FalsityElimination>) {
                copy.evidence = again(node.evidence, cutoff);
            } else if constexpr (std::is_same_v<Node, k::DisjunctionElimination>) {
                copy.evidence = again(node.evidence, cutoff);
                copy.left_case = again(node.left_case, cutoff);
                copy.right_case = again(node.right_case, cutoff);
            } else if constexpr (std::is_same_v<Node, k::UnsignedInduction>) {
                // Both premises are checked under the assumptions standing at
                // the induction; neither introduces one.
                copy.base = again(node.base, cutoff);
                copy.step = again(node.step, cutoff);
            } else {
                static_assert(std::is_same_v<Node, k::Reflexivity>);
            }
            return k::ProofTerm{std::move(copy)};
        },
        proof.node);
}

namespace {

constexpr std::uint32_t kPerturbOdds = 10;

k::Type integer(std::uint16_t width, k::Signedness signedness) {
    return k::Type::integer(width, signedness);
}

const k::IntType kBoolean = k::kBoolean;

// The abstract types every sample may quantify over.
k::Type value_v() {
    return k::Type::value("V", {integer(4, k::Signedness::Signed), integer(2, k::Signedness::Unsigned)});
}
k::Type value_w() {
    return k::Type::value("W", {value_v(), integer(8, k::Signedness::Signed)});
}
k::Type indexed_a() {
    return k::Type::indexed(integer(4, k::Signedness::Signed), 4);
}

struct Hypothesis {
    k::Proposition proposition;
    std::size_t binders = 0;
    bool usable = true;
};

struct Scope {
    std::vector<k::Type> locals;
    std::vector<Hypothesis> hypotheses;
};

struct Derivation {
    k::Proposition proposition;
    k::ProofTerm proof;
};

enum class Rule : std::uint8_t {
    ReflexivitySame,
    ReflexivityEquivalent,
    ReflexivityRandom,
    Hypothesis,
    ForallIntroduction,
    ImplicationIntroduction,
    ForallElimination,
    ImplicationElimination,
    ConjunctionIntroduction,
    ConjunctionElimination,
    DisjunctionIntroduction,
    DisjunctionElimination,
    FalsityElimination,
    EqualityElimination,
    ConditionalElimination,
    LinearArithmetic,
    UnsignedInduction,
};
constexpr std::uint32_t kRuleCount = 17;

class Generator {
  public:
    explicit Generator(Choices& choices) : choices_(choices) {}

    Sample run(Mode mode) {
        define_some();
        Sample sample{context_, k::Proposition::falsity(), k::ProofTerm::reflexivity(), mode};
        switch (mode) {
            case Mode::Derivation: {
                Scope scope;
                Derivation derived = derive(scope, 1 + choices_.below(5));
                sample.goal = std::move(derived.proposition);
                sample.proof = std::move(derived.proof);
                break;
            }
            case Mode::Arithmetic: {
                Derivation derived = arithmetic();
                sample.goal = std::move(derived.proposition);
                sample.proof = std::move(derived.proof);
                break;
            }
            case Mode::Automation: {
                sample.goal = automation_goal();
                if (auto proposed = automation::propose(context_, sample.goal)) {
                    sample.proof = std::move(proposed->proof);
                }
                break;
            }
        }
        sample.context = context_;
        return sample;
    }

  private:
    // ---- choices ------------------------------------------------------------

    std::uint32_t below(std::uint32_t bound) {
        return choices_.below(bound);
    }
    bool one_in(std::uint32_t odds) {
        return choices_.one_in(odds);
    }
    bool perturb() {
        return one_in(kPerturbOdds);
    }

    std::uint64_t bits64() {
        std::uint64_t value = 0;
        for (int part = 0; part < 4; ++part) {
            value = (value << 16) | below(1u << 16);
        }
        return value;
    }

    // ---- types --------------------------------------------------------------

    k::IntType small_integer() {
        static constexpr std::uint16_t widths[] = {2, 3, 4, 4, 1};
        const std::uint16_t width = widths[below(5)];
        return k::IntType{width, width == 1 || one_in(2) ? k::Signedness::Unsigned : k::Signedness::Signed};
    }

    k::IntType any_integer() {
        if (!one_in(4)) {
            return small_integer();
        }
        static constexpr std::uint16_t widths[] = {8, 16, 32, 64};
        return k::IntType{widths[below(4)], one_in(2) ? k::Signedness::Unsigned : k::Signedness::Signed};
    }

    // A type to quantify over: enumerable in the model, with abstract types and
    // (rarely) a type the model only samples.
    k::Type binder() {
        switch (below(10)) {
            case 0:
                return value_v();
            case 1:
                return indexed_a();
            case 2:
                return one_in(2) ? value_w() : as_type(any_integer());
            default:
                return as_type(small_integer());
        }
    }

    static k::Type as_type(const k::IntType& type) {
        return k::Type{type};
    }

    // ---- terms --------------------------------------------------------------

    k::Term literal(const k::IntType& type) {
        const Wide least = kernel_model::machine::lowest(type);
        const Wide greatest = kernel_model::machine::highest(type);
        Wide value = 0;
        switch (below(7)) {
            case 0:
                value = 0;
                break;
            case 1:
                value = 1;
                break;
            case 2:
                value = least;
                break;
            case 3:
                value = greatest;
                break;
            case 4:
                value = type.signedness == k::Signedness::Signed ? Wide{-1} : greatest - 1;
                break;
            case 5:
                value = Wide{below(5)} - 2;
                break;
            default:
                value = Wide{bits64()};
                break;
        }
        return k::Term::literal(type, kernel_model::machine::wrap(type, value));
    }

    static std::vector<std::uint32_t> variables_of(std::span<const k::Type> locals, const k::Type& type) {
        std::vector<std::uint32_t> found;
        for (std::size_t position = 0; position < locals.size(); ++position) {
            if (locals[position] == type) {
                found.push_back(static_cast<std::uint32_t>(locals.size() - 1 - position));
            }
        }
        return found;
    }

    std::optional<k::Term> variable(std::span<const k::Type> locals, const k::Type& type) {
        const auto found = variables_of(locals, type);
        if (found.empty()) {
            return std::nullopt;
        }
        return k::Term::variable(k::VarIndex{found[below(static_cast<std::uint32_t>(found.size()))]});
    }

    // A term of an abstract type, where one can be formed.
    std::optional<k::Term> abstract_term(std::span<const k::Type> locals, const k::Type& type, unsigned depth) {
        if (type == value_v() && depth > 0 && one_in(3)) {
            if (auto whole = variable(locals, value_w())) {
                return k::Term::project(value_w(), 0, std::move(*whole));
            }
        }
        if (depth > 0 && one_in(3)) {
            for (const k::Definition& definition : context_.definitions()) {
                if (definition.result == type) {
                    if (auto call = call_of(locals, definition, depth - 1)) {
                        return call;
                    }
                }
            }
        }
        return variable(locals, type);
    }

    std::optional<k::Term> call_of(std::span<const k::Type> locals, const k::Definition& definition, unsigned depth) {
        std::vector<k::Term> arguments;
        for (const k::Type& parameter : definition.parameters) {
            auto argument = term(locals, parameter, depth);
            if (!argument) {
                return std::nullopt;
            }
            arguments.push_back(std::move(*argument));
        }
        return k::Term::call(definition.id, std::move(arguments));
    }

    std::optional<k::Term> term(std::span<const k::Type> locals, const k::Type& type, unsigned depth) {
        if (type.is_integer()) {
            return integer_term(locals, type.integer_type(), depth);
        }
        return abstract_term(locals, type, depth);
    }

    k::Term integer_term(std::span<const k::Type> locals, const k::IntType& type, unsigned depth) {
        const bool boolean = type == kBoolean;
        const std::uint32_t choice = depth == 0 ? below(3) : below(boolean ? 15 : 12);
        const auto sub = [&](const k::IntType& at) {
            return integer_term(locals, at, depth - 1);
        };
        switch (choice) {
            case 1:
            case 2:
            case 9:
                if (auto found = variable(locals, as_type(type))) {
                    return *found;
                }
                return literal(type);
            case 3: {
                static constexpr k::PrimOp ring[] = {k::PrimOp::AddWrap, k::PrimOp::SubWrap, k::PrimOp::MulWrap};
                const k::PrimOp op = ring[below(3)];
                // A product is kept linear most of the time, so arithmetic
                // can close what it states.
                if (op == k::PrimOp::MulWrap && !one_in(3)) {
                    return k::Term::primitive(op, type, {literal(type), sub(type)});
                }
                return k::Term::primitive(op, type, {sub(type), sub(type)});
            }
            case 4:
                return k::Term::primitive(k::PrimOp::Select, type, {sub(kBoolean), sub(type), sub(type)});
            case 5: {
                const k::PrimOp op = one_in(2) ? k::PrimOp::Quotient : k::PrimOp::Remainder;
                return k::Term::primitive(op, type, {sub(type), one_in(2) ? literal(type) : sub(type)});
            }
            case 6:
                return k::Term::primitive(k::PrimOp::Convert, type, {sub(any_integer())});
            case 7: {
                for (const k::Definition& definition : context_.definitions()) {
                    if (definition.result == as_type(type) && one_in(2)) {
                        if (auto call = call_of(locals, definition, depth - 1)) {
                            return *call;
                        }
                    }
                }
                return literal(type);
            }
            case 8: {
                if (auto observed = observation(locals, type, depth)) {
                    return *observed;
                }
                return literal(type);
            }
            case 10:
                if (depth > 0 && !boolean) {
                    return k::Term::primitive(k::PrimOp::AddWrap, type, {sub(type), literal(type)});
                }
                return literal(type);
            case 11:
                if (depth > 0) {
                    return k::Term::primitive(k::PrimOp::Convert, type, {sub(small_integer())});
                }
                return literal(type);
            case 12:
                return comparison(locals, depth);
            case 13: {
                static constexpr k::PrimOp fits[] = {k::PrimOp::AddFits, k::PrimOp::SubFits, k::PrimOp::MulFits};
                const k::IntType at = any_integer();
                return k::Term::primitive(fits[below(3)], at, {sub(at), sub(at)});
            }
            case 14:
                return k::Term::primitive(k::PrimOp::Not, kBoolean, {sub(kBoolean)});
            default:
                return literal(type);
        }
    }

    // An observation of an abstract or indexed variable yielding `type`.
    std::optional<k::Term> observation(std::span<const k::Type> locals, const k::IntType& type, unsigned depth) {
        const k::Type wanted = as_type(type);
        if (wanted == integer(4, k::Signedness::Signed)) {
            if (one_in(2)) {
                if (auto array = variable(locals, indexed_a())) {
                    return k::Term::element(indexed_a(), std::move(*array),
                                            integer_term(locals, small_integer(), depth - 1));
                }
            }
            if (auto subject = abstract_term(locals, value_v(), depth - 1)) {
                return k::Term::project(value_v(), 0, std::move(*subject));
            }
        }
        if (wanted == integer(2, k::Signedness::Unsigned)) {
            if (auto subject = abstract_term(locals, value_v(), depth - 1)) {
                return k::Term::project(value_v(), 1, std::move(*subject));
            }
        }
        if (wanted == integer(8, k::Signedness::Signed)) {
            if (auto subject = variable(locals, value_w())) {
                return k::Term::project(value_w(), 1, std::move(*subject));
            }
        }
        return std::nullopt;
    }

    k::Term comparison(std::span<const k::Type> locals, unsigned depth) {
        static constexpr k::PrimOp orders[] = {k::PrimOp::Equal,     k::PrimOp::NotEqual, k::PrimOp::Less,
                                               k::PrimOp::LessEqual, k::PrimOp::Greater,  k::PrimOp::GreaterEqual};
        const k::IntType at = one_in(3) ? any_integer() : small_integer();
        const unsigned below_depth = depth == 0 ? 0 : depth - 1;
        return k::Term::primitive(orders[below(6)], at,
                                  {integer_term(locals, at, below_depth), integer_term(locals, at, below_depth)});
    }

    // ---- propositions -------------------------------------------------------

    // A comparison stated as a proposition, in either of the forms the kernel
    // and the elaborator use.
    k::Proposition comparison_proposition(std::span<const k::Type> locals, unsigned depth) {
        if (one_in(3)) {
            const k::IntType at = small_integer();
            return k::Proposition::equality(as_type(at), integer_term(locals, at, depth),
                                            integer_term(locals, at, depth));
        }
        return k::Proposition::equality(as_type(kBoolean), comparison(locals, depth),
                                        k::Term::literal(kBoolean, one_in(4) ? 0 : 1));
    }

    k::Proposition proposition(std::vector<k::Type>& locals, unsigned depth) {
        switch (depth == 0 ? below(2) : below(9)) {
            case 1: {
                const k::IntType at = any_integer();
                return k::Proposition::equality(as_type(at), integer_term(locals, at, 2), integer_term(locals, at, 2));
            }
            case 2: {
                const k::Type at = one_in(2) ? value_v() : indexed_a();
                auto lhs = abstract_term(locals, at, 1);
                auto rhs = abstract_term(locals, at, 1);
                if (lhs && rhs) {
                    return k::Proposition::equality(at, std::move(*lhs), std::move(*rhs));
                }
                return comparison_proposition(locals, 2);
            }
            case 3: {
                const k::Type bound = binder();
                locals.push_back(bound);
                auto body = proposition(locals, depth - 1);
                locals.pop_back();
                return k::Proposition::for_all(bound, std::move(body));
            }
            case 4: {
                auto premise = proposition(locals, depth - 1);
                return k::Proposition::implication(std::move(premise), proposition(locals, depth - 1));
            }
            case 5: {
                auto left = proposition(locals, depth - 1);
                return k::Proposition::conjunction(std::move(left), proposition(locals, depth - 1));
            }
            case 6: {
                auto left = proposition(locals, depth - 1);
                return k::Proposition::disjunction(std::move(left), proposition(locals, depth - 1));
            }
            case 7:
                return k::Proposition::falsity();
            default:
                return comparison_proposition(locals, 2);
        }
    }

    // ---- definitions --------------------------------------------------------

    void define_some() {
        const std::uint32_t count = below(4);
        for (std::uint32_t index = 0; index < count; ++index) {
            k::Definition definition;
            definition.id = k::DefId{index * 3 + 1};
            definition.name = "d" + std::to_string(index);
            const std::uint32_t parameters = below(4);
            for (std::uint32_t parameter = 0; parameter < parameters; ++parameter) {
                definition.parameters.push_back(one_in(4) ? (one_in(2) ? value_v() : indexed_a())
                                                          : as_type(small_integer()));
            }
            if (one_in(6)) {
                if (auto passed = variable(definition.parameters, value_v())) {
                    definition.result = value_v();
                    definition.body = *passed;
                    (void)context_.define(std::move(definition));
                    continue;
                }
            }
            const k::IntType result = one_in(4) ? any_integer() : small_integer();
            definition.result = as_type(result);
            definition.body = integer_term(definition.parameters, result, 3);
            (void)context_.define(std::move(definition));
        }
    }

    // ---- rewriting that preserves meaning ----------------------------------

    std::optional<k::Type> type_of(std::span<const k::Type> locals, const k::Term& term) const {
        auto typed = k::type_of(context_, locals, term);
        if (!typed) {
            return std::nullopt;
        }
        return *typed;
    }

    static k::Term substitute_parameters(const k::Term& body, const std::vector<k::Term>& arguments) {
        if (const auto* var = std::get_if<k::Var>(&body.node)) {
            if (var->index.value < arguments.size()) {
                return arguments[arguments.size() - 1 - var->index.value];
            }
            return body;
        }
        return std::visit(
            [&](const auto& node) -> k::Term {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                    return k::Term{node};
                } else {
                    Node copy = node;
                    for (k::Term& argument : copy.arguments) {
                        argument = substitute_parameters(argument, arguments);
                    }
                    return k::Term{std::move(copy)};
                }
            },
            body.node);
    }

    // A term that denotes the same value as `term` on every assignment.
    k::Term equivalent(std::span<const k::Type> locals, const k::Term& term, unsigned depth) {
        const auto typed = type_of(locals, term);
        if (!typed || !typed->is_integer()) {
            return term;
        }
        const k::IntType type = typed->integer_type();
        const auto zero = k::Term::literal(type, 0);
        if (depth > 0 && one_in(2)) {
            // Rewrite inside one argument instead.
            return std::visit(
                [&](const auto& node) -> k::Term {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                        return k::Term{node};
                    } else {
                        Node copy = node;
                        if (!copy.arguments.empty()) {
                            const auto at = below(static_cast<std::uint32_t>(copy.arguments.size()));
                            copy.arguments[at] = equivalent(locals, copy.arguments[at], depth - 1);
                        }
                        return k::Term{std::move(copy)};
                    }
                },
                term.node);
        }
        if (const auto* call = std::get_if<k::Call>(&term.node)) {
            if (const k::Definition* definition = context_.lookup(call->callee)) {
                return substitute_parameters(definition->body, call->arguments);
            }
        }
        if (const auto* primitive = std::get_if<k::Prim>(&term.node)) {
            const auto& a = primitive->arguments;
            switch (primitive->op) {
                case k::PrimOp::AddWrap:
                case k::PrimOp::MulWrap:
                    if (a.size() == 2 && one_in(2)) {
                        return k::Term::primitive(primitive->op, primitive->type, {a[1], a[0]});
                    }
                    break;
                case k::PrimOp::Less:
                    if (a.size() == 2) {
                        return k::Term::primitive(k::PrimOp::Greater, primitive->type, {a[1], a[0]});
                    }
                    break;
                case k::PrimOp::LessEqual:
                    if (a.size() == 2) {
                        return k::Term::primitive(k::PrimOp::Not, kBoolean,
                                                  {k::Term::primitive(k::PrimOp::Less, primitive->type, {a[1], a[0]})});
                    }
                    break;
                case k::PrimOp::Equal:
                    if (a.size() == 2) {
                        return k::Term::primitive(k::PrimOp::Equal, primitive->type, {a[1], a[0]});
                    }
                    break;
                case k::PrimOp::NotEqual:
                    if (a.size() == 2) {
                        return k::Term::primitive(k::PrimOp::Not, kBoolean,
                                                  {k::Term::primitive(k::PrimOp::Equal, primitive->type, a)});
                    }
                    break;
                case k::PrimOp::Select:
                    if (a.size() == 3) {
                        return k::Term::primitive(k::PrimOp::Select, primitive->type,
                                                  {k::Term::primitive(k::PrimOp::Not, kBoolean, {a[0]}), a[2], a[1]});
                    }
                    break;
                case k::PrimOp::AddFits:
                case k::PrimOp::MulFits:
                    if (a.size() == 2) {
                        return k::Term::primitive(primitive->op, primitive->type, {a[1], a[0]});
                    }
                    break;
                default:
                    break;
            }
        }
        if (type == kBoolean) {
            return k::Term::primitive(k::PrimOp::Not, kBoolean, {k::Term::primitive(k::PrimOp::Not, kBoolean, {term})});
        }
        switch (below(5)) {
            case 0:
                return k::Term::primitive(k::PrimOp::AddWrap, type, {term, zero});
            case 1:
                return k::Term::primitive(k::PrimOp::MulWrap, type, {k::Term::literal(type, 1), term});
            case 2: {
                const k::Term other = integer_term(locals, type, 1);
                return k::Term::primitive(k::PrimOp::SubWrap, type,
                                          {k::Term::primitive(k::PrimOp::AddWrap, type, {term, other}), other});
            }
            case 3:
                return k::Term::primitive(k::PrimOp::Quotient, type, {term, k::Term::literal(type, 1)});
            default:
                return k::Term::primitive(k::PrimOp::SubWrap, type, {term, zero});
        }
    }

    // ---- motives ------------------------------------------------------------

    // `term` with every occurrence of `target` replaced by the variable `hole`.
    static k::Term abstract(const k::Term& term, const k::Term& target, std::uint32_t hole) {
        if (term == target) {
            return k::Term::variable(k::VarIndex{hole});
        }
        return std::visit(
            [&](const auto& node) -> k::Term {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                    return k::Term{node};
                } else {
                    Node copy = node;
                    for (k::Term& argument : copy.arguments) {
                        argument = abstract(argument, target, hole);
                    }
                    return k::Term{std::move(copy)};
                }
            },
            term.node);
    }

    // A motive whose hole stands where `target` does in `proposition`. Both are
    // stated one binder out, under the hole; `depth` counts the quantifiers
    // passed inside the proposition.
    static k::Proposition abstract(const k::Proposition& proposition, const k::Term& target, std::uint32_t depth) {
        if (const auto* quantified = std::get_if<k::Forall>(&proposition.node)) {
            return k::Proposition::for_all(quantified->binder,
                                           abstract(*quantified->body, k::shift(target, 1), depth + 1));
        }
        if (const auto* implication = std::get_if<k::Implies>(&proposition.node)) {
            return k::Proposition::implication(abstract(*implication->premise, target, depth),
                                               abstract(*implication->conclusion, target, depth));
        }
        if (const auto* conjunction = std::get_if<k::And>(&proposition.node)) {
            return k::Proposition::conjunction(abstract(*conjunction->left, target, depth),
                                               abstract(*conjunction->right, target, depth));
        }
        if (const auto* disjunction = std::get_if<k::Or>(&proposition.node)) {
            return k::Proposition::disjunction(abstract(*disjunction->left, target, depth),
                                               abstract(*disjunction->right, target, depth));
        }
        if (const auto* equality = std::get_if<k::Eq>(&proposition.node)) {
            return k::Proposition::equality(equality->type, abstract(equality->lhs, target, depth),
                                            abstract(equality->rhs, target, depth));
        }
        return proposition;
    }

    // ---- scope --------------------------------------------------------------

    static k::Proposition available(const Scope& scope, std::size_t position) {
        const Hypothesis& hypothesis = scope.hypotheses[position];
        return k::shift(hypothesis.proposition, static_cast<std::uint32_t>(scope.locals.size() - hypothesis.binders));
    }

    static k::ProofTerm use(const Scope& scope, std::size_t position) {
        return k::ProofTerm::hypothesis(
            k::HypothesisIndex{static_cast<std::uint32_t>(scope.hypotheses.size() - 1 - position)});
    }

    static std::size_t suppose(Scope& scope, k::Proposition premise, bool usable = true) {
        scope.hypotheses.push_back(Hypothesis{std::move(premise), scope.locals.size(), usable});
        return scope.hypotheses.size() - 1;
    }

    // Closes every premise supposed since `mark` into an implication.
    static Derivation discharge(Scope& scope, std::size_t mark, Derivation derived) {
        while (scope.hypotheses.size() > mark) {
            k::Proposition premise = std::move(scope.hypotheses.back().proposition);
            scope.hypotheses.pop_back();
            derived.proof = k::ProofTerm::implication_introduction(premise, std::move(derived.proof));
            derived.proposition = k::Proposition::implication(std::move(premise), std::move(derived.proposition));
        }
        return derived;
    }

    // A usable hypothesis whose restatement here satisfies `accepts`.
    template <typename Predicate> std::optional<std::size_t> find(const Scope& scope, Predicate accepts) {
        std::vector<std::size_t> found;
        for (std::size_t position = 0; position < scope.hypotheses.size(); ++position) {
            if (scope.hypotheses[position].usable && accepts(available(scope, position))) {
                found.push_back(position);
            }
        }
        if (found.empty()) {
            return std::nullopt;
        }
        return found[below(static_cast<std::uint32_t>(found.size()))];
    }

    // ---- derivations --------------------------------------------------------

    Derivation derive(Scope& scope, unsigned depth) {
        const auto rule = static_cast<Rule>(depth == 0 ? below(4) : below(kRuleCount));
        switch (rule) {
            case Rule::ReflexivitySame:
                return reflexivity(scope, 0);
            case Rule::ReflexivityEquivalent:
                return reflexivity(scope, 1);
            case Rule::ReflexivityRandom:
                return depth == 0 ? reflexivity(scope, 1) : reflexivity(scope, 2);
            case Rule::Hypothesis:
                return hypothesis(scope);
            case Rule::ForallIntroduction:
                return forall_introduction(scope, depth);
            case Rule::ImplicationIntroduction:
                return implication_introduction(scope, depth);
            case Rule::ForallElimination:
                return forall_elimination(scope, depth);
            case Rule::ImplicationElimination:
                return implication_elimination(scope, depth);
            case Rule::ConjunctionIntroduction:
                return conjunction_introduction(scope, depth);
            case Rule::ConjunctionElimination:
                return conjunction_elimination(scope, depth);
            case Rule::DisjunctionIntroduction:
                return disjunction_introduction(scope, depth);
            case Rule::DisjunctionElimination:
                return disjunction_elimination(scope, depth);
            case Rule::FalsityElimination:
                return falsity_elimination(scope, depth);
            case Rule::EqualityElimination:
                return equality_elimination(scope, depth);
            case Rule::ConditionalElimination:
                return conditional_elimination(scope, depth);
            case Rule::LinearArithmetic:
                return linear_arithmetic(scope);
            case Rule::UnsignedInduction:
                return unsigned_induction(scope);
        }
        return reflexivity(scope, 0);
    }

    // Induction over an unsigned type, whose two premises the kernel states
    // itself (kernel::induction_base, kernel::induction_step). A reflexive
    // motive has premises derivable anywhere, so the rule is accepted; any other
    // is a comparison whose premises automation proposes where the goal is
    // closed, so a motive false at some value is refused or, if the kernel ever
    // accepted it, false in the model. Perturbed, the rule names another binder,
    // the goal quantifies over a signed type, or the premises trade places.
    Derivation unsigned_induction(Scope& scope) {
        static constexpr std::uint16_t widths[] = {1, 2, 3, 4};
        const k::IntType type{widths[below(4)], k::Signedness::Unsigned};
        const k::Type bound = as_type(type);
        const bool closed = scope.locals.empty() && scope.hypotheses.empty();
        const bool reflexive = !closed || one_in(3);
        scope.locals.push_back(bound);
        k::Proposition body = reflexive ? k::Proposition::equality(bound, k::Term::variable(k::VarIndex{0}),
                                                                   k::Term::variable(k::VarIndex{0}))
                                        : comparison_proposition(scope.locals, 2);
        scope.locals.pop_back();
        const k::Proposition base_goal = k::induction_base(type, body);
        const k::Proposition step_goal = k::induction_step(type, body);
        k::ProofTerm base = k::ProofTerm::reflexivity();
        k::ProofTerm step = k::ProofTerm::reflexivity();
        if (reflexive) {
            // forall n. n < max -> P(n) -> P(n + 1), each P an instance of x = x.
            const auto& quantified = std::get<k::Forall>(step_goal.node);
            const auto& range = std::get<k::Implies>(quantified.body->node);
            const auto& hypothesis = std::get<k::Implies>(range.conclusion->node);
            step = k::ProofTerm::forall_introduction(
                bound, k::ProofTerm::implication_introduction(
                           *range.premise,
                           k::ProofTerm::implication_introduction(*hypothesis.premise, k::ProofTerm::reflexivity())));
        } else {
            if (auto proposed = automation::propose(context_, base_goal)) {
                base = std::move(proposed->proof);
            }
            if (auto proposed = automation::propose(context_, step_goal)) {
                step = std::move(proposed->proof);
            }
        }
        k::Type stated = bound;
        k::Proposition goal = k::Proposition::for_all(bound, body);
        if (perturb()) {
            switch (below(3)) {
                case 0:
                    stated = as_type(k::IntType{widths[below(4)], k::Signedness::Unsigned});
                    break;
                case 1:
                    stated = as_type(k::IntType{type.width, k::Signedness::Signed});
                    goal = k::Proposition::for_all(stated, body);
                    break;
                default:
                    std::swap(base, step);
                    break;
            }
        }
        return Derivation{std::move(goal), k::ProofTerm::unsigned_induction(stated, std::move(base), std::move(step))};
    }

    // A claimed conclusion replaced by another, the evidence kept.
    Derivation misclaim(Scope& scope, Derivation derived) {
        derived.proposition = proposition(scope.locals, 1);
        return derived;
    }

    Derivation reflexivity(Scope& scope, unsigned kind) {
        if (one_in(8)) {
            const k::Type at = one_in(2) ? value_v() : indexed_a();
            if (auto subject = abstract_term(scope.locals, at, 1)) {
                return Derivation{k::Proposition::equality(at, *subject, *subject), k::ProofTerm::reflexivity()};
            }
        }
        const k::IntType type = any_integer();
        k::Term lhs = integer_term(scope.locals, type, 3);
        k::Term rhs = lhs;
        if (kind == 1) {
            rhs = equivalent(scope.locals, lhs, 3);
        } else if (kind == 2 || perturb()) {
            rhs = integer_term(scope.locals, type, 3);
        }
        if (one_in(2)) {
            std::swap(lhs, rhs);
        }
        return Derivation{k::Proposition::equality(as_type(type), std::move(lhs), std::move(rhs)),
                          k::ProofTerm::reflexivity()};
    }

    Derivation hypothesis(Scope& scope) {
        const auto position = find(scope, [](const k::Proposition&) { return true; });
        if (!position) {
            return reflexivity(scope, 0);
        }
        Derivation derived{available(scope, *position), use(scope, *position)};
        if (perturb()) {
            switch (below(3)) {
                case 0: {
                    // A neighbouring index.
                    auto& index = std::get<k::Hypothesis>(derived.proof.node).index.value;
                    index = one_in(2) ? index + 1 : (index == 0 ? 0 : index - 1);
                    break;
                }
                case 1:
                    // The premise as it was written, not restated for here.
                    derived.proposition = scope.hypotheses[*position].proposition;
                    break;
                default:
                    return misclaim(scope, std::move(derived));
            }
        }
        return derived;
    }

    Derivation forall_introduction(Scope& scope, unsigned depth) {
        const k::Type bound = binder();
        scope.locals.push_back(bound);
        Derivation body = derive(scope, depth - 1);
        scope.locals.pop_back();
        k::Type stated = bound;
        if (perturb()) {
            stated = binder();
        }
        return Derivation{k::Proposition::for_all(bound, std::move(body.proposition)),
                          k::ProofTerm::forall_introduction(stated, std::move(body.proof))};
    }

    Derivation implication_introduction(Scope& scope, unsigned depth) {
        k::Proposition premise = proposition(scope.locals, 2);
        const std::size_t mark = scope.hypotheses.size();
        suppose(scope, premise);
        Derivation body = derive(scope, depth - 1);
        Derivation derived = discharge(scope, mark, std::move(body));
        if (perturb()) {
            auto& introduction = std::get<k::ImplicationIntroduction>(derived.proof.node);
            introduction.premise = k::Box<k::Proposition>{proposition(scope.locals, 1)};
        }
        return derived;
    }

    // A term of `type` here, or nothing where none can be formed.
    std::optional<k::Term> argument(const Scope& scope, const k::Type& type) {
        return term(scope.locals, type, 2);
    }

    Derivation forall_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        k::Proposition quantified = k::Proposition::falsity();
        k::ProofTerm evidence = k::ProofTerm::reflexivity();
        const auto existing =
            find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Forall>(p.node); });
        const std::uint32_t source = below(3);
        if (source == 0 && existing) {
            quantified = available(scope, *existing);
            evidence = use(scope, *existing);
        } else {
            k::Type bound = binder();
            if (!argument(scope, bound)) {
                bound = as_type(small_integer());
            }
            if (source == 1) {
                scope.locals.push_back(bound);
                auto body = proposition(scope.locals, 2);
                scope.locals.pop_back();
                quantified = k::Proposition::for_all(bound, std::move(body));
                const std::size_t position = suppose(scope, quantified);
                evidence = use(scope, position);
            } else {
                scope.locals.push_back(bound);
                Derivation body = derive(scope, depth - 1);
                scope.locals.pop_back();
                quantified = k::Proposition::for_all(bound, body.proposition);
                evidence = k::ProofTerm::forall_introduction(bound, std::move(body.proof));
            }
        }
        const auto& forall = std::get<k::Forall>(quantified.node);
        auto chosen = argument(scope, forall.binder);
        if (!chosen) {
            return discharge(scope, mark, reflexivity(scope, 0));
        }
        k::Proposition conclusion = k::instantiate(*forall.body, *chosen);
        k::Term given = *chosen;
        k::Proposition restated = quantified;
        if (perturb()) {
            switch (below(4)) {
                case 0:
                    // An argument of some other type.
                    given = literal(any_integer());
                    break;
                case 1:
                    // The conclusion at some other argument.
                    if (auto other = argument(scope, forall.binder)) {
                        conclusion = k::instantiate(*forall.body, *other);
                    }
                    break;
                case 2:
                    // A restatement that is not what the evidence establishes.
                    restated = k::Proposition::for_all(forall.binder, [&] {
                        scope.locals.push_back(forall.binder);
                        auto body = proposition(scope.locals, 1);
                        scope.locals.pop_back();
                        return body;
                    }());
                    conclusion = k::instantiate(*std::get<k::Forall>(restated.node).body, *chosen);
                    break;
                default:
                    // Substitution without restating the argument for the
                    // binders it descends through.
                    conclusion = unshifted_instantiate(*forall.body, *chosen, 0);
                    break;
            }
        }
        Derivation derived{std::move(conclusion), k::ProofTerm::forall_elimination(
                                                      std::move(restated), std::move(evidence), std::move(given))};
        return discharge(scope, mark, std::move(derived));
    }

    // A capture-unsafe substitution, for a claim the kernel must refuse.
    static k::Term unshifted_instantiate(const k::Term& body, const k::Term& argument, std::uint32_t depth) {
        if (const auto* var = std::get_if<k::Var>(&body.node)) {
            if (var->index.value == depth) {
                return argument;
            }
            if (var->index.value > depth) {
                return k::Term::variable(k::VarIndex{var->index.value - 1});
            }
            return body;
        }
        return std::visit(
            [&](const auto& node) -> k::Term {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, k::Var> || std::is_same_v<Node, k::Literal>) {
                    return k::Term{node};
                } else {
                    Node copy = node;
                    for (k::Term& child : copy.arguments) {
                        child = unshifted_instantiate(child, argument, depth);
                    }
                    return k::Term{std::move(copy)};
                }
            },
            body.node);
    }

    static k::Proposition unshifted_instantiate(const k::Proposition& body, const k::Term& argument,
                                                std::uint32_t depth) {
        if (const auto* quantified = std::get_if<k::Forall>(&body.node)) {
            return k::Proposition::for_all(quantified->binder,
                                           unshifted_instantiate(*quantified->body, argument, depth + 1));
        }
        if (const auto* implication = std::get_if<k::Implies>(&body.node)) {
            return k::Proposition::implication(unshifted_instantiate(*implication->premise, argument, depth),
                                               unshifted_instantiate(*implication->conclusion, argument, depth));
        }
        if (const auto* conjunction = std::get_if<k::And>(&body.node)) {
            return k::Proposition::conjunction(unshifted_instantiate(*conjunction->left, argument, depth),
                                               unshifted_instantiate(*conjunction->right, argument, depth));
        }
        if (const auto* disjunction = std::get_if<k::Or>(&body.node)) {
            return k::Proposition::disjunction(unshifted_instantiate(*disjunction->left, argument, depth),
                                               unshifted_instantiate(*disjunction->right, argument, depth));
        }
        if (const auto* equality = std::get_if<k::Eq>(&body.node)) {
            return k::Proposition::equality(equality->type, unshifted_instantiate(equality->lhs, argument, depth),
                                            unshifted_instantiate(equality->rhs, argument, depth));
        }
        return body;
    }

    Derivation implication_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        k::Proposition premise = k::Proposition::falsity();
        std::optional<std::size_t> premise_supposed;
        std::optional<k::ProofTerm> premise_evidence;
        if (one_in(3)) {
            premise = proposition(scope.locals, 2);
            premise_supposed = suppose(scope, premise);
        } else {
            Derivation derived = derive(scope, depth - 1);
            premise = std::move(derived.proposition);
            premise_evidence = std::move(derived.proof);
        }

        k::Proposition implication = k::Proposition::falsity();
        k::ProofTerm implication_evidence = k::ProofTerm::reflexivity();
        std::uint32_t pushed_after = 0;
        if (one_in(3)) {
            k::Proposition conclusion = proposition(scope.locals, 2);
            implication = k::Proposition::implication(premise, std::move(conclusion));
            const std::size_t position = suppose(scope, implication);
            implication_evidence = use(scope, position);
            pushed_after = 1;
        } else {
            const std::size_t inner = scope.hypotheses.size();
            suppose(scope, premise);
            Derivation body = derive(scope, depth - 1);
            Derivation introduced = discharge(scope, inner, std::move(body));
            implication = std::move(introduced.proposition);
            implication_evidence = std::move(introduced.proof);
        }

        k::ProofTerm evidence =
            premise_supposed ? use(scope, *premise_supposed) : lift(*premise_evidence, pushed_after);
        k::Proposition conclusion = *std::get<k::Implies>(implication.node).conclusion;
        k::Proposition restated = implication;
        if (perturb()) {
            if (one_in(2)) {
                // Evidence for some other premise.
                evidence = reflexivity(scope, 2).proof;
            } else {
                restated = k::Proposition::implication(proposition(scope.locals, 1), conclusion);
            }
        }
        Derivation derived{std::move(conclusion),
                           k::ProofTerm::implication_elimination(std::move(restated), std::move(implication_evidence),
                                                                 std::move(evidence))};
        return discharge(scope, mark, std::move(derived));
    }

    // Evidence for some proposition, either derived here or supposed; a
    // supposed one stays in scope until the caller discharges it.
    Derivation premise(Scope& scope, unsigned depth) {
        if (one_in(3)) {
            k::Proposition supposed = proposition(scope.locals, 2);
            const std::size_t position = suppose(scope, supposed);
            return Derivation{std::move(supposed), use(scope, position)};
        }
        return derive(scope, depth - 1);
    }

    // Evidence for a conjunction; premises it supposes stay in scope.
    Derivation conjunction_of(Scope& scope, unsigned depth) {
        Derivation left = premise(scope, depth);
        const std::size_t between = scope.hypotheses.size();
        Derivation right = premise(scope, depth);
        const auto lifted = static_cast<std::uint32_t>(scope.hypotheses.size() - between);
        return Derivation{k::Proposition::conjunction(left.proposition, right.proposition),
                          k::ProofTerm::conjunction_introduction(lift(left.proof, lifted), std::move(right.proof))};
    }

    Derivation conjunction_introduction(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        Derivation both = conjunction_of(scope, depth);
        if (perturb()) {
            const auto& sides = std::get<k::And>(both.proposition.node);
            both.proposition = k::Proposition::conjunction(*sides.left, proposition(scope.locals, 1));
        }
        return discharge(scope, mark, std::move(both));
    }

    Derivation conjunction_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        k::Proposition conjunction = k::Proposition::falsity();
        k::ProofTerm evidence = k::ProofTerm::reflexivity();
        const auto existing =
            find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::And>(p.node); });
        if (existing && one_in(2)) {
            conjunction = available(scope, *existing);
            evidence = use(scope, *existing);
        } else {
            Derivation both = conjunction_of(scope, depth);
            conjunction = std::move(both.proposition);
            evidence = std::move(both.proof);
        }
        const bool right = one_in(2);
        const auto& sides = std::get<k::And>(conjunction.node);
        k::Proposition conclusion = right ? *sides.right : *sides.left;
        bool stated_right = right;
        if (perturb()) {
            stated_right = !right;
        }
        Derivation derived{std::move(conclusion),
                           k::ProofTerm::conjunction_elimination(conjunction, std::move(evidence), stated_right)};
        return discharge(scope, mark, std::move(derived));
    }

    // Evidence for a disjunction by one of its sides; premises it supposes stay
    // in scope.
    Derivation disjunction_of(Scope& scope, unsigned depth) {
        Derivation side = premise(scope, depth);
        k::Proposition other = proposition(scope.locals, 2);
        const bool right = one_in(2);
        k::Proposition conclusion = right ? k::Proposition::disjunction(std::move(other), side.proposition)
                                          : k::Proposition::disjunction(side.proposition, std::move(other));
        return Derivation{std::move(conclusion), k::ProofTerm::disjunction_introduction(std::move(side.proof), right)};
    }

    Derivation disjunction_introduction(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        Derivation one = disjunction_of(scope, depth);
        if (perturb()) {
            auto& introduction = std::get<k::DisjunctionIntroduction>(one.proof.node);
            introduction.right = !introduction.right;
        }
        return discharge(scope, mark, std::move(one));
    }

    Derivation disjunction_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        k::Proposition disjunction = k::Proposition::falsity();
        k::ProofTerm evidence = k::ProofTerm::reflexivity();
        const auto existing =
            find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Or>(p.node); });
        if (existing && one_in(2)) {
            disjunction = available(scope, *existing);
            evidence = use(scope, *existing);
        } else if (one_in(2)) {
            disjunction = k::Proposition::disjunction(proposition(scope.locals, 2), proposition(scope.locals, 2));
            const std::size_t position = suppose(scope, disjunction);
            evidence = use(scope, position);
        } else {
            Derivation one = disjunction_of(scope, depth);
            disjunction = std::move(one.proposition);
            evidence = std::move(one.proof);
        }
        const auto& sides = std::get<k::Or>(disjunction.node);
        const k::Proposition& left = *sides.left;
        const k::Proposition& right = *sides.right;

        k::Proposition conclusion = k::Proposition::falsity();
        k::ProofTerm left_case = k::ProofTerm::reflexivity();
        k::ProofTerm right_case = k::ProofTerm::reflexivity();
        std::uint32_t lifted = 0;
        switch (below(3)) {
            case 0: {
                // The disjunction with its sides exchanged.
                conclusion = k::Proposition::disjunction(right, left);
                left_case = k::ProofTerm::implication_introduction(
                    left,
                    k::ProofTerm::disjunction_introduction(k::ProofTerm::hypothesis(k::HypothesisIndex{0}), true));
                right_case = k::ProofTerm::implication_introduction(
                    right,
                    k::ProofTerm::disjunction_introduction(k::ProofTerm::hypothesis(k::HypothesisIndex{0}), false));
                break;
            }
            case 1: {
                // A goal that follows from neither side in particular.
                const std::size_t placeholder = suppose(scope, k::Proposition::falsity(), false);
                Derivation body = derive(scope, depth - 1);
                scope.hypotheses.erase(scope.hypotheses.begin() + static_cast<std::ptrdiff_t>(placeholder));
                conclusion = body.proposition;
                left_case = k::ProofTerm::implication_introduction(left, body.proof);
                right_case = k::ProofTerm::implication_introduction(right, std::move(body.proof));
                break;
            }
            default: {
                // Each case supposed.
                conclusion = proposition(scope.locals, 2);
                const std::size_t from_left = suppose(scope, k::Proposition::implication(left, conclusion));
                const std::size_t from_right = suppose(scope, k::Proposition::implication(right, conclusion));
                left_case = use(scope, from_left);
                right_case = use(scope, from_right);
                lifted = 2;
                break;
            }
        }
        if (perturb()) {
            std::swap(left_case, right_case);
        }
        Derivation derived{std::move(conclusion),
                           k::ProofTerm::disjunction_elimination(disjunction, lift(evidence, lifted),
                                                                 std::move(left_case), std::move(right_case))};
        return discharge(scope, mark, std::move(derived));
    }

    // Evidence for `False` from the premises in scope, where there is any.
    std::optional<k::ProofTerm> contradiction(Scope& scope) {
        if (const auto supposed =
                find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Falsity>(p.node); });
            supposed && one_in(2)) {
            return use(scope, *supposed);
        }
        return arithmetic_from_scope(scope, k::Proposition::falsity());
    }

    // Linear arithmetic over the equalities in scope, closing `goal`.
    std::optional<k::ProofTerm> arithmetic_from_scope(const Scope& scope, const k::Proposition& goal) {
        std::vector<k::ArithmeticFact> facts;
        std::vector<k::Proposition> stated;
        for (std::size_t position = 0; position < scope.hypotheses.size(); ++position) {
            if (!scope.hypotheses[position].usable) {
                continue;
            }
            k::Proposition fact = available(scope, position);
            if (!std::holds_alternative<k::Eq>(fact.node) || !std::get<k::Eq>(fact.node).type.is_integer() ||
                one_in(4)) {
                continue;
            }
            stated.push_back(fact);
            facts.push_back(k::ArithmeticFact{std::move(fact), k::Box<k::ProofTerm>{use(scope, position)}});
        }
        auto certificate = propose_certificate(scope.locals, stated, goal);
        if (!certificate) {
            return std::nullopt;
        }
        return k::ProofTerm::linear_arithmetic(std::move(facts), std::move(*certificate));
    }

    std::optional<k::ArithmeticCertificate> propose_certificate(std::span<const k::Type> locals,
                                                                std::span<const k::Proposition> facts,
                                                                const k::Proposition& goal) {
        const auto system = k::arithmetic_system(context_, facts, goal, k::CoreLimits{}, locals);
        if (!system) {
            return std::nullopt;
        }
        auto certificate = refutation::refute(*system);
        if (!certificate) {
            if (one_in(3)) {
                return random_certificate(system->constraints.size(), system->variables.size(),
                                          system->disjunctions.size(), 2);
            }
            return std::nullopt;
        }
        if (perturb()) {
            return corrupt(*certificate, system->constraints.size());
        }
        return certificate;
    }

    k::ArithmeticCertificate random_certificate(std::size_t constraints, std::size_t variables,
                                                std::size_t disjunctions, unsigned depth) {
        const auto count = [](std::size_t n) {
            return static_cast<std::uint32_t>(n + 2);
        };
        switch (depth == 0 ? 0 : below(3)) {
            case 1: {
                std::vector<std::pair<std::uint32_t, std::int64_t>> terms;
                terms.emplace_back(below(count(variables)), static_cast<std::int64_t>(below(5)) - 2);
                const auto constant = static_cast<std::int64_t>(below(5)) - 2;
                auto at_most_zero = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
                auto at_least_one = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
                return k::ArithmeticCertificate{k::IntegerSplit{
                    std::move(terms), constant, k::Box<k::ArithmeticCertificate>{std::move(at_most_zero)},
                    k::Box<k::ArithmeticCertificate>{std::move(at_least_one)}}};
            }
            case 2: {
                const std::uint32_t disjunction = below(count(disjunctions));
                auto first = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
                auto second = random_certificate(constraints + 1, variables, disjunctions, depth - 1);
                return k::ArithmeticCertificate{
                    k::DisjunctionCases{disjunction, k::Box<k::ArithmeticCertificate>{std::move(first)},
                                        k::Box<k::ArithmeticCertificate>{std::move(second)}}};
            }
            default: {
                k::FarkasSum sum;
                std::uint32_t position = below(3);
                const std::uint32_t used = 1 + below(4);
                for (std::uint32_t term = 0; term < used; ++term) {
                    sum.multipliers.emplace_back(position, Wide{below(4)});
                    position += 1 + below(3);
                }
                (void)constraints;
                return k::ArithmeticCertificate{std::move(sum)};
            }
        }
    }

    // A proposed certificate with one of its steps changed.
    k::ArithmeticCertificate corrupt(const k::ArithmeticCertificate& certificate, std::size_t constraints) {
        k::ArithmeticCertificate result = certificate;
        if (auto* sum = std::get_if<k::FarkasSum>(&result.node)) {
            if (sum->multipliers.empty()) {
                return result;
            }
            auto& chosen = sum->multipliers[below(static_cast<std::uint32_t>(sum->multipliers.size()))];
            switch (below(4)) {
                case 0:
                    chosen.second += 1;
                    break;
                case 1:
                    chosen.second = chosen.second > 1 ? chosen.second - 1 : chosen.second + 2;
                    break;
                case 2:
                    sum->multipliers.erase(
                        sum->multipliers.begin() +
                        static_cast<std::ptrdiff_t>(below(static_cast<std::uint32_t>(sum->multipliers.size()))));
                    break;
                default:
                    chosen.first = static_cast<std::uint32_t>((chosen.first + 1) % (constraints + 1));
                    break;
            }
            return result;
        }
        if (auto* split = std::get_if<k::IntegerSplit>(&result.node)) {
            if (one_in(2)) {
                split->constant += 1;
            } else {
                split->at_most_zero = k::Box<k::ArithmeticCertificate>{corrupt(*split->at_most_zero, constraints + 1)};
            }
            return result;
        }
        auto& cases = std::get<k::DisjunctionCases>(result.node);
        if (one_in(2)) {
            std::swap(cases.first, cases.second);
        } else {
            cases.first = k::Box<k::ArithmeticCertificate>{corrupt(*cases.first, constraints + 1)};
        }
        return result;
    }

    Derivation falsity_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        std::optional<k::ProofTerm> evidence = contradiction(scope);
        if (!evidence) {
            if (one_in(2)) {
                const std::size_t position = suppose(scope, k::Proposition::falsity());
                evidence = use(scope, position);
            } else {
                // Two comparisons that cannot both hold, supposed.
                const k::IntType at = small_integer();
                const k::Term subject = integer_term(scope.locals, at, 1);
                const k::Term bound = literal(at);
                suppose(scope, k::Proposition::equality(as_type(kBoolean),
                                                        k::Term::primitive(k::PrimOp::Less, at, {subject, bound}),
                                                        k::Term::literal(kBoolean, 1)));
                suppose(scope, k::Proposition::equality(
                                   as_type(kBoolean),
                                   k::Term::primitive(one_in(4) ? k::PrimOp::Less : k::PrimOp::GreaterEqual, at,
                                                      {subject, bound}),
                                   k::Term::literal(kBoolean, 1)));
                evidence = arithmetic_from_scope(scope, k::Proposition::falsity());
                if (!evidence) {
                    evidence = derive(scope, depth - 1).proof;
                }
            }
        }
        k::ProofTerm stated = std::move(*evidence);
        if (perturb()) {
            stated = derive(scope, depth - 1).proof;
        }
        Derivation derived{proposition(scope.locals, 2), k::ProofTerm::falsity_elimination(std::move(stated))};
        return discharge(scope, mark, std::move(derived));
    }

    Derivation equality_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        k::Type type = as_type(small_integer());
        k::Term lhs = k::Term::literal(k::IntType{1, k::Signedness::Unsigned}, 0);
        k::Term rhs = lhs;
        k::ProofTerm equality = k::ProofTerm::reflexivity();
        const auto existing =
            find(scope, [](const k::Proposition& p) { return std::holds_alternative<k::Eq>(p.node); });
        const std::uint32_t source = below(3);
        if (source == 0 && existing) {
            const k::Proposition stated = available(scope, *existing);
            const auto& eq = std::get<k::Eq>(stated.node);
            type = eq.type;
            lhs = eq.lhs;
            rhs = eq.rhs;
            equality = use(scope, *existing);
        } else if (source == 1) {
            type = one_in(4) ? value_v() : as_type(any_integer());
            auto a = term(scope.locals, type, 2);
            auto b = term(scope.locals, type, 2);
            if (!a || !b) {
                type = as_type(small_integer());
                a = integer_term(scope.locals, type.integer_type(), 2);
                b = integer_term(scope.locals, type.integer_type(), 2);
            }
            lhs = std::move(*a);
            rhs = std::move(*b);
            const std::size_t position = suppose(scope, k::Proposition::equality(type, lhs, rhs));
            equality = use(scope, position);
        } else {
            const k::IntType at = any_integer();
            type = as_type(at);
            lhs = integer_term(scope.locals, at, 2);
            rhs = equivalent(scope.locals, lhs, 2);
        }

        std::vector<k::Type> under = scope.locals;
        under.push_back(type);
        k::Proposition motive = k::Proposition::falsity();
        k::ProofTerm evidence = k::ProofTerm::reflexivity();
        std::uint32_t lifted_equality = 0;
        switch (below(4)) {
            case 0: {
                // Congruence: f(hole) = f(rhs).
                const k::IntType at = any_integer();
                const k::Term shape = integer_term(under, at, 1 + depth % 3);
                motive = k::Proposition::equality(as_type(at), shape, k::shift(k::instantiate(shape, rhs), 1));
                break;
            }
            case 1:
                // Symmetry: rhs = hole.
                motive = k::Proposition::equality(type, k::shift(rhs, 1), k::Term::variable(k::VarIndex{0}));
                break;
            case 2: {
                // A premise in scope, with rhs abstracted.
                const auto position = find(scope, [](const k::Proposition&) { return true; });
                if (position) {
                    motive = abstract(k::shift(available(scope, *position), 1), k::shift(rhs, 1), 0);
                    if (k::instantiate(motive, rhs) == available(scope, *position)) {
                        evidence = use(scope, *position);
                        break;
                    }
                }
                [[fallthrough]];
            }
            default: {
                // Supposed at rhs.
                motive = proposition(under, 2);
                const std::size_t position = suppose(scope, k::instantiate(motive, rhs));
                evidence = use(scope, position);
                lifted_equality = 1;
                break;
            }
        }
        k::Proposition conclusion = k::instantiate(motive, lhs);
        k::Term stated_lhs = lhs;
        k::Term stated_rhs = rhs;
        k::Type stated_type = type;
        if (perturb()) {
            switch (below(4)) {
                case 0:
                    // Transported the wrong way.
                    std::swap(stated_lhs, stated_rhs);
                    break;
                case 1:
                    conclusion = k::instantiate(motive, rhs);
                    std::swap(stated_lhs, stated_rhs);
                    break;
                case 2:
                    stated_type = as_type(any_integer());
                    break;
                default:
                    return misclaim(scope, discharge(scope, mark,
                                                     Derivation{std::move(conclusion),
                                                                k::ProofTerm::equality_elimination(
                                                                    type, lhs, rhs, motive,
                                                                    lift(equality, lifted_equality), evidence)}));
            }
        }
        Derivation derived{std::move(conclusion),
                           k::ProofTerm::equality_elimination(std::move(stated_type), std::move(stated_lhs),
                                                              std::move(stated_rhs), std::move(motive),
                                                              lift(equality, lifted_equality), std::move(evidence))};
        return discharge(scope, mark, std::move(derived));
    }

    Derivation conditional_elimination(Scope& scope, unsigned depth) {
        const std::size_t mark = scope.hypotheses.size();
        const k::IntType at = small_integer();
        const k::Type type = as_type(at);
        const k::Term condition = integer_term(scope.locals, kBoolean, 2);
        const k::Term when_true = integer_term(scope.locals, at, 2);
        const k::Term when_false = integer_term(scope.locals, at, 2);
        const k::Term selected = k::Term::primitive(k::PrimOp::Select, at, {condition, when_true, when_false});

        std::vector<k::Type> under = scope.locals;
        under.push_back(type);
        k::Proposition motive = k::Proposition::falsity();
        k::ProofTerm true_case = k::ProofTerm::reflexivity();
        k::ProofTerm false_case = k::ProofTerm::reflexivity();
        const k::Proposition if_true = k::predicate(condition, true);
        const k::Proposition if_false = k::predicate(condition, false);
        switch (below(3)) {
            case 0: {
                // Both cases supposed.
                motive = one_in(2) ? proposition(under, 2)
                                   : k::Proposition::equality(as_type(at), k::Term::variable(k::VarIndex{0}),
                                                              integer_term(under, at, 2));
                const bool exchanged = perturb();
                const std::size_t first =
                    suppose(scope, k::Proposition::implication(exchanged ? if_false : if_true,
                                                               k::instantiate(motive, when_true)));
                const std::size_t second =
                    suppose(scope, k::Proposition::implication(exchanged ? if_true : if_false,
                                                               k::instantiate(motive, when_false)));
                true_case = use(scope, first);
                false_case = use(scope, second);
                break;
            }
            case 1: {
                // The selection restated in each case, from the premise alone.
                motive = k::Proposition::equality(type, k::Term::variable(k::VarIndex{0}), k::shift(selected, 1));
                true_case = restate(scope, if_true, k::instantiate(motive, when_true));
                false_case = restate(scope, if_false, k::instantiate(motive, when_false));
                break;
            }
            default: {
                const k::IntType other = any_integer();
                const k::Term shape = integer_term(under, other, 2);
                motive = k::Proposition::equality(as_type(other), shape, shape);
                true_case = k::ProofTerm::implication_introduction(if_true, k::ProofTerm::reflexivity());
                false_case = k::ProofTerm::implication_introduction(if_false, k::ProofTerm::reflexivity());
                (void)depth;
                break;
            }
        }
        k::Proposition conclusion = k::instantiate(motive, selected);
        k::Term stated_true = when_true;
        k::Term stated_false = when_false;
        if (perturb()) {
            switch (below(3)) {
                case 0:
                    std::swap(stated_true, stated_false);
                    conclusion = k::instantiate(
                        motive, k::Term::primitive(k::PrimOp::Select, at, {condition, stated_true, stated_false}));
                    break;
                case 1:
                    std::swap(true_case, false_case);
                    break;
                default:
                    conclusion = k::instantiate(
                        motive, k::Term::primitive(k::PrimOp::Select, at, {condition, when_false, when_true}));
                    break;
            }
        }
        Derivation derived{std::move(conclusion), k::ProofTerm::conditional_elimination(
                                                      type, condition, std::move(stated_true), std::move(stated_false),
                                                      std::move(motive), std::move(true_case), std::move(false_case))};
        return discharge(scope, mark, std::move(derived));
    }

    // Evidence for `premise -> goal`, where `premise` is an equality whose left
    // side, replaced by its right, turns `goal` into a reflexive equality.
    static k::ProofTerm restate(const Scope& /*scope*/, const k::Proposition& premise, const k::Proposition& goal) {
        const auto& equality = std::get<k::Eq>(premise.node);
        const k::Proposition motive = abstract(k::shift(goal, 1), k::shift(equality.lhs, 1), 0);
        return k::ProofTerm::implication_introduction(
            premise, k::ProofTerm::equality_elimination(equality.type, equality.lhs, equality.rhs, motive,
                                                        k::ProofTerm::hypothesis(k::HypothesisIndex{0}),
                                                        k::ProofTerm::reflexivity()));
    }

    Derivation linear_arithmetic(Scope& scope) {
        const std::size_t mark = scope.hypotheses.size();
        const std::uint32_t supposed = below(3);
        for (std::uint32_t index = 0; index < supposed; ++index) {
            suppose(scope, comparison_proposition(scope.locals, 2));
        }
        k::Proposition goal = one_in(5) ? k::Proposition::falsity() : comparison_proposition(scope.locals, 2);
        auto proof = arithmetic_from_scope(scope, goal);
        if (!proof) {
            return discharge(scope, mark, reflexivity(scope, 0));
        }
        return discharge(scope, mark, Derivation{std::move(goal), std::move(*proof)});
    }

    // ---- the arithmetic mode ------------------------------------------------

    // forall binders. F1 -> ... -> Fn -> G, closed by linear arithmetic.
    Derivation arithmetic() {
        Scope scope;
        const std::uint32_t binders = 1 + below(3);
        for (std::uint32_t index = 0; index < binders; ++index) {
            scope.locals.push_back(
                one_in(6) ? as_type(k::IntType{8, one_in(2) ? k::Signedness::Signed : k::Signedness::Unsigned})
                          : as_type(small_integer()));
        }
        const std::uint32_t facts = below(4);
        for (std::uint32_t index = 0; index < facts; ++index) {
            suppose(scope, comparison_proposition(scope.locals, 2));
        }
        k::Proposition goal = one_in(6) ? k::Proposition::falsity() : comparison_proposition(scope.locals, 2);
        std::vector<k::ArithmeticFact> used;
        std::vector<k::Proposition> stated;
        for (std::size_t position = 0; position < scope.hypotheses.size(); ++position) {
            if (one_in(6)) {
                continue;
            }
            stated.push_back(scope.hypotheses[position].proposition);
            used.push_back(
                k::ArithmeticFact{scope.hypotheses[position].proposition, k::Box<k::ProofTerm>{use(scope, position)}});
        }
        auto certificate = propose_certificate(scope.locals, stated, goal);
        k::ProofTerm proof = certificate ? k::ProofTerm::linear_arithmetic(std::move(used), std::move(*certificate))
                                         : k::ProofTerm::reflexivity();
        Derivation derived = discharge(scope, 0, Derivation{std::move(goal), std::move(proof)});
        while (!scope.locals.empty()) {
            const k::Type bound = scope.locals.back();
            scope.locals.pop_back();
            derived.proposition = k::Proposition::for_all(bound, std::move(derived.proposition));
            derived.proof = k::ProofTerm::forall_introduction(bound, std::move(derived.proof));
        }
        return derived;
    }

    // ---- the automation mode ------------------------------------------------

    k::Proposition automation_goal() {
        std::vector<k::Type> locals;
        const std::uint32_t binders = below(3);
        for (std::uint32_t index = 0; index < binders; ++index) {
            locals.push_back(as_type(small_integer()));
        }
        std::vector<k::Proposition> premises;
        const std::uint32_t count = below(3);
        for (std::uint32_t index = 0; index < count; ++index) {
            premises.push_back(comparison_proposition(locals, 2));
        }
        k::Proposition goal = one_in(4) ? k::Proposition::disjunction(comparison_proposition(locals, 2),
                                                                      comparison_proposition(locals, 2))
                                        : comparison_proposition(locals, 2);
        for (auto premise = premises.rbegin(); premise != premises.rend(); ++premise) {
            goal = k::Proposition::implication(*premise, std::move(goal));
        }
        while (!locals.empty()) {
            goal = k::Proposition::for_all(locals.back(), std::move(goal));
            locals.pop_back();
        }
        return goal;
    }

  public:
    TermSample term_sample() {
        define_some();
        TermSample sample{
            context_, {}, k::Term::literal(kBoolean, 0), k::Proposition::falsity(), k::Term::literal(kBoolean, 0)};
        const std::uint32_t binders = 1 + below(3);
        for (std::uint32_t index = 0; index < binders; ++index) {
            sample.locals.push_back(one_in(5) ? (one_in(2) ? value_v() : indexed_a()) : as_type(small_integer()));
        }
        const k::IntType at = one_in(3) ? any_integer() : small_integer();
        sample.term = integer_term(sample.locals, at, 1 + below(4));
        sample.proposition = proposition(sample.locals, 2);
        const std::span<const k::Type> outer(sample.locals.data(), sample.locals.size() - 1);
        auto replacement = term(outer, sample.locals.back(), 2);
        if (!replacement) {
            // No term of an abstract type can be formed without a variable of
            // it, so the innermost binder becomes an integer.
            sample.locals.back() = as_type(small_integer());
            sample.term = integer_term(sample.locals, at, 2);
            sample.proposition = proposition(sample.locals, 2);
            replacement = integer_term(outer, sample.locals.back().integer_type(), 2);
        }
        sample.argument = std::move(*replacement);
        sample.context = context_;
        return sample;
    }

  private:
    Choices& choices_;
    k::Context context_;
};

} // namespace

Sample generate(Choices& choices, Mode mode) {
    Generator generator(choices);
    return generator.run(mode);
}

TermSample generate_term(Choices& choices) {
    Generator generator(choices);
    return generator.term_sample();
}

} // namespace cppl::testing::kernel_generator
