#include "cppl/obligations/status.hpp"

namespace cppl::obligations {

std::string describe(Status status) {
    switch (status) {
        case Status::Proven:
            return "PROVEN";
        case Status::Trusted:
            return "TRUSTED";
        case Status::RuntimeChecked:
            return "RUNTIME-CHECKED";
        case Status::Unsafe:
            return "UNSAFE";
        case Status::Unverified:
            return "UNVERIFIED";
        case Status::Unresolved:
            return "UNRESOLVED";
    }
    return "UNRESOLVED";
}

std::string describe(Origin origin) {
    switch (origin) {
        case Origin::LawProposition:
            return "law proposition";
    }
    return "obligation";
}

Verdict Verdict::proven(const kernel::Acceptance& acceptance, const Obligation& obligation) {
    // The acceptance must be for this obligation's goal. Holding an acceptance
    // for some other proposition establishes nothing about this one.
    if (!(acceptance.proposition() == obligation.goal)) {
        return Verdict(Status::Unresolved,
                       "the kernel accepted a different proposition than this obligation states");
    }
    return Verdict(Status::Proven, {});
}

Verdict Verdict::unresolved(std::string reason) {
    return Verdict(Status::Unresolved, std::move(reason));
}

}  // namespace cppl::obligations
