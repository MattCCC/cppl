#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/source/digest.hpp"
#include "cppl/source/location.hpp"
#include "cppl/vir/ids.hpp"

namespace cppl::obligations {

// A content-derived identity: the same obligation, produced by any run of the
// same compiler on the same input, has the same id (ARCHITECTURE.md 32).
struct ObligationId {
    source::Digest digest;

    [[nodiscard]] std::string text() const { return digest.to_short_hex(16); }

    friend bool operator==(const ObligationId&, const ObligationId&) = default;
};

enum class Origin : std::uint8_t {
    LawProposition,
};

std::string describe(Origin origin);

struct Obligation {
    ObligationId id;
    vir::LawId law;
    std::string law_name;
    Origin origin = Origin::LawProposition;
    kernel::Proposition goal;
    source::SourceRange range;
};

// Everything the kernel needs in order to decide the obligations of one
// translation unit: the definitions it may unfold, and the goals themselves.
struct Program {
    kernel::Context context;
    std::vector<Obligation> obligations;
};

}  // namespace cppl::obligations
