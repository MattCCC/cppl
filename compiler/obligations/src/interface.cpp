#include "cppl/obligations/interface.hpp"

#include "cppl/artifact/interface.hpp"
#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/vir/capability.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/module.hpp"
#include "cppl/vir/place.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations {

namespace detail {
namespace {

// Every alternative of a kernel type, term and proposition is encoded below. A
// new one must be given an encoding here before this compiles again, or two
// different statements could share an identity (SPEC.md TUBOUND-004).
static_assert(std::variant_size_v<decltype(kernel::Type::node)> == 3,
              "a kernel type former was added or removed: encode it in CanonicalEncoder::type");
static_assert(std::variant_size_v<decltype(kernel::Term::node)> == 6,
              "a kernel term former was added or removed: encode it in CanonicalEncoder::term");
static_assert(std::variant_size_v<decltype(kernel::Proposition::node)> == 6,
              "a kernel proposition former was added or removed: encode it in CanonicalEncoder::proposition");

// Encodes formal content so that its identity does not depend on the unit that
// stated it.
//
// Within one unit a call names a definition by the number the unit admitted it
// under, which depends on what else the unit defines and in what order. Two
// units stating one contract over one pure function would then disagree. Here a
// definition is instead numbered by where the content first reaches it, and
// encoded by its own content: parameter types, result type and body. Its name
// is not part of it, so a definition is identified by what it computes, never
// by what it is called.
class CanonicalEncoder {
  public:
    CanonicalEncoder(const kernel::Context& context, source::Hasher& hasher) : context_(context), hasher_(hasher) {}

    void type(const kernel::Type& type) {
        if (const auto* integer = std::get_if<kernel::IntType>(&type.node)) {
            hasher_.update_u8(1);
            integer_type(*integer);
        } else if (const auto* value = std::get_if<kernel::ValueType>(&type.node)) {
            hasher_.update_u8(2);
            hasher_.update_field(value->identity);
            hasher_.update_u64(value->projections.size());
            for (const kernel::Type& projection : value->projections) {
                this->type(projection);
            }
        } else {
            const auto& indexed = std::get<kernel::IndexedType>(type.node);
            hasher_.update_u8(3);
            wide(indexed.extent);
            hasher_.update_u64(indexed.element.size());
            for (const kernel::Type& element : indexed.element) {
                this->type(element);
            }
        }
    }

    void term(const kernel::Term& term) {
        std::visit(
            [this](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, kernel::Var>) {
                    hasher_.update_u8(10);
                    hasher_.update_u64(node.index.value);
                } else if constexpr (std::is_same_v<Node, kernel::Literal>) {
                    hasher_.update_u8(11);
                    integer_type(node.type);
                    wide(node.value);
                } else if constexpr (std::is_same_v<Node, kernel::Call>) {
                    hasher_.update_u8(12);
                    hasher_.update_u64(definition(node.callee));
                    arguments(node.arguments);
                } else if constexpr (std::is_same_v<Node, kernel::Prim>) {
                    hasher_.update_u8(13);
                    hasher_.update_u8(static_cast<std::uint8_t>(node.op));
                    integer_type(node.type);
                    arguments(node.arguments);
                } else if constexpr (std::is_same_v<Node, kernel::Projection>) {
                    hasher_.update_u8(14);
                    this->type(node.domain);
                    hasher_.update_u64(node.index);
                    arguments(node.arguments);
                } else {
                    static_assert(std::is_same_v<Node, kernel::Element>);
                    hasher_.update_u8(15);
                    this->type(node.domain);
                    arguments(node.arguments);
                }
            },
            term.node);
    }

    void proposition(const kernel::Proposition& proposition) {
        std::visit(
            [this](const auto& node) {
                using Node = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<Node, kernel::Eq>) {
                    hasher_.update_u8(21);
                    this->type(node.type);
                    this->term(node.lhs);
                    this->term(node.rhs);
                } else if constexpr (std::is_same_v<Node, kernel::Forall>) {
                    hasher_.update_u8(20);
                    this->type(node.binder);
                    this->proposition(*node.body);
                } else if constexpr (std::is_same_v<Node, kernel::Implies>) {
                    hasher_.update_u8(22);
                    this->proposition(*node.premise);
                    this->proposition(*node.conclusion);
                } else if constexpr (std::is_same_v<Node, kernel::And>) {
                    hasher_.update_u8(23);
                    this->proposition(*node.left);
                    this->proposition(*node.right);
                } else if constexpr (std::is_same_v<Node, kernel::Or>) {
                    hasher_.update_u8(24);
                    this->proposition(*node.left);
                    this->proposition(*node.right);
                } else {
                    static_assert(std::is_same_v<Node, kernel::Falsity>);
                    hasher_.update_u8(25);
                }
            },
            proposition.node);
    }

    // Encodes every definition reached so far, and every one those reach, in
    // the order first reached. Fails when a call names a definition the unit
    // does not have, which no well-formed statement does.
    [[nodiscard]] bool definitions() {
        hasher_.update_u8(30);
        // Encoding a body may reach further definitions, which join the end.
        for (std::size_t position = 0; position < order_.size(); ++position) {
            const kernel::Definition* found = context_.lookup(kernel::DefId{order_[position]});
            if (found == nullptr) {
                return false;
            }
            hasher_.update_u64(position);
            hasher_.update_u64(found->parameters.size());
            for (const kernel::Type& parameter : found->parameters) {
                type(parameter);
            }
            type(found->result);
            term(found->body);
        }
        hasher_.update_u64(order_.size());
        return true;
    }

  private:
    void integer_type(const kernel::IntType& integer) {
        hasher_.update_u64(integer.width);
        hasher_.update_u8(integer.signedness == kernel::Signedness::Signed ? 1 : 0);
    }

    // Both halves: a value is 128 bits wide, so hashing only the low half would
    // give two distinct values one identity.
    void wide(kernel::Wide value) {
        const auto bits = static_cast<kernel::WideUnsigned>(value);
        hasher_.update_u64(static_cast<std::uint64_t>(bits));
        hasher_.update_u64(static_cast<std::uint64_t>(bits >> 64U));
    }

    void arguments(const std::vector<kernel::Term>& arguments) {
        hasher_.update_u64(arguments.size());
        for (const kernel::Term& argument : arguments) {
            term(argument);
        }
    }

    std::uint64_t definition(kernel::DefId id) {
        const auto [found, added] = canonical_.emplace(id.value, order_.size());
        if (added) {
            order_.push_back(id.value);
        }
        return found->second;
    }

    const kernel::Context& context_;
    source::Hasher& hasher_;
    std::map<std::uint32_t, std::uint64_t> canonical_;
    std::vector<std::uint32_t> order_;
};

void place(source::Hasher& hasher, const vir::Place& at) {
    hasher.update_u8(static_cast<std::uint8_t>(at.root.kind));
    hasher.update_u64(at.root.id);
    hasher.update_u64(at.root.version);
    hasher.update_u64(at.path.size());
    for (const vir::PlaceStep& step : at.path) {
        hasher.update_u8(static_cast<std::uint8_t>(step.kind));
        hasher.update_u64(step.index);
        hasher.update_u64(step.symbol);
    }
}

} // namespace

std::expected<Statement, Failure> state_statement(const vir::Function& function, const ContractVerification& plan,
                                                  const kernel::Context& context,
                                                  const DefinitionMap& pure_definitions) {
    if (!function.contract.has_value() || function.parameters.size() != plan.parameters.size()) {
        return std::unexpected(Failure{"the contract is not stated", function.range.begin, {}});
    }
    source::Hasher hasher;
    CanonicalEncoder encoder(context, hasher);
    hasher.update_field("cppl-contract-statement-v1");
    hasher.update_field(kernel::kFormalCoreVersion);
    hasher.update_field(kernel::kKernelVersion);

    // Parameters and result, with how each parameter is passed: a reference
    // parameter's post-state is part of what the contract says.
    hasher.update_u64(plan.parameters.size());
    for (std::size_t index = 0; index < plan.parameters.size(); ++index) {
        encoder.type(plan.parameters[index]);
        hasher.update_u8(static_cast<std::uint8_t>(function.parameters[index].passing));
    }
    encoder.type(plan.result);

    // The preconditions, refined parameters' predicates included, and the
    // postcondition, a refined result's and reference parameters' included.
    hasher.update_u64(plan.preconditions.size());
    for (const kernel::Proposition& precondition : plan.preconditions) {
        encoder.proposition(precondition);
    }
    encoder.proposition(plan.postcondition);

    // Memory capabilities are owed at every call, so they are part of what a
    // caller must meet; a sized one's count is a term over the parameters.
    const vir::Contract& contract = *function.contract;
    hasher.update_u64(contract.capabilities.size());
    for (const vir::Capability& capability : contract.capabilities) {
        hasher.update_u8(static_cast<std::uint8_t>(capability.kind));
        place(hasher, capability.place);
        hasher.update_u64(capability.extent.size());
        for (const vir::Expr& count : capability.extent) {
            auto lowered = lower_value(count, pure_definitions, plan.parameters.size());
            if (!lowered) {
                return std::unexpected(lowered.error());
            }
            encoder.term(*lowered);
        }
    }

    // A measure is what a recursive caller compares against, so it too is
    // part of the statement.
    hasher.update_u64(contract.measures.size());
    for (const vir::Expr& measure : contract.measures) {
        auto lowered = lower_value(measure, pure_definitions, plan.parameters.size());
        if (!lowered) {
            return std::unexpected(lowered.error());
        }
        encoder.term(*lowered);
    }

    if (!encoder.definitions()) {
        return std::unexpected(
            Failure{"the contract calls a definition the formal core does not have", function.range.begin, {}});
    }

    // forall parameters. P1 -> ... -> Pn -> forall result. Q, as the kernel
    // prints it.
    kernel::Proposition described = kernel::Proposition::for_all(plan.result, plan.postcondition);
    for (const kernel::Proposition& precondition : std::views::reverse(plan.preconditions)) {
        described = kernel::Proposition::implication(precondition, std::move(described));
    }
    for (const kernel::Type& parameter : std::views::reverse(plan.parameters)) {
        described = kernel::Proposition::for_all(parameter, std::move(described));
    }
    return Statement{hasher.finish(), kernel::describe(described)};
}

} // namespace detail

const ImportedEntry* Imports::find(std::string_view symbol) const {
    const auto found = std::ranges::find_if(
        entries, [symbol](const ImportedEntry& candidate) { return candidate.entry.symbol == symbol; });
    return found == entries.end() ? nullptr : &*found;
}

const RefusedEntry* Imports::refusal(std::string_view symbol) const {
    const auto found =
        std::ranges::find_if(refused, [symbol](const RefusedEntry& candidate) { return candidate.symbol == symbol; });
    return found == refused.end() ? nullptr : &*found;
}

std::vector<artifact::Entry> exported_contracts(const Program& program, const TrustClosure& closure) {
    std::vector<artifact::Entry> exported;
    for (const ClaimClosure& claim : closure.claims) {
        if (claim.kind != ClaimKind::Contract || claim.symbol.empty()) {
            continue;
        }
        const auto contract = std::ranges::find_if(program.contracts, [&claim](const ContractVerification& candidate) {
            return candidate.symbol == claim.symbol && !candidate.imported.has_value();
        });
        if (contract == program.contracts.end()) {
            continue;
        }
        const std::optional<source::Digest>& statement = contract->statement;
        if (!statement.has_value()) {
            continue;
        }
        artifact::Entry entry;
        entry.symbol = claim.symbol;
        entry.name = claim.subject;
        entry.statement = *statement;
        entry.contract = contract->description;
        entry.correctness = claim.total ? artifact::Correctness::Total : artifact::Correctness::Partial;
        for (const TrustedPremise& premise : claim.premises) {
            entry.premises.push_back(
                artifact::Premise{premise.identity.digest, premise.name, premise.location.file, premise.location.line});
        }
        for (const UnsafeDependency& dependency : claim.unsafe) {
            entry.unsafe.push_back(
                artifact::UnsafeBlock{dependency.location.file, dependency.location.line, dependency.location.column});
        }
        // What a contract of another unit rests on is carried on, so a unit
        // that imports this one's sees all of it (SPEC.md TUBOUND-006, TUBOUND-009).
        for (const ImportedDependency& imported : claim.imported) {
            entry.premises.insert(entry.premises.end(), imported.premises.begin(), imported.premises.end());
            entry.unsafe.insert(entry.unsafe.end(), imported.unsafe.begin(), imported.unsafe.end());
            entry.depends.push_back(artifact::Dependency{imported.symbol, imported.entry});
            entry.depends.insert(entry.depends.end(), imported.depends.begin(), imported.depends.end());
        }
        exported.push_back(std::move(entry));
    }
    return exported;
}

} // namespace cppl::obligations
