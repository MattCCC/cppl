#pragma once

#include "cppl/artifact/interface.hpp"
#include "cppl/obligations/obligation.hpp"
#include "cppl/obligations/trust.hpp"
#include "cppl/source/digest.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace cppl::obligations {

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

// The contracts this unit proved, as its own verification interface records
// them (SPEC.md TUBOUND-002): each with its statement identity, whether it is total,
// and what it rests on, its own trusted laws and unsafe blocks and those of
// every contract of another unit it was proven through (TUBOUND-006). A contract
// imported rather than proven here is not among them, and neither is one whose
// statement identity could not be computed.
[[nodiscard]] std::vector<artifact::Entry> exported_contracts(const Program& program, const TrustClosure& closure);

} // namespace cppl::obligations
