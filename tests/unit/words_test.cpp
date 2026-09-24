// The words the specification gives a C++L meaning, as the frontend lists them
// (compiler/frontend/include/cppl/frontend/words.hpp), checked against the
// lists SPEC.md itself writes, so the two cannot drift apart.

#include "cppl/frontend/words.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string spec() {
    std::ifstream stream(CPPL_TEST_SPEC, std::ios::binary);
    if (!stream) {
        ::cppl::testing::fail(__FILE__, __LINE__, "could not read SPEC.md");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// The words of the first ```text block after `heading`, one per line.
std::set<std::string> listed_after(const std::string& text, const std::string& heading) {
    std::set<std::string> words;
    const std::size_t at = text.find(heading);
    if (at == std::string::npos) {
        ::cppl::testing::fail(__FILE__, __LINE__, "no '" + heading + "' in SPEC.md");
        return words;
    }
    const std::string open = "```text\n";
    const std::size_t begin = text.find(open, at);
    const std::size_t end = text.find("```", begin + open.size());
    std::istringstream block(text.substr(begin + open.size(), end - begin - open.size()));
    for (std::string line; std::getline(block, line);) {
        if (!line.empty()) {
            words.insert(line);
        }
    }
    return words;
}

} // namespace

CPPL_TEST(every_word_the_specification_lists_is_a_cppl_word) {
    const std::string text = spec();
    std::set<std::string> expected;
    for (const char* heading : {"# 3. Contextual C++L words", "[WORD-001]", "[WORD-002]"}) {
        const std::set<std::string> words = listed_after(text, heading);
        CPPL_CHECK(!words.empty());
        expected.insert(words.begin(), words.end());
    }
    // WORD-010 names its two words in its sentence.
    CPPL_CHECK(text.find("[WORD-010] `omit` and `by` have special meaning") != std::string::npos);
    expected.insert({"omit", "by"});

    std::set<std::string> listed;
    for (const std::string_view word : cppl::frontend::cppl_words()) {
        CPPL_CHECK(listed.insert(std::string(word)).second);
    }
    CPPL_CHECK(listed == expected);
}

CPPL_TEST(a_word_is_a_cppl_word_only_as_spelled) {
    CPPL_CHECK(cppl::frontend::is_cppl_word("law"));
    CPPL_CHECK(cppl::frontend::is_cppl_word("contradiction"));
    CPPL_CHECK(cppl::frontend::is_cppl_word("result"));
    CPPL_CHECK(!cppl::frontend::is_cppl_word("Law"));
    CPPL_CHECK(!cppl::frontend::is_cppl_word("laws"));
    // An arm label is a C++L word only in an arm (WORD-005).
    CPPL_CHECK(!cppl::frontend::is_cppl_word("none"));
    CPPL_CHECK(!cppl::frontend::is_cppl_word(""));
}
