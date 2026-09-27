#pragma once

#include <string_view>

namespace cppl::driver {

// The declared version of the verification semantics: what a proven contract
// means, how C++ is modeled, what each library model states and what an
// interface records. Raise it with every change to any of these. A
// verification interface is bound to it and, since a declared version can be
// left unchanged by mistake, also to the digest of the sources that implement
// it (kVerifierSemanticsDigest, SPEC.md TUBOUND-005).
inline constexpr std::string_view kVerificationSemanticsVersion = "cppl-verification-semantics-0.2.0";

} // namespace cppl::driver
