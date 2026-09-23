#pragma once

#include "cppl/frontend/token.hpp"

#include <cstddef>
#include <vector>

namespace cppl::frontend::detail {

// Whether a declaration may begin at `tokens[index]`, as the recognizer decides
// it for every C++L declaration: at the start of the unit, or after what can
// only end a declaration, a statement or a label, with the declaration's
// specifiers and template header read as part of it.
bool declaration_may_begin(const std::vector<Token>& tokens, std::size_t index);

} // namespace cppl::frontend::detail
