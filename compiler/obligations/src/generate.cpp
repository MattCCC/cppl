#include "cppl/obligations/generate.hpp"

#include <algorithm>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <variant>

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/substitution.hpp"
#include "cppl/kernel/version.hpp"
#include "cppl/source/digest.hpp"

namespace cppl::obligations {

namespace {

struct Failure {
    std::string reason;
    source::SourceLocation location;
    // Set when the failure is a definition the core does not have, so the
    // diagnostic can explain why that definition was not admitted.
    std::string missing_symbol;
};

std::unexpected<Failure> fail(std::string reason, const source::SourceLocation& location) {
    return std::unexpected(Failure{std::move(reason), location, {}});
}

std::optional<kernel::Type> lower_type(const vir::Type& type) {
    if (!type.is_integer()) {
        return std::nullopt;
    }
    const vir::IntType& integer = type.integer_type();
    return kernel::Type::integer(
        integer.width,
        integer.is_signed ? kernel::Signedness::Signed : kernel::Signedness::Unsigned);
}

using DefinitionMap = std::map<std::string, kernel::DefId>;

// Lowers a VIR value expression into a core term.
//
// The core is total: every primitive it offers is defined on every input. A C++
// operator may be lowered onto one only when the C++ operator is equally total.
// Where it is not, the lowering refuses rather than pretending (SPEC.md 29, 31).
class TermLowering {
public:
    TermLowering(const DefinitionMap& definitions, std::size_t parameter_count)
        : definitions_(definitions), parameter_count_(parameter_count) {}

    [[nodiscard]] std::expected<kernel::Term, Failure> lower(const vir::Expr& expr) const {
        const source::SourceLocation& location = expr.provenance.range.begin;
        const std::optional<kernel::Type> type = lower_type(expr.type);

        if (const auto* parameter = std::get_if<vir::ParameterRef>(&expr.node)) {
            if (parameter->parameter >= parameter_count_) {
                return fail("parameter reference is outside the declaration's parameter list",
                            location);
            }
            return kernel::Term::variable(
                kernel::parameter_reference(parameter_count_, parameter->parameter));
        }

        if (const auto* literal = std::get_if<vir::IntLiteral>(&expr.node)) {
            if (!type.has_value()) {
                return fail("a literal of type '" + vir::describe(expr.type) +
                                "' has no core representation",
                            location);
            }
            return kernel::Term::literal(type->integer_type(), literal->value);
        }

        if (const auto* call = std::get_if<vir::Call>(&expr.node)) {
            const auto definition = definitions_.find(call->callee.usr);
            if (definition == definitions_.end()) {
                return std::unexpected(
                    Failure{"'" + call->callee_name +
                                "' is not available to the formal core as a definition",
                            location, call->callee.usr});
            }
            std::vector<kernel::Term> arguments;
            arguments.reserve(call->arguments.size());
            for (const vir::Expr& argument : call->arguments) {
                std::expected<kernel::Term, Failure> lowered = lower(argument);
                if (!lowered) {
                    return lowered;
                }
                arguments.push_back(std::move(*lowered));
            }
            return kernel::Term::call(definition->second, std::move(arguments));
        }

        if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
            if (binary->op != vir::BinaryOp::Add) {
                return fail("'" + vir::describe(binary->op) +
                                "' does not denote a value in the formal core",
                            location);
            }
            if (!type.has_value()) {
                return fail("addition at type '" + vir::describe(expr.type) + "' is not modeled",
                            location);
            }

            const kernel::IntType integer = type->integer_type();
            if (integer.signedness == kernel::Signedness::Signed) {
                // Unsigned C++ addition is modular and matches the core's
                // wrapping primitive exactly. Signed C++ addition has undefined
                // behaviour on overflow, so it is not this primitive, and the
                // obligation that would justify the difference is not part of
                // the core yet.
                return fail(
                    "addition on the signed type '" + vir::describe(expr.type) +
                        "' is not modeled: C++ leaves signed overflow undefined, and this "
                        "implementation cannot yet discharge the obligation that it does not "
                        "occur",
                    location);
            }

            std::vector<kernel::Term> operands;
            operands.reserve(binary->operands.size());
            for (const vir::Expr& operand : binary->operands) {
                std::expected<kernel::Term, Failure> lowered = lower(operand);
                if (!lowered) {
                    return lowered;
                }
                operands.push_back(std::move(*lowered));
            }
            return kernel::Term::primitive(kernel::PrimOp::AddWrap, integer, std::move(operands));
        }

        return fail("this expression has no core representation", location);
    }

private:
    const DefinitionMap& definitions_;
    std::size_t parameter_count_;
};

void collect_callees(const vir::Expr& expr, std::set<std::string>& callees) {
    if (const auto* call = std::get_if<vir::Call>(&expr.node)) {
        callees.insert(call->callee.usr);
        for (const vir::Expr& argument : call->arguments) {
            collect_callees(argument, callees);
        }
        return;
    }
    if (const auto* binary = std::get_if<vir::Binary>(&expr.node)) {
        for (const vir::Expr& operand : binary->operands) {
            collect_callees(operand, callees);
        }
    }
}

void encode(source::Hasher& hasher, const kernel::Type& type);
void encode(source::Hasher& hasher, const kernel::Term& term);
void encode(source::Hasher& hasher, const kernel::Proposition& proposition);

void encode(source::Hasher& hasher, const kernel::Type& type) {
    hasher.update_u8(1);
    const kernel::IntType& integer = type.integer_type();
    hasher.update_u64(integer.width);
    hasher.update_u8(integer.signedness == kernel::Signedness::Signed ? 1 : 0);
}

void encode(source::Hasher& hasher, const kernel::Term& term) {
    std::visit(
        [&hasher](const auto& node) {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, kernel::Var>) {
                hasher.update_u8(10);
                hasher.update_u64(node.index.value);
            } else if constexpr (std::is_same_v<Node, kernel::Literal>) {
                hasher.update_u8(11);
                encode(hasher, kernel::Type{node.type});
                hasher.update_u64(static_cast<std::uint64_t>(node.value));
            } else if constexpr (std::is_same_v<Node, kernel::Call>) {
                hasher.update_u8(12);
                hasher.update_u64(node.callee.value);
                hasher.update_u64(node.arguments.size());
                for (const kernel::Term& argument : node.arguments) {
                    encode(hasher, argument);
                }
            } else {
                hasher.update_u8(13);
                hasher.update_u8(static_cast<std::uint8_t>(node.op));
                encode(hasher, kernel::Type{node.type});
                hasher.update_u64(node.arguments.size());
                for (const kernel::Term& argument : node.arguments) {
                    encode(hasher, argument);
                }
            }
        },
        term.node);
}

void encode(source::Hasher& hasher, const kernel::Proposition& proposition) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&proposition.node)) {
        hasher.update_u8(20);
        encode(hasher, quantified->binder);
        encode(hasher, *quantified->body);
        return;
    }
    const auto& equality = std::get<kernel::Eq>(proposition.node);
    hasher.update_u8(21);
    encode(hasher, equality.type);
    encode(hasher, equality.lhs);
    encode(hasher, equality.rhs);
}

void encode(source::Hasher& hasher, const kernel::Definition& definition) {
    hasher.update_u8(30);
    hasher.update_u64(definition.id.value);
    hasher.update_u64(definition.parameters.size());
    for (const kernel::Type& parameter : definition.parameters) {
        encode(hasher, parameter);
    }
    encode(hasher, definition.result);
    encode(hasher, definition.body);
}

void collect_dependencies(const kernel::Context& context,
                          const kernel::Term& term,
                          std::set<std::uint32_t>& reached);

void collect_dependencies(const kernel::Context& context,
                          const kernel::Proposition& proposition,
                          std::set<std::uint32_t>& reached) {
    if (const auto* quantified = std::get_if<kernel::Forall>(&proposition.node)) {
        collect_dependencies(context, *quantified->body, reached);
        return;
    }
    const auto& equality = std::get<kernel::Eq>(proposition.node);
    collect_dependencies(context, equality.lhs, reached);
    collect_dependencies(context, equality.rhs, reached);
}

void collect_dependencies(const kernel::Context& context,
                          const kernel::Term& term,
                          std::set<std::uint32_t>& reached) {
    if (const auto* call = std::get_if<kernel::Call>(&term.node)) {
        if (reached.insert(call->callee.value).second) {
            if (const kernel::Definition* definition = context.lookup(call->callee)) {
                collect_dependencies(context, definition->body, reached);
            }
        }
        for (const kernel::Term& argument : call->arguments) {
            collect_dependencies(context, argument, reached);
        }
        return;
    }
    if (const auto* primitive = std::get_if<kernel::Prim>(&term.node)) {
        for (const kernel::Term& argument : primitive->arguments) {
            collect_dependencies(context, argument, reached);
        }
    }
}

// The identity of an obligation is the content it depends on: the formal core
// and kernel versions, the law it comes from, the goal, and the definitions the
// goal can actually reach. Unrelated definitions elsewhere in the unit do not
// change it.
ObligationId identify(const kernel::Context& context,
                      const std::string& law_name,
                      const kernel::Proposition& goal) {
    std::set<std::uint32_t> reached;
    collect_dependencies(context, goal, reached);

    source::Hasher hasher;
    hasher.update_field(kernel::kFormalCoreVersion);
    hasher.update_field(kernel::kKernelVersion);
    hasher.update_field(law_name);
    hasher.update_u64(reached.size());
    for (std::uint32_t id : reached) {
        if (const kernel::Definition* definition = context.lookup(kernel::DefId{id})) {
            encode(hasher, *definition);
        }
    }
    encode(hasher, goal);
    return ObligationId{hasher.finish()};
}

// Whether evidence for `available` can stand as evidence for `goal`.
//
// The quantifier prefix and the type of the equality must agree: a proof term
// restates its binders, so a term built for one prefix is not a term for
// another. Whether the two equalities themselves coincide is left to the
// kernel, which is the point of `apply`.
bool conclusion_is_applicable(const kernel::Proposition& available,
                              const kernel::Proposition& goal,
                              std::string& reason) {
    const auto* available_forall = std::get_if<kernel::Forall>(&available.node);
    const auto* goal_forall = std::get_if<kernel::Forall>(&goal.node);

    if (available_forall != nullptr && goal_forall != nullptr) {
        if (!(available_forall->binder == goal_forall->binder)) {
            reason = "it quantifies over '" + kernel::describe(available_forall->binder) +
                     "' where the goal quantifies over '" +
                     kernel::describe(goal_forall->binder) + "'";
            return false;
        }
        return conclusion_is_applicable(*available_forall->body, *goal_forall->body, reason);
    }

    if (available_forall != nullptr || goal_forall != nullptr) {
        reason = "it quantifies over a different number of variables than the goal";
        return false;
    }

    const auto& available_equality = std::get<kernel::Eq>(available.node);
    const auto& goal_equality = std::get<kernel::Eq>(goal.node);
    if (!(available_equality.type == goal_equality.type)) {
        reason = "it is an equality at '" + kernel::describe(available_equality.type) +
                 "' where the goal is an equality at '" + kernel::describe(goal_equality.type) +
                 "'";
        return false;
    }
    return true;
}

void report(diagnostics::Engine& engine,
            diagnostics::Category category,
            const source::SourceLocation& location,
            std::string message,
            std::string note = {}) {
    diagnostics::Diagnostic diagnostic;
    diagnostic.severity = diagnostics::Severity::Error;
    diagnostic.category = category;
    diagnostic.message = std::move(message);
    diagnostic.location = location;
    if (!note.empty()) {
        diagnostic.notes.push_back(diagnostics::Note{std::move(note), location});
    }
    engine.report(std::move(diagnostic));
}

// The proposition a `proves` clause claims.
//
// A law states what holds for every inhabitant of its parameters. Naming it at
// particular arguments claims one instance of it, which is that statement with
// the quantifiers instantiated: universal elimination, performed here on the
// proposition by the kernel's own substitution. What remains open are the
// proof's own parameters, and those are quantified back over the result.
std::optional<kernel::Proposition> claimed_proposition(const vir::Proof& proof,
                                                       const Obligation& obligation,
                                                       const DefinitionMap& definitions,
                                                       diagnostics::Engine& engine) {
    const auto& claim = std::get<vir::Call>(proof.proposition.node);
    const TermLowering lowering(definitions, proof.parameters.size());

    kernel::Proposition instance = obligation.goal;
    for (const vir::Expr& argument : claim.arguments) {
        const auto* quantified = std::get_if<kernel::Forall>(&instance.node);
        if (quantified == nullptr) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   argument.provenance.range.begin,
                   "law '" + obligation.law_name + "' is named at more arguments than it "
                   "quantifies over");
            return std::nullopt;
        }

        std::expected<kernel::Term, Failure> term = lowering.lower(argument);
        if (!term) {
            report(engine, diagnostics::Category::UnsupportedSemantics, term.error().location,
                   "proof '" + proof.name + "' claims law '" + obligation.law_name +
                       "' at a term the formal core cannot state: " + term.error().reason);
            return std::nullopt;
        }
        instance = kernel::instantiate(*quantified->body, *term);
    }

    for (auto parameter = proof.parameters.rbegin(); parameter != proof.parameters.rend();
         ++parameter) {
        const std::optional<kernel::Type> binder = lower_type(parameter->type);
        if (!binder.has_value()) {
            report(engine, diagnostics::Category::UnsupportedSemantics, proof.range.begin,
                   "proof '" + proof.name + "' quantifies over '" +
                       vir::describe(parameter->type) +
                       "', which the formal core does not represent");
            return std::nullopt;
        }
        instance = kernel::Proposition::for_all(*binder, std::move(instance));
    }

    return instance;
}

// The evidence a referenced proof supplies once it is instantiated.
//
// Each argument is one universal elimination. The proof term records the
// proposition it eliminates from so the kernel can check that step itself; the
// proposition tracked alongside is this layer's own account of where the
// elimination has got to, and the kernel derives it again independently.
struct Instantiation {
    kernel::Proposition proposition;
    kernel::ProofTerm term;
};

std::optional<Instantiation> instantiate_evidence(const vir::Proof& proof,
                                                  const WrittenProof& evidence,
                                                  const std::vector<vir::Expr>& arguments,
                                                  const DefinitionMap& definitions,
                                                  diagnostics::Engine& engine) {
    Instantiation state{evidence.goal, evidence.term};
    const TermLowering lowering(definitions, proof.parameters.size());

    for (const vir::Expr& argument : arguments) {
        const source::SourceLocation& location = argument.provenance.range.begin;

        const auto* quantified = std::get_if<kernel::Forall>(&state.proposition.node);
        if (quantified == nullptr) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "proof '" + evidence.name + "' is instantiated at more arguments than it "
                   "quantifies over",
                   "at this argument it establishes " + kernel::describe(state.proposition) +
                       ", which quantifies over nothing");
            return std::nullopt;
        }

        const std::optional<kernel::Type> type = lower_type(argument.type);
        if (!type.has_value() || !(*type == quantified->binder)) {
            report(engine, diagnostics::Category::ProofFailure, location,
                   "proof '" + evidence.name + "' quantifies over '" +
                       kernel::describe(quantified->binder) +
                       "' and cannot be instantiated at a term of type '" +
                       vir::describe(argument.type) + "'");
            return std::nullopt;
        }

        std::expected<kernel::Term, Failure> term = lowering.lower(argument);
        if (!term) {
            report(engine, diagnostics::Category::UnsupportedSemantics, term.error().location,
                   "proof '" + proof.name + "' instantiates '" + evidence.name +
                       "' at a term the formal core cannot state: " + term.error().reason);
            return std::nullopt;
        }

        kernel::Proposition eliminated = kernel::instantiate(*quantified->body, *term);
        state.term = kernel::ProofTerm::forall_elimination(std::move(state.proposition),
                                                           std::move(state.term), *term);
        state.proposition = std::move(eliminated);
    }

    return state;
}

// The same evidence, with this proof's parameters quantified back over it.
//
// An argument may mention the proof's parameters, in which case the statement
// the elimination leaves is open in them and the claim is its closure. An
// argument may equally be a closed term, in which case the statement stands on
// its own. Both are readings of one written statement; which one the claim
// asks for is settled by comparing propositions, never by searching.
Instantiation close_over(const std::vector<vir::Parameter>& parameters, Instantiation state) {
    for (auto parameter = parameters.rbegin(); parameter != parameters.rend(); ++parameter) {
        const kernel::Type binder = *lower_type(parameter->type);
        state.term = kernel::ProofTerm::forall_introduction(binder, std::move(state.term));
        state.proposition = kernel::Proposition::for_all(binder, std::move(state.proposition));
    }
    return state;
}

// Lowers each written proof into a kernel proof term.
//
// A step is lowered only once the proof it names has been lowered, so the
// dependency graph is traversed in order and anything left over is circular.
// No step is admitted on the strength of what it is called: `exact` must offer
// the claimed proposition itself, and `apply` must offer a conclusion that
// proposition can accept. Both then go to the kernel like any other evidence.
void lower_proofs(const vir::Module& module,
                  const elaboration::Result& elaborated,
                  const DefinitionMap& definitions,
                  Program& program,
                  diagnostics::Engine& engine) {
    program.refused_proofs = elaborated.laws_with_refused_proofs;

    std::map<std::uint32_t, const Obligation*> goals;
    for (const Obligation& obligation : program.obligations) {
        goals.emplace(obligation.law.value, &obligation);
    }

    std::set<std::uint32_t> declared;
    std::vector<const vir::Proof*> pending;
    for (const vir::Proof& proof : module.proofs) {
        declared.insert(proof.id.value);
        if (goals.contains(proof.law.value)) {
            pending.push_back(&proof);
        }
    }

    std::map<std::uint32_t, std::size_t> lowered;  // proof id -> index in program.proofs

    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const vir::Proof& proof = **candidate;
            const Obligation& obligation = *goals.at(proof.law.value);

            const auto* exact = std::get_if<vir::ExactStep>(&proof.step.node);
            const auto* apply = std::get_if<vir::ApplyStep>(&proof.step.node);

            const auto refuse = [&] {
                program.refused_proofs.push_back(proof.law);
                candidate = pending.erase(candidate);
                progress = true;
            };

            const std::optional<kernel::Proposition> claimed =
                claimed_proposition(proof, obligation, definitions, engine);
            if (!claimed.has_value()) {
                refuse();
                continue;
            }

            std::optional<kernel::ProofTerm> term;
            WrittenProofKind kind = WrittenProofKind::Reflexivity;

            if (exact == nullptr && apply == nullptr) {
                term = definitional_evidence(*claimed);
            } else {
                kind = exact != nullptr ? WrittenProofKind::Exact : WrittenProofKind::Apply;
                const std::string& name =
                    exact != nullptr ? exact->target_name : apply->target_name;
                const std::uint32_t used =
                    exact != nullptr ? exact->target.value : apply->target.value;

                if (!declared.contains(used)) {
                    report(engine, diagnostics::Category::ProofFailure, proof.step.location,
                           "proof '" + name + "' was not admitted, so proof '" + proof.name +
                               "' has no evidence",
                           "the reason it was not admitted is reported above");
                    refuse();
                    continue;
                }

                const auto source = lowered.find(used);
                if (source == lowered.end()) {
                    ++candidate;  // its evidence is not built yet
                    continue;
                }

                const std::vector<vir::Expr>& arguments =
                    exact != nullptr ? exact->arguments : apply->arguments;
                std::optional<Instantiation> instantiated = instantiate_evidence(
                    proof, program.proofs[source->second], arguments, definitions, engine);
                if (!instantiated.has_value()) {
                    refuse();
                    continue;
                }

                std::vector<Instantiation> readings;
                readings.push_back(*instantiated);
                if (!arguments.empty() && !proof.parameters.empty()) {
                    readings.push_back(close_over(proof.parameters, std::move(*instantiated)));
                }

                std::string reason;
                const Instantiation* accepted = nullptr;
                for (const Instantiation& reading : readings) {
                    std::string why;
                    const bool matches =
                        exact != nullptr
                            ? reading.proposition == *claimed
                            : conclusion_is_applicable(reading.proposition, *claimed, why);
                    if (matches) {
                        accepted = &reading;
                        break;
                    }
                    if (reason.empty()) {
                        reason = why;  // the reading the note below shows
                    }
                }

                if (accepted == nullptr) {
                    report(engine, diagnostics::Category::ProofFailure, proof.step.location,
                           exact != nullptr
                               ? "proof '" + name + "' does not prove what proof '" + proof.name +
                                     "' claims"
                               : "the conclusion of proof '" + name +
                                     "' cannot be applied to what proof '" + proof.name +
                                     "' claims: " + reason,
                           "it establishes " + kernel::describe(readings.front().proposition) +
                               ", and the claim is " + kernel::describe(*claimed));
                    refuse();
                    continue;
                }

                term = accepted->term;
            }

            WrittenProof written;
            written.id = proof.id;
            written.name = proof.name;
            written.law = proof.law;
            written.kind = kind;
            written.goal = *claimed;
            written.closes_law = *claimed == obligation.goal;
            written.term = std::move(*term);
            written.range = proof.range;

            // A proof that discharges its law is submitted as that law's
            // evidence and is checked there. A proof of one instance has no
            // obligation of its own, so it is checked here: an author's claim
            // is never left standing without the kernel having seen it.
            if (!written.closes_law) {
                const kernel::CheckResult checked = kernel::check(
                    program.context, written.goal, written.term, kernel::CoreLimits{});
                if (!checked.has_value()) {
                    diagnostics::Diagnostic diagnostic;
                    diagnostic.severity = diagnostics::Severity::Error;
                    diagnostic.category = diagnostics::Category::KernelRejection;
                    diagnostic.message =
                        "proof '" + proof.name + "' does not establish what it claims";
                    diagnostic.location = proof.range.begin;
                    diagnostic.notes.push_back(diagnostics::Note{
                        "claim: " + kernel::describe(written.goal), proof.range.begin});
                    diagnostic.notes.push_back(diagnostics::Note{
                        "the kernel did not accept the evidence: " + checked.error().detail,
                        proof.range.begin});
                    engine.report(std::move(diagnostic));
                    refuse();
                    continue;
                }
            }

            lowered.emplace(proof.id.value, program.proofs.size());
            program.proofs.push_back(std::move(written));

            candidate = pending.erase(candidate);
            progress = true;
        }
    }

    for (const vir::Proof* proof : pending) {
        report(engine, diagnostics::Category::ProofFailure, proof->step.location,
               "proof '" + proof->name + "' depends on itself through the proofs it uses",
               "this formal core has no induction rule, so written proofs must be acyclic");
        program.refused_proofs.push_back(proof->law);
    }

    std::map<std::uint32_t, std::string> closed_by;
    for (const WrittenProof& written : program.proofs) {
        if (!written.closes_law) {
            continue;
        }
        const auto [owner, first] = closed_by.emplace(written.law.value, written.name);
        if (!first) {
            report(engine, diagnostics::Category::CpplSyntax, written.range.begin,
                   "law '" + goals.at(written.law.value)->law_name + "' already has a proof",
                   "'" + owner->second + "' establishes it; a law is discharged by exactly one "
                   "written proof, though others may prove instances of it");
        }
    }

    // Writing a proof for a law is taking responsibility for it. If none of the
    // proofs that name a law establishes the law itself, the law stays open:
    // the compiler does not quietly close with its own strategy a goal the
    // author has already said how to establish.
    std::set<std::uint32_t> reported;
    for (const vir::Proof& proof : module.proofs) {
        if (!goals.contains(proof.law.value) || closed_by.contains(proof.law.value)) {
            continue;
        }
        if (program.proof_refused(proof.law) || !reported.insert(proof.law.value).second) {
            continue;
        }
        report(engine, diagnostics::Category::ProofFailure, proof.range.begin,
               "law '" + goals.at(proof.law.value)->law_name +
                   "' is named by a written proof, but nothing establishes the law itself",
               "a proof of one instance does not discharge the law; write a proof that claims "
               "it at its own parameters");
        program.refused_proofs.push_back(proof.law);
    }
}

}  // namespace

Program generate(const vir::Module& module,
                 const elaboration::Result& elaborated,
                 diagnostics::Engine& engine) {
    Program program;
    DefinitionMap definitions;
    std::map<std::string, Failure> deferred;

    // Definitions are admitted in dependency order. The kernel only accepts a
    // definition whose callees are already present, which is what keeps the
    // definition graph acyclic and normalization terminating.
    std::vector<const vir::Function*> pending;
    for (const vir::Function& function : module.functions) {
        if (function.purity == vir::Purity::Pure && function.returned_value.has_value()) {
            pending.push_back(&function);
        }
    }

    std::uint32_t next_definition = 0;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const vir::Function& function = **candidate;

            std::set<std::string> callees;
            collect_callees(*function.returned_value, callees);
            const bool ready = std::ranges::all_of(callees, [&](const std::string& callee) {
                return definitions.contains(callee);
            });
            if (!ready) {
                ++candidate;
                continue;
            }

            const TermLowering lowering(definitions, function.parameters.size());
            std::expected<kernel::Term, Failure> body = lowering.lower(*function.returned_value);
            if (!body) {
                deferred.emplace(function.symbol.usr, body.error());
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }

            kernel::Definition definition;
            definition.id = kernel::DefId{next_definition};
            definition.name = function.qualified_name;
            definition.result = *lower_type(function.result);
            for (const vir::Parameter& parameter : function.parameters) {
                const std::optional<kernel::Type> type = lower_type(parameter.type);
                if (!type.has_value()) {
                    definition.parameters.clear();
                    break;
                }
                definition.parameters.push_back(*type);
            }

            if (definition.parameters.size() != function.parameters.size()) {
                deferred.emplace(function.symbol.usr,
                                 Failure{"a parameter type has no core representation",
                                         function.range.begin, {}});
                candidate = pending.erase(candidate);
                progress = true;
                continue;
            }

            definition.body = std::move(*body);
            if (auto admitted = program.context.define(std::move(definition)); !admitted) {
                deferred.emplace(function.symbol.usr,
                                 Failure{kernel::describe(admitted.error().kind) + ": " +
                                             admitted.error().detail,
                                         function.range.begin, {}});
            } else {
                definitions.emplace(function.symbol.usr, kernel::DefId{next_definition});
                ++next_definition;
            }

            candidate = pending.erase(candidate);
            progress = true;
        }
    }

    for (const vir::Function* function : pending) {
        deferred.emplace(function->symbol.usr,
                         Failure{"its definition is recursive, or depends on a definition that "
                                 "could not be admitted",
                                 function->range.begin, {}});
    }

    for (const vir::Law& law : module.laws) {
        const auto* equality = std::get_if<vir::Binary>(&law.proposition.node);
        if (equality == nullptr || equality->op != vir::BinaryOp::Equal ||
            equality->operands.size() != 2) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   law.proposition_range.begin,
                   "law '" + law.name + "' does not state an equality",
                   "this implementation proves propositions of the form 'a == b' over "
                   "built-in integer values");
            continue;
        }

        // A C++ equality between built-in integer values denotes propositional
        // equality of those values. The correspondence holds for this operand
        // type only, and is established here rather than assumed anywhere else
        // (SPEC.md 7.3).
        const std::optional<kernel::Type> left_type = lower_type(equality->operands[0].type);
        const std::optional<kernel::Type> right_type = lower_type(equality->operands[1].type);
        if (!left_type.has_value() || !right_type.has_value() || !(*left_type == *right_type)) {
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   law.proposition_range.begin,
                   "law '" + law.name + "' compares values this implementation does not model as "
                   "equal-typed integers");
            continue;
        }

        const TermLowering lowering(definitions, law.parameters.size());
        std::expected<kernel::Term, Failure> lhs = lowering.lower(equality->operands[0]);
        std::expected<kernel::Term, Failure> rhs =
            lhs ? lowering.lower(equality->operands[1]) : std::unexpected(lhs.error());
        if (!lhs || !rhs) {
            const Failure& failure = lhs ? rhs.error() : lhs.error();
            std::string note;
            if (!failure.missing_symbol.empty()) {
                if (const elaboration::FunctionRejection* rejection =
                        elaborated.rejection(vir::SymbolId{failure.missing_symbol})) {
                    note = "'" + rejection->name + "' was not admitted because " +
                           rejection->reason;
                } else if (const auto deferral = deferred.find(failure.missing_symbol);
                           deferral != deferred.end()) {
                    note = deferral->second.reason;
                } else {
                    note = "it is not marked pure, so it is not a definition the formal core "
                           "may unfold";
                }
            }
            report(engine, diagnostics::Category::UnsupportedSemantics,
                   failure.location.is_valid() ? failure.location : law.range.begin,
                   "law '" + law.name + "' cannot be stated to the formal core: " + failure.reason,
                   note);
            continue;
        }

        kernel::Proposition goal = kernel::Proposition::equality(*left_type, *lhs, *rhs);
        bool quantified = true;
        for (auto parameter = law.parameters.rbegin(); parameter != law.parameters.rend();
             ++parameter) {
            const std::optional<kernel::Type> binder = lower_type(parameter->type);
            if (!binder.has_value()) {
                report(engine, diagnostics::Category::UnsupportedSemantics, law.range.begin,
                       "law '" + law.name + "' quantifies over '" + vir::describe(parameter->type) +
                           "', which the formal core does not represent");
                quantified = false;
                break;
            }
            goal = kernel::Proposition::for_all(*binder, std::move(goal));
        }
        if (!quantified) {
            continue;
        }

        Obligation obligation;
        obligation.law = law.id;
        obligation.law_name = law.name;
        obligation.origin = Origin::LawProposition;
        obligation.range = law.range;
        obligation.id = identify(program.context, law.name, goal);
        obligation.goal = std::move(goal);
        program.obligations.push_back(std::move(obligation));
    }

    lower_proofs(module, elaborated, definitions, program, engine);
    return program;
}

}  // namespace cppl::obligations
