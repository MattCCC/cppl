#pragma once

#include "cppl/source/location.hpp"
#include "cppl/vir/expr.hpp"
#include "cppl/vir/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Representation-independent proof decomposition (SPEC.md 20.5, RFC 0013).
//
// A *representation provider* knows the sound logical state model of one C++
// representation family. It contributes nothing else: arm matching, binder
// handling, exhaustiveness validation, proof-state splitting, evidence
// construction, dependency checking, diagnostics, erasure and kernel lowering
// are representation-independent and live in the generic case engine.
//
// A provider models ordinary C++ states for verification purposes. It does not
// introduce a C++L runtime type, runtime pattern matching, runtime
// destructuring, or runtime control flow. Clang stays authoritative for the
// ordinary C++ semantics a provider reads; a provider only states what those
// resolved semantics mean as propositions.
namespace cppl::decomposition {

// How a proof-visible binding relates to the subject it came from.
//
// A binding never denotes a new object. It names a value the C++ object model
// already justifies, so nothing here implies a copy, a move, a temporary, or a
// member access in the erased program.
enum class BindingKind : std::uint8_t {
    // The bound name denotes the subject itself, at the subject's own value.
    Alias,
    // The bound name denotes a component projected out of the subject.
    Projection,
};

struct ProofBinding {
    // The provider's name for the binding. The written binder renames it; this
    // is what diagnostics and completion offer.
    std::string name;
    vir::Type type;
    BindingKind kind = BindingKind::Alias;

    // How the bound value is obtained from the subject, logically. For an
    // alias this is the subject expression itself.
    vir::Expr value;
};

// The source-level name of one case.
struct CaseLabel {
    // What an author writes for this case, as the provider spells it.
    std::string text;
    // Whether the label is written qualified, as a scoped enumerator is.
    bool qualified = false;

    friend bool operator==(const CaseLabel&, const CaseLabel&) = default;
};

// One alternative of a sum decomposition.
//
// `discriminator` is a modeled Boolean condition over the subject that holds in
// exactly this case. It is an ordinary VIR expression, so the generic engine
// lowers it with the same machinery as any other expression and the kernel
// checks the resulting evidence without learning anything about the
// representation it came from.
struct CaseDescriptor {
    CaseLabel label;
    vir::Expr discriminator;
    std::vector<ProofBinding> bindings;
};

// Why the cases a provider lists cover the representation's whole state space.
enum class ExhaustivenessModel : std::uint8_t {
    // The named discriminators do not cover the state space on their own. The
    // residual case completes the partition and must be written. This is the
    // model for a representation whose C++ value set is larger than its named
    // states, such as a scoped enum over its fixed underlying type.
    ResidualRequired,
    // The named discriminators are jointly exhaustive by construction, so the
    // last one is the negation of the others and no residual case exists.
    Complete,
};

// A finite partition of a subject's proof-visible states.
//
// The generic engine splits the enclosing goal on `cases` in order and gives
// the residual case the conjunction of the negated discriminators. Adding a
// state to the representation therefore adds a case here, and a proof that did
// not write an arm for it stops being exhaustive.
struct SumDecomposition {
    std::vector<CaseDescriptor> cases;
    ExhaustivenessModel exhaustiveness = ExhaustivenessModel::ResidualRequired;

    // The residual case, when `exhaustiveness` requires one. Its discriminator
    // is derived by the engine, never supplied, so a provider cannot widen the
    // residual state beyond "none of the named discriminators holds".
    CaseLabel residual;
    std::vector<ProofBinding> residual_bindings;
};

// The components of a product representation.
//
// Binding a component does not destructure anything at runtime: the components
// are values the C++ object model already provides, exposed for reasoning.
struct ProductDecomposition {
    std::vector<ProofBinding> fields;
};

// No provider models this resolved representation.
//
// `representation` names the resolved C++ type, so a diagnostic can say what
// was not modeled instead of reporting an unsupported construct.
struct Unsupported {
    std::string representation;
    std::string reason;
};

using Decomposition = std::variant<SumDecomposition, ProductDecomposition, Unsupported>;

// What a provider is asked about: the resolved subject of a `cases` statement.
struct Subject {
    // The subject expression, with the type Clang resolved for it.
    vir::Expr expression;
    source::SourceLocation location;
};

// One representation family's logical state model.
class Provider {
  public:
    Provider() = default;
    Provider(const Provider&) = delete;
    Provider& operator=(const Provider&) = delete;
    Provider(Provider&&) = delete;
    Provider& operator=(Provider&&) = delete;
    virtual ~Provider() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;

    // Whether this provider models the subject's resolved representation.
    // Selection is by resolved semantic identity, never by type spelling.
    [[nodiscard]] virtual bool recognizes(const vir::Type& type) const = 0;

    [[nodiscard]] virtual Decomposition decompose(const Subject& subject) const = 0;

    // Which case of `decomposition` the Clang-resolved label expression
    // denotes, when it denotes one. The engine never interprets a label itself:
    // what a written label means is part of the representation's semantics.
    [[nodiscard]] virtual std::optional<std::size_t> resolve_label(const SumDecomposition& decomposition,
                                                                   const vir::Expr& label) const = 0;
};

// The decomposition of `subject`, from the first provider that recognizes its
// resolved representation. Returns `Unsupported` naming the resolved type when
// no provider does.
[[nodiscard]] Decomposition decompose(const Subject& subject);

// The provider that recognizes `type`, or null when none does.
[[nodiscard]] const Provider* provider_for(const vir::Type& type);

} // namespace cppl::decomposition
