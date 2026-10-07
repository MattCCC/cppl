#pragma once

#include "cppl/artifact/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/representation.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace cppl::obligations {

// The version of the verification semantics this compiler implements: what
// every obligation it generates means, what its models of C++ and of the
// standard library state, and what a contract it records states and rests on.
// An interface is used only by a compiler of the same release implementing the
// same version, whatever build of it that compiler is (SPEC.md TUBOUND-005,
// TUBOUND-013), and the version is maintained by hand. It MUST change with any
// change that could make a recorded result mean something else to a consumer;
// keeping it honest is an obligation of the reuse TCB (TRUST.md TCB-XTU-008,
// TCB-VERSION-003). Version 2 records runtime validation sites. Version 3
// records as sites only explicit validation expressions: a refinement crossing
// proven from path facts is proven statically and is no site (SPEC.md
// RUNTIMECHECK-010, RUNTIMECHECK-011). Version 4 quantifies a binder or a
// parameter of a refinement type over that refinement's values only, so a
// contract stating such a quantifier states less than it did (SPEC.md
// FORALL-001). Version 5 models a call to a function whose unsafe code may
// write what it is handed as writing every reference, pointer and view it
// hands over, so a contract proven through one under version 4 may rest on a
// `const` argument kept that was not (TRUST.md TCB-UNSAFE-004). Version 6
// passes, returns and copies whole struct values, supposing of a value assembled
// from its members only that its members hold those values, so a contract it
// records may rest on that correspondence, which no earlier version stated
// (TRUST.md TCB-AGGREGATE-001); and it refuses a call to a function whose unsafe
// code may write what it is handed anywhere but in a position of its own, where
// version 5 passed over that call's writes, so a contract version 5 recorded
// through one may rest on an argument kept that was not (TCB-UNSAFE-004).
// Version 7 gives a member function's implicit object one place per element of
// a `std::array` member, so the parameter positions a contract it records
// numbers differ from version 6's for such a class, and it follows an object a
// reference designates member by member, handing back the value its members
// assemble, which version 6 stated of no contract (TRUST.md TCB-AGGREGATE-003).
// Version 8 owes a signed subscript index that it is not negative as well as
// below the extent, so a contract version 7 recorded may rest on an element
// read or written at a negative index, which names no element (TRUST.md 36.4).
inline constexpr std::string_view kVerificationSemanticsVersion = "cppl-verification-8";

// A contract another translation unit proved, offered to this one by the
// verification interface that recorded it (SPEC.md TUBOUND-003).
//
// The driver has read the interface strictly and checked that it was produced
// under this unit's configuration, from sources that have not changed since
// (TUBOUND-005). What remains is decided where the contract would be used: whether
// this unit's own declaration of the function states the recorded statement
// (TUBOUND-004), and whether using it would recurse through another unit (TUBOUND-008).
// Nothing in an entry is ever read as a proposition.
struct ImportedEntry {
    std::string origin; // the interface file, for diagnostics and the report
    artifact::Entry entry;
    source::Digest identity; // artifact::identify(entry)
};

// An entry an imported interface records that is not usable here, and why: the
// interface is stale or was produced under another configuration, another
// interface records a different contract for the same function, or what the
// entry rests on is not recorded as it was when it was proven (SPEC.md TUBOUND-005,
// TUBOUND-009). A call relying on it is refused with that reason.
struct RefusedEntry {
    std::string symbol;
    std::string origin;
    std::string reason;
};

// Every contract the interfaces a unit imports record, at most one per symbol.
struct Imports {
    std::vector<ImportedEntry> entries;
    std::vector<RefusedEntry> refused;

    [[nodiscard]] const ImportedEntry* find(std::string_view symbol) const;
    [[nodiscard]] const RefusedEntry* refusal(std::string_view symbol) const;
};

// A standard-library model as a verification interface records it: the
// identity this compiler gives the model and the name a report shows (RFC 0020
// §10, TRUST.md TCB-LIB-010). The interface format itself names no model.
[[nodiscard]] artifact::Model library_model(source::RepresentationKind model);

// Whether `model` is one this compiler has, with the identity and the name this
// compiler gives it. A compiler of the same verification semantics gives every
// model both, so a record naming any other was not written by one (SPEC.md
// TUBOUND-005), and a model a report names is never a name an interface chose.
[[nodiscard]] bool known_library_model(const artifact::Model& model);

// The contracts this unit proved, as its own verification interface records
// them (SPEC.md TUBOUND-002): each with its statement identity, whether it is total,
// and what it rests on, its own trusted laws, standard-library models and unsafe
// blocks and those of every contract of another unit it was proven through
// (TUBOUND-006, STDMODEL-018). A contract imported rather than proven here is not
// among them, and neither is one whose statement identity could not be computed.
[[nodiscard]] std::vector<artifact::Entry> exported_contracts(const Program& program, const TrustClosure& closure);

} // namespace cppl::obligations
