// The files a dependency rule Clang wrote names (compiler/driver/src/
// dependencies.cpp), which a verification interface is bound to beside those
// its line markers name (SPEC.md TUBOUND-005, TRUST.md TCB-XTU-008).
//
// Every escape Clang writes is undone, and anything but the one rule asked for
// is refused rather than read in part: a file left out would be one an edit to
// leaves the interface current.

#include "cppl/driver/dependencies.hpp"
#include "cppl/testing/test.hpp"

#include <expected>
#include <string>
#include <vector>

using cppl::driver::make_prerequisites;

namespace {

void expect(const std::expected<std::vector<std::string>, std::string>& actual,
            const std::vector<std::string>& expected, int line) {
    if (!actual) {
        ::cppl::testing::fail(__FILE__, line, "refused: " + actual.error());
        return;
    }
    if (*actual != expected) {
        std::string got;
        for (const std::string& name : *actual) {
            got += "[" + name + "]";
        }
        ::cppl::testing::fail(__FILE__, line, "got " + got);
    }
}

} // namespace

// SPEC: TUBOUND-005
CPPL_TEST(each_prerequisite_is_read_in_order_across_joined_lines) {
    expect(make_prerequisites("t: a.cpp b.hpp \\\n  /abs/c.bin\n", "t"), {"a.cpp", "b.hpp", "/abs/c.bin"}, __LINE__);
    expect(make_prerequisites("t: a.cpp \\\r\n  b.hpp\r\n", "t"), {"a.cpp", "b.hpp"}, __LINE__);
    expect(make_prerequisites("t:\n", "t"), {}, __LINE__);
    expect(make_prerequisites("t: a.cpp", "t"), {"a.cpp"}, __LINE__);
}

// SPEC: TUBOUND-005 -- a name is read as Clang wrote it, escapes undone.
CPPL_TEST(each_escape_clang_writes_is_undone) {
    expect(make_prerequisites(R"(t: with\ space.h hash\#.h dollar$$.h <val.h)", "t"),
           {"with space.h", "hash#.h", "dollar$.h", "<val.h"}, __LINE__);
    // A backslash a name ends in before an escaped space is doubled; one not
    // before a space or `#` is the name's own, as in a Windows path.
    expect(make_prerequisites(R"(t: a\\\ b.h C:\dir\c.h d\\ e.h)", "t"), {R"(a\ b.h)", R"(C:\dir\c.h)", R"(d\)", "e.h"},
           __LINE__);
}

// SPEC: TUBOUND-005 -- anything but the one rule asked for is refused.
CPPL_TEST(anything_but_the_one_rule_asked_for_is_refused) {
    CPPL_CHECK(!make_prerequisites("", "t").has_value());
    CPPL_CHECK(!make_prerequisites("u: a.cpp\n", "t").has_value());
    CPPL_CHECK(!make_prerequisites("tt: a.cpp\n", "t").has_value());
    CPPL_CHECK(!make_prerequisites("t a.cpp\n", "t").has_value());
    // A second rule, as -MP would add, is not read past.
    CPPL_CHECK(!make_prerequisites("t: a.cpp b.hpp\n\nb.hpp:\n", "t").has_value());
}
