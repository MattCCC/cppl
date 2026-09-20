#pragma once

#include <string_view>

namespace cppl::kernel {

// The kernel and the formal core it checks are versioned independently of the
// compiler: a change here can invalidate previously accepted evidence even when
// the language and the compiler are unchanged (ARCHITECTURE.md 78).
inline constexpr std::string_view kKernelVersion = "cppl-kernel-0.5.0";
inline constexpr std::string_view kFormalCoreVersion = "cppl-core-0.5.0";

} // namespace cppl::kernel
