#pragma once

#define CPPL_VERIFY verified
namespace imported {
using word = unsigned;
CPPL_VERIFY word from_header(word x)
    ensures (result == x)
{
    return x;
}
verified word next(word y)
    ensures (result == y)
{
    return y;
}
} // namespace imported
#undef CPPL_VERIFY
