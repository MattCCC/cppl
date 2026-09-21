#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// docs/SPEC.md is the single canonical normative source; this tool never splits
// it. It assigns stable [FAMILY-NNN] anchors so code, tests, diagnostics and the
// docs/agent/ index can cite a rule without depending on line or section
// numbers, both of which move as the specification grows.

struct FamilyEntry {
    int chapter;
    std::string_view family;
};

// Chapter number -> rule family. The main body states most rules as declarative
// prose rather than with MUST, so families are assigned by chapter, not keyword.
constexpr std::array<FamilyEntry, 65> kChapterFamilies{{
    {1, "TERM"},        {2, "CXX"},          {3, "WORD"},          {4, "DOMAIN"},       {5, "PROP"},
    {6, "BOOL"},        {7, "EQ"},           {8, "FORALL"},        {9, "EXISTS"},       {10, "LAW"},
    {11, "CONTRACT"},   {12, "VERIFIED"},    {13, "PURE"},         {14, "SPECEXPR"},    {15, "PROOF"},
    {16, "REFL"},       {17, "REFINE"},      {18, "DEP"},          {19, "CXXTYPE"},     {20, "CASE"},
    {21, "INDUCT"},     {22, "TERMINATION"}, {23, "CORRECT"},      {24, "LOOP"},        {25, "GHOST"},
    {26, "UNSAFE"},     {27, "TRUSTED"},     {28, "RUNTIMECHECK"}, {29, "ARITH"},       {30, "FLOAT"},
    {31, "UB"},         {32, "MEM"},         {33, "EXCEPT"},       {34, "CONCUR"},      {35, "FFI"},
    {36, "ERASE"},      {37, "ABI"},         {38, "STATUS"},       {39, "STATUSPROMO"}, {40, "BOUNDARY"},
    {41, "CALL"},       {42, "TEMPLATE"},    {43, "SCOPE"},        {44, "TU"},          {45, "MODULE"},
    {46, "FILEEXT"},    {47, "LAWIMPL"},     {48, "TESTPROOF"},    {49, "ASSERT"},      {50, "CEX"},
    {51, "IMPOSSIBLE"}, {52, "FAILCLOSED"},  {53, "PROOFFAIL"},    {54, "SPECFAIL"},    {55, "IRRELEVANCE"},
    {56, "ORTHOTRUST"}, {57, "ORTHOCHECK"},  {58, "SOUND"},        {59, "LANGVERIFY"},  {60, "SRCCOMPAT"},
    {61, "CONFORM"},    {62, "NONGOAL"},     {63, "EXAMPLE"},      {64, "SEMMODEL"},    {65, "FUNDAMENTAL"},
}};

struct AnnexEntry {
    char letter;
    std::string_view family;
};

constexpr std::array<AnnexEntry, 26> kAnnexFamilies{{
    {'A', "JUDGMENT"},     {'B', "EXPR"},      {'C', "STMT"},         {'D', "DECL"},      {'E', "STORAGE"},
    {'F', "CLASS"},        {'G', "TEMPLATE"},  {'H', "PROOFSRC"},     {'I', "REFINEOBL"}, {'J', "STDMODEL"},
    {'K', "EXCEPTCONCUR"}, {'L', "TUBOUND"},   {'M', "ERASEMATRIX"},  {'N', "COVERAGE"},  {'O', "BOUNDARYEX"},
    {'P', "ACCEPTANCE"},   {'Q', "INVENTORY"}, {'R', "CONTRACTCOMP"}, {'S', "DECLCOVER"}, {'T', "DEFINEDBEHAVIOR"},
    {'U', "ADMISSIBLE"},   {'V', "INTERACT"},  {'W', "CORPUS"},       {'X', "CONSTRUCT"}, {'Y', "STDOBL"},
    {'Z', "EDGECASE"},
}};

// Annexes N, X and Y are mechanical per-construct catalogues restating the same
// dimensions for every construct. Tagging each bullet would produce thousands of
// near-duplicate IDs, so they get one ID per construct entry (per section).
bool is_catalogue_annex(char annex) {
    return annex == 'N' || annex == 'X' || annex == 'Y';
}

std::string_view family_for_chapter(int chapter) {
    for (const auto& entry : kChapterFamilies) {
        if (entry.chapter == chapter) {
            return entry.family;
        }
    }
    return {};
}

std::string_view family_for_annex(char letter) {
    for (const auto& entry : kAnnexFamilies) {
        if (entry.letter == letter) {
            return entry.family;
        }
    }
    return {};
}

std::string trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

struct Rule {
    std::string id;
    std::string family;
    int number = 0;
    std::size_t line = 0;
    std::string chapter;
    std::string section;
    std::string section_title;
    std::string text;
};

// Matches [FAMILY-NNN] where FAMILY is uppercase alphanumeric with optional
// hyphenated parts, e.g. [REFINE-001] or [REFINE-WRITE-001].
std::optional<std::pair<std::string, int>> find_rule_id(std::string_view text) {
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != '[') {
            continue;
        }
        const auto close = text.find(']', i);
        if (close == std::string_view::npos) {
            continue;
        }
        const std::string_view inner = text.substr(i + 1, close - i - 1);
        if (inner.size() < 5 || inner[0] < 'A' || inner[0] > 'Z') {
            continue;
        }
        const auto dash = inner.rfind('-');
        if (dash == std::string_view::npos || dash + 4 != inner.size()) {
            continue;
        }
        const std::string_view digits = inner.substr(dash + 1);
        if (!std::all_of(digits.begin(), digits.end(),
                         [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            continue;
        }
        const std::string_view family = inner.substr(0, dash);
        const bool valid = std::all_of(family.begin(), family.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        });
        if (!valid) {
            continue;
        }
        return std::make_pair(std::string(family), std::stoi(std::string(digits)));
    }
    return std::nullopt;
}

bool is_known_family(std::string_view family) {
    const auto chapter_match = std::any_of(kChapterFamilies.begin(), kChapterFamilies.end(),
                                           [&](const FamilyEntry& e) { return e.family == family; });
    return chapter_match || std::any_of(kAnnexFamilies.begin(), kAnnexFamilies.end(),
                                        [&](const AnnexEntry& e) { return e.family == family; });
}

// Finds every rule citation on a line, in either form: the bracketed anchor
// [REFINE-010] used inside docs/SPEC.md, and the bare FAMILY-NNN that code and
// tests cite after `SPEC:`. Only the latter appears in a test comment, so
// scanning for brackets alone would report no coverage at all. The family must
// be a known one, so that identifiers like SHA-256 are not read as citations.
std::vector<std::string> find_citations(std::string_view text) {
    std::vector<std::string> found;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c < 'A' || c > 'Z') {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() &&
               ((text[end] >= 'A' && text[end] <= 'Z') || (text[end] >= '0' && text[end] <= '9') || text[end] == '-')) {
            ++end;
        }
        const std::string_view token = text.substr(i, end - i);
        i = end > i ? end : i + 1;

        const auto dash = token.rfind('-');
        if (dash == std::string_view::npos || dash == 0 || dash + 4 != token.size()) {
            continue;
        }
        const std::string_view digits = token.substr(dash + 1);
        if (!std::all_of(digits.begin(), digits.end(),
                         [](char d) { return std::isdigit(static_cast<unsigned char>(d)); })) {
            continue;
        }
        const std::string_view family = token.substr(0, dash);
        if (!is_known_family(family)) {
            continue;
        }
        found.emplace_back(token);
    }
    return found;
}

std::string format_rule_id(std::string_view family, int number) {
    std::ostringstream out;
    out << family << '-';
    out.width(3);
    out.fill('0');
    out << number;
    return out.str();
}

// A paragraph or list item between blank lines, outside code fences.
struct Block {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string family;
    char annex = '\0';
    std::string chapter;
    std::string section;
    std::string section_title;
};

std::optional<int> parse_chapter_number(std::string_view heading) {
    std::size_t i = 0;
    while (i < heading.size() && std::isdigit(static_cast<unsigned char>(heading[i]))) {
        ++i;
    }
    if (i == 0 || i >= heading.size() || heading[i] != '.') {
        return std::nullopt;
    }
    return std::stoi(std::string(heading.substr(0, i)));
}

std::optional<char> parse_annex_letter(std::string_view heading) {
    constexpr std::string_view prefix = "Normative Annex ";
    if (!starts_with(heading, prefix) || heading.size() <= prefix.size()) {
        return std::nullopt;
    }
    const char letter = heading[prefix.size()];
    if (letter < 'A' || letter > 'Z') {
        return std::nullopt;
    }
    return letter;
}

// Parses "## 17.2 Introduction and construction" into section and title.
bool parse_section_heading(std::string_view line, std::string& section, std::string& title) {
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') {
        ++hashes;
    }
    if (hashes < 2 || hashes > 4 || hashes >= line.size() || line[hashes] != ' ') {
        return false;
    }
    const std::string rest = trim(line.substr(hashes + 1));
    const auto space = rest.find(' ');
    if (space == std::string::npos) {
        return false;
    }
    const std::string candidate = rest.substr(0, space);
    const bool numeric = std::all_of(candidate.begin(), candidate.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || c == '.' || (c >= 'A' && c <= 'Z');
    });
    if (!numeric || candidate.find_first_of("0123456789") == std::string::npos) {
        return false;
    }
    section = candidate;
    title = rest.substr(space + 1);
    return true;
}

std::vector<Block> collect_blocks(const std::vector<std::string>& lines) {
    std::vector<Block> blocks;
    bool in_fence = false;
    std::string family;
    char annex = '\0';
    std::string chapter = "(preamble)";
    std::string section;
    std::string section_title;
    std::optional<std::size_t> buffer_start;

    const auto flush = [&](std::size_t end) {
        if (!buffer_start.has_value()) {
            return;
        }
        blocks.push_back(Block{*buffer_start, end, family, annex, chapter, section, section_title});
        buffer_start.reset();
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& raw = lines[i];
        const std::string stripped = trim(raw);

        if (starts_with(stripped, "```")) {
            in_fence = !in_fence;
            flush(i == 0 ? 0 : i - 1);
            continue;
        }
        if (in_fence) {
            continue;
        }

        if (starts_with(raw, "# ")) {
            flush(i == 0 ? 0 : i - 1);
            const std::string heading = trim(raw.substr(2));
            chapter = heading;
            section.clear();
            section_title.clear();
            if (const auto letter = parse_annex_letter(heading)) {
                annex = *letter;
                family = std::string(family_for_annex(*letter));
            } else if (const auto number = parse_chapter_number(heading)) {
                annex = '\0';
                family = std::string(family_for_chapter(*number));
            } else {
                annex = '\0';
                family.clear();
            }
            continue;
        }

        std::string next_section;
        std::string next_title;
        if (parse_section_heading(raw, next_section, next_title)) {
            flush(i == 0 ? 0 : i - 1);
            section = next_section;
            section_title = next_title;
            continue;
        }

        if (stripped.empty()) {
            flush(i == 0 ? 0 : i - 1);
            continue;
        }

        if (!buffer_start.has_value()) {
            buffer_start = i;
        }
    }
    flush(lines.empty() ? 0 : lines.size() - 1);
    return blocks;
}

std::string block_text(const std::vector<std::string>& lines, const Block& block) {
    std::string joined;
    for (std::size_t i = block.start; i <= block.end && i < lines.size(); ++i) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += trim(lines[i]);
    }
    return trim(joined);
}

std::string strip_list_marker(std::string_view text) {
    if (starts_with(text, "- ") || starts_with(text, "* ")) {
        return trim(text.substr(2));
    }
    return trim(text);
}

bool has_normative_lead(std::string_view text) {
    static constexpr std::array<std::string_view, 26> leads{"A ",
                                                            "An ",
                                                            "The ",
                                                            "Every ",
                                                            "Each ",
                                                            "No ",
                                                            "Any ",
                                                            "If ",
                                                            "When ",
                                                            "Where ",
                                                            "Within ",
                                                            "For ",
                                                            "This ",
                                                            "These ",
                                                            "Only ",
                                                            "Implementation",
                                                            "Implementations",
                                                            "Verification",
                                                            "Erasure",
                                                            "Proof",
                                                            "Trusted",
                                                            "Unsafe",
                                                            "C++L",
                                                            "Runtime",
                                                            "Mutation",
                                                            "Facts"};
    return std::any_of(leads.begin(), leads.end(), [&](std::string_view lead) { return starts_with(text, lead); });
}

bool has_nonnormative_lead(std::string_view text) {
    static constexpr std::array<std::string_view, 7> leads{"For example", "Example", "Note", "Rationale",
                                                           "That is",     "e.g.",    "i.e."};
    return std::any_of(leads.begin(), leads.end(), [&](std::string_view lead) { return starts_with(text, lead); });
}

// Each catalogue entry opens with a "Primary semantic focus:" preamble before
// its normative bullets; the entry's ID anchors on the first real requirement.
bool is_normative_catalogue_entry(std::string_view text) {
    const std::string stripped = strip_list_marker(text);
    return !starts_with(stripped, "Primary semantic focus") && stripped.size() > 60;
}

bool is_normative(std::string_view text, char annex) {
    const std::string stripped = strip_list_marker(text);
    if (stripped.empty() || has_nonnormative_lead(stripped)) {
        return false;
    }
    if (stripped.find("MUST") != std::string::npos) {
        return true;
    }
    if (is_catalogue_annex(annex)) {
        return false;
    }
    return has_normative_lead(stripped) && stripped.size() > 60;
}

std::optional<std::vector<std::string>> read_lines(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool write_lines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        stream << lines[i];
        if (i + 1 < lines.size()) {
            stream << '\n';
        }
    }
    stream << '\n';
    return static_cast<bool>(stream);
}

std::vector<Rule> parse_rules(const std::vector<std::string>& lines) {
    std::vector<Rule> rules;
    for (const Block& block : collect_blocks(lines)) {
        const std::string text = block_text(lines, block);
        if (const auto found = find_rule_id(text)) {
            Rule rule;
            rule.family = found->first;
            rule.number = found->second;
            rule.id = format_rule_id(rule.family, rule.number);
            rule.line = block.start + 1;
            rule.chapter = block.chapter;
            rule.section = block.section;
            rule.section_title = block.section_title;
            rule.text = text;
            rules.push_back(std::move(rule));
        }
    }
    return rules;
}

int command_assign(const std::string& spec_path, bool dry_run) {
    const auto lines_opt = read_lines(spec_path);
    if (!lines_opt.has_value()) {
        std::cerr << "cppl-spec-rules: could not read '" << spec_path << "'\n";
        return 1;
    }
    std::vector<std::string> lines = *lines_opt;

    std::map<std::string, int> counters;
    for (const Rule& rule : parse_rules(lines)) {
        counters[rule.family] = std::max(counters[rule.family], rule.number);
    }

    // A catalogue section is anchored by exactly one ID. Sections that already
    // carry one from an earlier run are recorded first, so assignment stays
    // idempotent instead of tagging a different block on each pass.
    const std::vector<Block> blocks = collect_blocks(lines);
    std::set<std::string> anchored_sections;
    for (const Block& block : blocks) {
        if (is_catalogue_annex(block.annex) && !block.section.empty() &&
            find_rule_id(block_text(lines, block)).has_value()) {
            anchored_sections.insert(std::string(1, block.annex) + ":" + block.section);
        }
    }

    std::vector<std::pair<std::size_t, std::string>> inserts;

    for (const Block& block : blocks) {
        if (block.family.empty()) {
            continue;
        }
        const std::string text = block_text(lines, block);
        if (find_rule_id(text).has_value()) {
            continue;
        }

        if (is_catalogue_annex(block.annex)) {
            // Anchor the entry on its first normative bullet, not on the
            // "Primary semantic focus" preamble that opens each entry.
            if (block.section.empty() || !is_normative_catalogue_entry(text)) {
                continue;
            }
            const std::string key = std::string(1, block.annex) + ":" + block.section;
            if (!anchored_sections.insert(key).second) {
                continue;
            }
        } else if (!is_normative(text, block.annex)) {
            continue;
        }

        const int number = ++counters[block.family];
        inserts.emplace_back(block.start, format_rule_id(block.family, number));
    }

    if (inserts.empty()) {
        std::cout << "no unlabelled normative statements found\n";
        return 0;
    }

    if (dry_run) {
        std::cout << "would assign " << inserts.size() << " rule IDs\n";
        for (std::size_t i = 0; i < inserts.size() && i < 20; ++i) {
            std::cout << "  " << inserts[i].second << '\n';
        }
        if (inserts.size() > 20) {
            std::cout << "  ... and " << (inserts.size() - 20) << " more\n";
        }
        return 0;
    }

    // Applied back to front so that earlier insertion indices stay valid.
    // NOLINTNEXTLINE(modernize-loop-convert): reverse order is load-bearing.
    for (auto it = inserts.rbegin(); it != inserts.rend(); ++it) {
        std::string& line = lines[it->first];
        const std::string stripped = trim(line);
        const auto first = line.find_first_not_of(" \t");

        std::string rewritten = first == std::string::npos ? std::string{} : line.substr(0, first);
        std::string body = stripped;
        if (starts_with(stripped, "- ") || starts_with(stripped, "* ")) {
            rewritten.append(stripped, 0, 2);
            body = stripped.substr(2);
        }
        rewritten += '[';
        rewritten += it->second;
        rewritten += "] ";
        rewritten += body;
        line = std::move(rewritten);
    }

    if (!write_lines(spec_path, lines)) {
        std::cerr << "cppl-spec-rules: could not write '" << spec_path << "'\n";
        return 1;
    }
    std::cout << "assigned " << inserts.size() << " rule IDs in " << spec_path << '\n';
    return 0;
}

// Collects `[RULE-NNN]` citations from implementation, tests and the agent layer.
std::map<std::string, std::vector<std::string>> collect_citations(const std::filesystem::path& repo) {
    std::map<std::string, std::vector<std::string>> citations;
    const std::array<std::string_view, 5> roots{"compiler", "tests", "kernel", "vir", "docs/agent"};
    // Negative tests in this repository are shell scripts, so .sh citations
    // count toward coverage exactly as C++ ones do.
    const std::array<std::string_view, 7> extensions{".cpp", ".hpp", ".h", ".md", ".yaml", ".yml", ".sh"};

    for (std::string_view root : roots) {
        const std::filesystem::path base = repo / root;
        std::error_code ec;
        if (!std::filesystem::exists(base, ec)) {
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(base, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            const std::string ext = it->path().extension().string();
            if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) {
                continue;
            }
            std::ifstream stream(it->path(), std::ios::binary);
            if (!stream) {
                continue;
            }
            const std::string relative = std::filesystem::relative(it->path(), repo).string();
            std::string line;
            while (std::getline(stream, line)) {
                for (const std::string& id : find_citations(line)) {
                    auto& where = citations[id];
                    if (std::find(where.begin(), where.end(), relative) == where.end()) {
                        where.push_back(relative);
                    }
                }
            }
        }
    }
    return citations;
}

int command_check(const std::string& spec_path, const std::filesystem::path& repo, bool allow_gaps) {
    const auto lines_opt = read_lines(spec_path);
    if (!lines_opt.has_value()) {
        std::cerr << "cppl-spec-rules: could not read '" << spec_path << "'\n";
        return 1;
    }
    const std::vector<Rule> rules = parse_rules(*lines_opt);
    std::vector<std::string> errors;

    std::map<std::string, const Rule*> seen;
    for (const Rule& rule : rules) {
        const auto [it, inserted] = seen.emplace(rule.id, &rule);
        if (!inserted) {
            errors.push_back("duplicate rule id " + rule.id + " at lines " + std::to_string(it->second->line) +
                             " and " + std::to_string(rule.line));
        }
    }

    std::map<std::string, std::vector<int>> by_family;
    for (const Rule& rule : rules) {
        by_family[rule.family].push_back(rule.number);
    }
    if (!allow_gaps) {
        for (auto& [family, numbers] : by_family) {
            std::sort(numbers.begin(), numbers.end());
            std::vector<int> missing;
            for (int n = 1; n <= numbers.back(); ++n) {
                if (!std::binary_search(numbers.begin(), numbers.end(), n)) {
                    missing.push_back(n);
                }
            }
            if (!missing.empty()) {
                std::string message = "gaps in ";
                message += family;
                message += ": ";
                for (std::size_t i = 0; i < missing.size() && i < 8; ++i) {
                    if (i > 0) {
                        message += ", ";
                    }
                    message += format_rule_id(family, missing[i]);
                }
                if (missing.size() > 8) {
                    message += " ...";
                }
                errors.push_back(std::move(message));
            }
        }
    }

    for (const auto& [id, where] : collect_citations(repo)) {
        if (!seen.contains(id)) {
            errors.push_back("orphan citation " + id + " in " + where.front() + " (no such rule in SPEC.md)");
        }
    }

    if (!errors.empty()) {
        for (const std::string& error : errors) {
            std::cerr << "error: " << error << '\n';
        }
        std::cerr << '\n' << errors.size() << " problem(s); " << rules.size() << " rules parsed\n";
        return 1;
    }

    std::cout << "ok: " << rules.size() << " rules, " << by_family.size() << " families, no duplicates or orphans\n";
    return 0;
}

int command_report(const std::string& spec_path, const std::filesystem::path& repo, bool show_all) {
    const auto lines_opt = read_lines(spec_path);
    if (!lines_opt.has_value()) {
        std::cerr << "cppl-spec-rules: could not read '" << spec_path << "'\n";
        return 1;
    }
    const std::vector<Rule> rules = parse_rules(*lines_opt);
    const auto citations = collect_citations(repo);

    std::size_t with_impl = 0;
    std::size_t with_tests = 0;
    std::vector<std::tuple<std::string, std::size_t, std::size_t>> rows;

    for (const Rule& rule : rules) {
        std::size_t impl = 0;
        std::size_t tests = 0;
        if (const auto it = citations.find(rule.id); it != citations.end()) {
            for (const std::string& where : it->second) {
                if (starts_with(where, "compiler/") || starts_with(where, "kernel/") || starts_with(where, "vir/")) {
                    ++impl;
                } else if (starts_with(where, "tests/")) {
                    ++tests;
                }
            }
        }
        with_impl += impl > 0 ? 1 : 0;
        with_tests += tests > 0 ? 1 : 0;
        rows.emplace_back(rule.id, impl, tests);
    }

    std::cout << "rules:          " << rules.size() << '\n';
    std::cout << "cited by impl:  " << with_impl << '\n';
    std::cout << "cited by tests: " << with_tests << "\n\n";
    std::cout << "RULE                      IMPL  TESTS\n";
    for (const auto& [id, impl, tests] : rows) {
        if (show_all || impl > 0 || tests > 0) {
            std::cout << id;
            for (std::size_t i = id.size(); i < 26; ++i) {
                std::cout << ' ';
            }
            std::cout << impl << "     " << tests << '\n';
        }
    }
    return 0;
}

// Reads `rule_families:` from a feature manifest. The manifest lists families
// rather than individual IDs so that a rule added to docs/SPEC.md is picked up
// without editing the manifest.
std::optional<std::vector<std::string>> read_feature_families(const std::filesystem::path& repo,
                                                              const std::string& feature) {
    const std::filesystem::path path = repo / "docs" / "agent" / "features" / (feature + ".yaml");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::string> families;
    std::string line;
    bool in_families = false;
    while (std::getline(stream, line)) {
        const std::string stripped = trim(line);
        if (starts_with(stripped, "rule_families:")) {
            in_families = true;
            continue;
        }
        if (in_families) {
            if (starts_with(stripped, "- ")) {
                families.push_back(trim(stripped.substr(2)));
                continue;
            }
            if (!stripped.empty() && !starts_with(stripped, "#")) {
                break;
            }
        }
    }
    return families;
}

int command_extract(const std::string& spec_path, const std::filesystem::path& repo,
                    const std::vector<std::string>& rule_ids, const std::string& feature) {
    const auto lines_opt = read_lines(spec_path);
    if (!lines_opt.has_value()) {
        std::cerr << "cppl-spec-rules: could not read '" << spec_path << "'\n";
        return 1;
    }
    std::map<std::string, const Rule*> by_id;
    const std::vector<Rule> rules = parse_rules(*lines_opt);
    for (const Rule& rule : rules) {
        by_id[rule.id] = &rule;
    }

    std::vector<std::string> wanted = rule_ids;
    if (!feature.empty()) {
        const auto families = read_feature_families(repo, feature);
        if (!families.has_value()) {
            std::cerr << "cppl-spec-rules: no manifest for feature '" << feature << "'\n";
            return 1;
        }
        if (families->empty()) {
            std::cerr << "cppl-spec-rules: feature '" << feature << "' lists no rule_families\n";
            return 1;
        }
        for (const Rule& rule : rules) {
            if (std::find(families->begin(), families->end(), rule.family) != families->end()) {
                wanted.push_back(rule.id);
            }
        }
        if (wanted.empty()) {
            std::cerr << "cppl-spec-rules: no rules found for feature '" << feature << "'; has 'assign' been run?\n";
            return 1;
        }
    }

    bool missing = false;
    for (const std::string& id : wanted) {
        if (!by_id.contains(id)) {
            std::cerr << "cppl-spec-rules: unknown rule '" << id << "'\n";
            missing = true;
        }
    }
    if (missing) {
        return 1;
    }

    for (const std::string& id : wanted) {
        const Rule& rule = *by_id.at(id);
        std::cout << "## " << rule.id << "\n\n";
        std::cout << "Source: docs/SPEC.md:" << rule.line << " — " << rule.chapter;
        if (!rule.section.empty()) {
            std::cout << " §" << rule.section << ' ' << rule.section_title;
        }
        std::cout << "\n\n" << rule.text << "\n\n";
    }
    return 0;
}

void print_usage() {
    std::cerr << "usage: cppl-spec-rules [--spec PATH] [--repo PATH] <command> [options]\n"
                 "\n"
                 "commands:\n"
                 "  assign [--dry-run]      insert IDs into unlabelled normative statements\n"
                 "  check [--allow-gaps]    validate uniqueness and citations\n"
                 "  report [--all]          rule -> implementation -> test coverage\n"
                 "  extract RULE-ID...      print the normative text for given rules\n"
                 "  extract --feature=NAME  print every rule for a feature manifest\n";
}

} // namespace

// Assigns and validates stable normative rule IDs in docs/SPEC.md, which
// remains the single canonical specification (docs/agent/README.md).
int main(int argc, char** argv) {
    std::string spec_path = "docs/SPEC.md";
    std::filesystem::path repo = ".";
    std::string command;
    std::string feature;
    std::vector<std::string> positional;
    bool dry_run = false;
    bool allow_gaps = false;
    bool show_all = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.starts_with("--spec=")) {
            spec_path = argument.substr(std::string_view("--spec=").size());
        } else if (argument.starts_with("--repo=")) {
            repo = argument.substr(std::string_view("--repo=").size());
        } else if (argument.starts_with("--feature=")) {
            feature = argument.substr(std::string_view("--feature=").size());
        } else if (argument == "--dry-run") {
            dry_run = true;
        } else if (argument == "--allow-gaps") {
            allow_gaps = true;
        } else if (argument == "--all") {
            show_all = true;
        } else if (argument == "-h" || argument == "--help") {
            print_usage();
            return 0;
        } else if (argument.starts_with("-")) {
            std::cerr << "cppl-spec-rules: unknown option '" << argument << "'\n";
            return 2;
        } else if (command.empty()) {
            command = argument;
        } else {
            positional.push_back(argument);
        }
    }

    if (command.empty()) {
        print_usage();
        return 2;
    }
    if (command == "assign") {
        return command_assign(spec_path, dry_run);
    }
    if (command == "check") {
        return command_check(spec_path, repo, allow_gaps);
    }
    if (command == "report") {
        return command_report(spec_path, repo, show_all);
    }
    if (command == "extract") {
        if (positional.empty() && feature.empty()) {
            std::cerr << "cppl-spec-rules: extract requires a rule id or --feature\n";
            return 2;
        }
        return command_extract(spec_path, repo, positional, feature);
    }

    std::cerr << "cppl-spec-rules: unknown command '" << command << "'\n";
    print_usage();
    return 2;
}
