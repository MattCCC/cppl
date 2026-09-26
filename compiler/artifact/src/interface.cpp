#include "cppl/artifact/interface.hpp"

#include "cppl/source/digest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppl::artifact {
namespace {

constexpr std::string_view kUpperHex = "0123456789ABCDEF";
constexpr std::string_view kLowerHex = "0123456789abcdef";

// The most fields any line has: `premise <identity> <line> <file> <name>`.
constexpr std::size_t kMaxFields = 5;

// A byte a token may hold as itself. Everything else, the separator and the
// escape character included, is written as `%XX`.
bool plain(unsigned char byte) {
    return byte >= 0x21U && byte <= 0x7EU && byte != '%';
}

std::string encode(std::string_view text) {
    std::string encoded;
    encoded.reserve(text.size());
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (plain(byte)) {
            encoded.push_back(character);
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kUpperHex[static_cast<std::size_t>(byte >> 4U)]);
        encoded.push_back(kUpperHex[static_cast<std::size_t>(byte & 0x0FU)]);
    }
    return encoded;
}

std::optional<unsigned> digit_value(char character, std::string_view digits) {
    const std::size_t position = digits.find(character);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    return static_cast<unsigned>(position);
}

// The one spelling `encode` gives a text: no raw byte that must be escaped, no
// escape of a byte that need not be, upper-case hex only, and never empty.
std::optional<std::string> decode(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    std::string decoded;
    decoded.reserve(token.size());
    for (std::size_t index = 0; index < token.size(); ++index) {
        const auto byte = static_cast<unsigned char>(token[index]);
        if (byte != '%') {
            if (!plain(byte)) {
                return std::nullopt;
            }
            decoded.push_back(token[index]);
            continue;
        }
        if (token.size() - index < 3) {
            return std::nullopt;
        }
        const std::optional<unsigned> high = digit_value(token[index + 1], kUpperHex);
        const std::optional<unsigned> low = digit_value(token[index + 2], kUpperHex);
        if (!high.has_value() || !low.has_value()) {
            return std::nullopt;
        }
        const auto value = static_cast<unsigned char>((*high << 4U) | *low);
        if (plain(value)) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>(value));
        index += 2;
    }
    return decoded;
}

std::optional<source::Digest> parse_digest(std::string_view token) {
    source::Digest digest;
    if (token.size() != digest.bytes.size() * 2) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        const std::optional<unsigned> high = digit_value(token[2 * index], kLowerHex);
        const std::optional<unsigned> low = digit_value(token[(2 * index) + 1], kLowerHex);
        if (!high.has_value() || !low.has_value()) {
            return std::nullopt;
        }
        digest.bytes[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return digest;
}

// A decimal with no sign, no leading zero and no more than a 32-bit value.
std::optional<std::uint32_t> parse_number(std::string_view token, std::uint32_t least) {
    constexpr std::size_t kMaxDigits = 10;
    if (token.empty() || token.size() > kMaxDigits || (token.size() > 1 && token.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char character : token) {
        const std::optional<unsigned> digit = digit_value(character, kLowerHex.substr(0, 10));
        if (!digit.has_value()) {
            return std::nullopt;
        }
        value = (value * 10U) + *digit;
    }
    if (value > std::numeric_limits<std::uint32_t>::max() || value < least) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::string_view describe(Correctness correctness) {
    return correctness == Correctness::Total ? "total" : "partial";
}

std::string premise_line(const Premise& premise) {
    return "premise " + premise.identity.to_hex() + " " + std::to_string(premise.line) + " " + encode(premise.file) +
           " " + encode(premise.name);
}

std::string unsafe_line(const UnsafeBlock& block) {
    return "unsafe " + std::to_string(block.line) + " " + std::to_string(block.column) + " " + encode(block.file);
}

std::string dependency_line(const Dependency& dependency) {
    return "depends " + dependency.entry.to_hex() + " " + encode(dependency.symbol);
}

// A repeated item's lines in canonical order, each once.
template <typename Item, typename Render>
std::vector<std::string> canonical_lines(const std::vector<Item>& items, Render render) {
    std::vector<std::string> lines;
    lines.reserve(items.size());
    for (const Item& item : items) {
        lines.push_back(render(item));
    }
    std::ranges::sort(lines);
    const auto duplicates = std::ranges::unique(lines);
    lines.erase(duplicates.begin(), duplicates.end());
    return lines;
}

// One entry's lines, `entry` through `end`, each ending in a newline.
std::string entry_text(const Entry& entry) {
    std::string text;
    const auto line = [&text](std::string_view content) {
        text += content;
        text += '\n';
    };
    line("entry " + encode(entry.symbol));
    line("name " + encode(entry.name));
    line("statement " + entry.statement.to_hex());
    line("contract " + encode(entry.contract));
    line("status proven");
    line("correctness " + std::string(describe(entry.correctness)));
    for (const std::string& premise : canonical_lines(entry.premises, premise_line)) {
        line(premise);
    }
    for (const std::string& block : canonical_lines(entry.unsafe, unsafe_line)) {
        line(block);
    }
    for (const std::string& dependency : canonical_lines(entry.depends, dependency_line)) {
        line(dependency);
    }
    line("end");
    return text;
}

// Why an entry could not be read back as written, if it could not.
std::optional<std::string> entry_problem(const Entry& entry) {
    if (entry.symbol.empty() || entry.name.empty() || entry.contract.empty()) {
        return "an entry lacks its symbol, name or contract";
    }
    for (const Premise& premise : entry.premises) {
        if (premise.name.empty() || premise.file.empty() || premise.line == 0) {
            return "a premise of '" + entry.name + "' lacks its name or location";
        }
    }
    for (const UnsafeBlock& block : entry.unsafe) {
        if (block.file.empty() || block.line == 0) {
            return "an unsafe block of '" + entry.name + "' lacks its location";
        }
    }
    for (const Dependency& dependency : entry.depends) {
        if (dependency.symbol.empty()) {
            return "a dependency of '" + entry.name + "' lacks its symbol";
        }
    }
    if (entry.premises.size() > kMaxEntryItems || entry.unsafe.size() > kMaxEntryItems ||
        entry.depends.size() > kMaxEntryItems) {
        return "'" + entry.name + "' rests on more than " + std::to_string(kMaxEntryItems) + " items of one kind";
    }
    return std::nullopt;
}

// A text taken from the input, shortened and kept printable for a diagnostic.
std::string quoted(std::string_view text) {
    constexpr std::size_t kShown = 64;
    std::string shown;
    for (const char character : text.substr(0, kShown)) {
        const auto byte = static_cast<unsigned char>(character);
        shown.push_back(byte >= 0x20U && byte <= 0x7EU ? character : '?');
    }
    if (text.size() > kShown) {
        shown += "...";
    }
    return "'" + shown + "'";
}

struct Line {
    std::size_t number = 0;
    std::string_view text;
    std::array<std::string_view, kMaxFields> fields{};
    std::size_t count = 0;

    [[nodiscard]] std::string_view key() const {
        return fields[0];
    }
};

// Reads the body one line at a time. Nothing is split up front, so memory does
// not grow with the number of lines an input has.
class Parser {
  public:
    Parser(std::string_view body, std::size_t first_number) : rest_(body), number_(first_number) {}

    [[nodiscard]] bool done() const {
        return rest_.empty();
    }

    // The next line, whatever its key, without consuming it.
    [[nodiscard]] std::expected<Line, ParseError> peek() const {
        const std::size_t end = rest_.find('\n');
        if (end == std::string_view::npos) {
            // The body is the text between the first and the checksum line, so
            // every line of it ends in a newline.
            return std::unexpected(ParseError{"a line is not terminated", number_});
        }
        if (end > kMaxLineBytes) {
            return std::unexpected(
                ParseError{"a line is longer than the " + std::to_string(kMaxLineBytes) + " bytes allowed", number_});
        }
        Line line;
        line.number = number_;
        line.text = rest_.substr(0, end);
        for (const char character : line.text) {
            const auto byte = static_cast<unsigned char>(character);
            if (byte < 0x20U || byte > 0x7EU) {
                return std::unexpected(ParseError{"a line holds a byte that must be escaped", number_});
            }
        }
        std::string_view remaining = line.text;
        while (true) {
            const std::size_t space = remaining.find(' ');
            const std::string_view field = remaining.substr(0, space);
            if (field.empty()) {
                return std::unexpected(ParseError{"a line has an empty field", number_});
            }
            if (line.count == kMaxFields) {
                return std::unexpected(ParseError{"a line has too many fields", number_});
            }
            line.fields[line.count++] = field;
            if (space == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(space + 1);
        }
        return line;
    }

    // The next line, which must have `key` and exactly `arity` fields after it.
    [[nodiscard]] std::expected<Line, ParseError> take(std::string_view key, std::size_t arity) {
        if (done()) {
            return std::unexpected(ParseError{"it ends where '" + std::string(key) + "' was expected", number_});
        }
        std::expected<Line, ParseError> line = peek();
        if (!line) {
            return line;
        }
        if (line->key() != key) {
            return std::unexpected(ParseError{
                "found " + quoted(line->key()) + " where '" + std::string(key) + "' was expected", line->number});
        }
        return consume(*line, arity);
    }

    // The next line when it has `key`; nothing, and nothing consumed, otherwise.
    [[nodiscard]] std::expected<std::optional<Line>, ParseError> take_if(std::string_view key, std::size_t arity) {
        if (done()) {
            return std::optional<Line>{};
        }
        std::expected<Line, ParseError> line = peek();
        if (!line) {
            return std::unexpected(line.error());
        }
        if (line->key() != key) {
            return std::optional<Line>{};
        }
        std::expected<Line, ParseError> taken = consume(*line, arity);
        if (!taken) {
            return std::unexpected(taken.error());
        }
        return std::optional<Line>{*taken};
    }

  private:
    // A line only views the text, so the one consumed is returned as a copy.
    std::expected<Line, ParseError> consume(const Line& line, std::size_t arity) {
        if (line.count != arity + 1) {
            return std::unexpected(ParseError{"'" + std::string(line.key()) + "' has " +
                                                  std::to_string(line.count - 1) + " fields, not " +
                                                  std::to_string(arity),
                                              line.number});
        }
        rest_.remove_prefix(line.text.size() + 1);
        ++number_;
        return line;
    }

    std::string_view rest_;
    std::size_t number_;
};

std::unexpected<ParseError> malformed(std::string what, const Line& line) {
    return std::unexpected(ParseError{std::move(what), line.number});
}

std::expected<std::string, ParseError> text_field(const Line& line, std::size_t index) {
    std::optional<std::string> decoded = decode(line.fields[index]);
    if (!decoded.has_value()) {
        return malformed("'" + std::string(line.key()) + "' holds a field that is not canonically encoded", line);
    }
    return std::move(*decoded);
}

std::expected<source::Digest, ParseError> digest_field(const Line& line, std::size_t index) {
    const std::optional<source::Digest> digest = parse_digest(line.fields[index]);
    if (!digest.has_value()) {
        return malformed("'" + std::string(line.key()) + "' holds a malformed digest", line);
    }
    return *digest;
}

std::expected<std::uint32_t, ParseError> number_field(const Line& line, std::size_t index, std::uint32_t least) {
    const std::optional<std::uint32_t> number = parse_number(line.fields[index], least);
    if (!number.has_value()) {
        return malformed("'" + std::string(line.key()) + "' holds a malformed number", line);
    }
    return *number;
}

// Items of one kind are written in the byte order of their lines, each once.
std::expected<void, ParseError> in_order(std::string_view previous, const Line& line, std::string_view what) {
    if (!previous.empty() && !(previous < line.text)) {
        return malformed(std::string(what) + " are not in canonical order, or one is repeated", line);
    }
    return {};
}

// One entry's fields after its opening line, in the order the format fixes.
std::expected<Entry, ParseError> parse_entry(Parser& parser, const Line& opening) {
    Entry entry;
    auto symbol = text_field(opening, 1);
    if (!symbol) {
        return std::unexpected(symbol.error());
    }
    entry.symbol = std::move(*symbol);

    auto name_line = parser.take("name", 1);
    if (!name_line) {
        return std::unexpected(name_line.error());
    }
    auto name = text_field(*name_line, 1);
    if (!name) {
        return std::unexpected(name.error());
    }
    entry.name = std::move(*name);

    auto statement_line = parser.take("statement", 1);
    if (!statement_line) {
        return std::unexpected(statement_line.error());
    }
    auto statement = digest_field(*statement_line, 1);
    if (!statement) {
        return std::unexpected(statement.error());
    }
    entry.statement = *statement;

    auto contract_line = parser.take("contract", 1);
    if (!contract_line) {
        return std::unexpected(contract_line.error());
    }
    auto contract = text_field(*contract_line, 1);
    if (!contract) {
        return std::unexpected(contract.error());
    }
    entry.contract = std::move(*contract);

    // Only a contract every obligation of which the kernel accepted is ever
    // recorded, so any other status is refused rather than read as one
    // (SPEC.md TUBOUND-002).
    auto status = parser.take("status", 1);
    if (!status) {
        return std::unexpected(status.error());
    }
    if (status->fields[1] != "proven") {
        return malformed("the entry for " + quoted(entry.name) + " records status " + quoted(status->fields[1]) +
                             "; only a proven contract is recorded",
                         *status);
    }

    auto correctness = parser.take("correctness", 1);
    if (!correctness) {
        return std::unexpected(correctness.error());
    }
    if (correctness->fields[1] == "total") {
        entry.correctness = Correctness::Total;
    } else if (correctness->fields[1] == "partial") {
        entry.correctness = Correctness::Partial;
    } else {
        return malformed("'correctness' is neither 'total' nor 'partial'", *correctness);
    }

    std::string_view previous;
    while (true) {
        auto line = parser.take_if("premise", 4);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            break;
        }
        const Line& premise_line = **line;
        if (entry.premises.size() == kMaxEntryItems) {
            return malformed("an entry rests on more than " + std::to_string(kMaxEntryItems) + " premises",
                             premise_line);
        }
        if (auto ordered = in_order(previous, premise_line, "premises"); !ordered) {
            return std::unexpected(ordered.error());
        }
        previous = premise_line.text;
        auto identity = digest_field(premise_line, 1);
        auto at = number_field(premise_line, 2, 1);
        auto file = text_field(premise_line, 3);
        auto law = text_field(premise_line, 4);
        if (!identity || !at || !file || !law) {
            return std::unexpected(!identity ? identity.error()
                                   : !at     ? at.error()
                                   : !file   ? file.error()
                                             : law.error());
        }
        entry.premises.push_back(Premise{*identity, std::move(*law), std::move(*file), *at});
    }

    previous = {};
    while (true) {
        auto line = parser.take_if("unsafe", 3);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            break;
        }
        const Line& block_line = **line;
        if (entry.unsafe.size() == kMaxEntryItems) {
            return malformed("an entry rests on more than " + std::to_string(kMaxEntryItems) + " unsafe blocks",
                             block_line);
        }
        if (auto ordered = in_order(previous, block_line, "unsafe blocks"); !ordered) {
            return std::unexpected(ordered.error());
        }
        previous = block_line.text;
        auto at = number_field(block_line, 1, 1);
        auto column = number_field(block_line, 2, 0);
        auto file = text_field(block_line, 3);
        if (!at || !column || !file) {
            return std::unexpected(!at ? at.error() : !column ? column.error() : file.error());
        }
        entry.unsafe.push_back(UnsafeBlock{std::move(*file), *at, *column});
    }

    previous = {};
    while (true) {
        auto line = parser.take_if("depends", 2);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            break;
        }
        const Line& dependency_line = **line;
        if (entry.depends.size() == kMaxEntryItems) {
            return malformed("an entry rests on more than " + std::to_string(kMaxEntryItems) + " imported contracts",
                             dependency_line);
        }
        if (auto ordered = in_order(previous, dependency_line, "dependencies"); !ordered) {
            return std::unexpected(ordered.error());
        }
        previous = dependency_line.text;
        auto identity = digest_field(dependency_line, 1);
        auto callee = text_field(dependency_line, 2);
        if (!identity || !callee) {
            return std::unexpected(!identity ? identity.error() : callee.error());
        }
        entry.depends.push_back(Dependency{std::move(*callee), *identity});
    }

    if (auto end = parser.take("end", 0); !end) {
        return std::unexpected(end.error());
    }
    return entry;
}

std::expected<std::string, ParseError> single_text(Parser& parser, std::string_view key) {
    auto line = parser.take(key, 1);
    if (!line) {
        return std::unexpected(line.error());
    }
    return text_field(*line, 1);
}

} // namespace

source::Digest identify(const Entry& entry) {
    source::Hasher hasher;
    hasher.update_field("cppl-verification-interface-entry-v1");
    hasher.update(entry_text(entry));
    return hasher.finish();
}

std::expected<std::string, std::string> serialize(const Interface& recorded) {
    const Configuration& configuration = recorded.configuration;
    if (configuration.compiler.empty() || configuration.kernel.empty() || configuration.core.empty() ||
        configuration.clang.empty() || configuration.language.empty() || configuration.target.empty() ||
        recorded.unit.empty()) {
        return std::unexpected("a configuration field or the unit is empty");
    }
    if (configuration.flags.size() > kMaxFlags) {
        return std::unexpected("more than " + std::to_string(kMaxFlags) + " options change C++ meaning");
    }

    std::vector<SourceFile> sources = recorded.sources;
    std::ranges::sort(sources, {}, [](const SourceFile& file) { return encode(file.path); });
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (sources[index].path.empty()) {
            return std::unexpected("a source file has no path");
        }
        if (index > 0 && sources[index].path == sources[index - 1].path &&
            !(sources[index].digest == sources[index - 1].digest)) {
            return std::unexpected("source '" + sources[index].path + "' is recorded with two contents");
        }
    }
    const auto repeated = std::ranges::unique(sources);
    sources.erase(repeated.begin(), repeated.end());
    if (sources.size() > kMaxSources) {
        return std::unexpected("more than " + std::to_string(kMaxSources) + " source files");
    }

    std::vector<Entry> entries = recorded.entries;
    std::ranges::sort(entries, {}, [](const Entry& entry) { return encode(entry.symbol); });
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (const std::optional<std::string> problem = entry_problem(entries[index])) {
            return std::unexpected(*problem);
        }
        if (index > 0 && entries[index].symbol == entries[index - 1].symbol) {
            return std::unexpected("'" + entries[index].name + "' is recorded twice");
        }
    }
    if (entries.size() > kMaxEntries) {
        return std::unexpected("more than " + std::to_string(kMaxEntries) + " contracts");
    }

    std::string text;
    const auto line = [&text](std::string_view content) {
        text += content;
        text += '\n';
    };
    line(std::string(kMagic) + " " + std::to_string(kFormatVersion));
    line("compiler " + encode(configuration.compiler));
    line("build " + configuration.build.to_hex());
    line("kernel " + encode(configuration.kernel));
    line("core " + encode(configuration.core));
    line("clang " + encode(configuration.clang));
    line("language " + encode(configuration.language));
    line("target " + encode(configuration.target));
    for (const std::string& flag : configuration.flags) {
        if (flag.empty()) {
            return std::unexpected("an option is empty");
        }
        line("flag " + encode(flag));
    }
    line("unit " + encode(recorded.unit));
    for (const SourceFile& file : sources) {
        line("source " + file.digest.to_hex() + " " + encode(file.path));
    }
    for (const Entry& entry : entries) {
        text += entry_text(entry);
    }

    // Every line must be one a reader accepts.
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end - start > kMaxLineBytes) {
            return std::unexpected("a recorded line is longer than " + std::to_string(kMaxLineBytes) + " bytes");
        }
        start = end + 1;
    }

    const source::Digest checksum = source::hash_bytes(text);
    line("checksum " + checksum.to_hex());
    if (text.size() > kMaxBytes) {
        return std::unexpected("the interface would be larger than " + std::to_string(kMaxBytes) + " bytes");
    }
    return text;
}

std::expected<Interface, ParseError> parse(std::string_view text) {
    if (text.size() > kMaxBytes) {
        return std::unexpected(
            ParseError{"it is larger than the " + std::to_string(kMaxBytes) + " bytes an interface may have", 0});
    }

    // The format and its version come first, so an artifact of another format
    // or version is named as that rather than as a corrupt one of this.
    const std::size_t first_end = text.find('\n');
    const std::string_view first = text.substr(0, std::min(first_end, kMaxLineBytes));
    const std::string expected_first = std::string(kMagic) + " " + std::to_string(kFormatVersion);
    if (first != expected_first) {
        const std::string prefix = std::string(kMagic) + " ";
        if (first.starts_with(prefix)) {
            return std::unexpected(ParseError{"it is format version " + quoted(first.substr(prefix.size())) +
                                                  ", and this compiler reads only version " +
                                                  std::to_string(kFormatVersion),
                                              1});
        }
        return std::unexpected(ParseError{"it is not a C++L verification interface", 1});
    }
    if (first_end == std::string_view::npos || text.back() != '\n') {
        return std::unexpected(ParseError{"it is truncated: its last line is incomplete", 0});
    }

    // The checksum is the last line and covers every byte before it.
    const std::size_t before_last = text.rfind('\n', text.size() - 2);
    const std::size_t last_start = before_last == std::string_view::npos ? 0 : before_last + 1;
    if (last_start <= first_end) {
        return std::unexpected(ParseError{"it is truncated: it does not end with its checksum", 0});
    }
    const std::string_view last = text.substr(last_start, text.size() - 1 - last_start);
    constexpr std::string_view kChecksum = "checksum ";
    if (!last.starts_with(kChecksum)) {
        return std::unexpected(ParseError{"it is truncated: it does not end with its checksum", 0});
    }
    const std::optional<source::Digest> recorded_checksum = parse_digest(last.substr(kChecksum.size()));
    if (!recorded_checksum.has_value()) {
        return std::unexpected(ParseError{"its checksum line is malformed", 0});
    }
    if (!(source::hash_bytes(text.substr(0, last_start)) == *recorded_checksum)) {
        return std::unexpected(ParseError{"it is corrupt: its checksum does not match its content", 0});
    }

    Parser parser(text.substr(first_end + 1, last_start - first_end - 1), 2);
    Interface recorded;
    Configuration& configuration = recorded.configuration;

    auto compiler = single_text(parser, "compiler");
    if (!compiler) {
        return std::unexpected(compiler.error());
    }
    configuration.compiler = std::move(*compiler);
    auto build_line = parser.take("build", 1);
    if (!build_line) {
        return std::unexpected(build_line.error());
    }
    auto build = digest_field(*build_line, 1);
    if (!build) {
        return std::unexpected(build.error());
    }
    configuration.build = *build;
    for (auto [key, field] :
         std::array<std::pair<std::string_view, std::string*>, 5>{{{"kernel", &configuration.kernel},
                                                                   {"core", &configuration.core},
                                                                   {"clang", &configuration.clang},
                                                                   {"language", &configuration.language},
                                                                   {"target", &configuration.target}}}) {
        auto value = single_text(parser, key);
        if (!value) {
            return std::unexpected(value.error());
        }
        *field = std::move(*value);
    }
    while (true) {
        auto line = parser.take_if("flag", 1);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            break;
        }
        if (configuration.flags.size() == kMaxFlags) {
            return malformed("more than " + std::to_string(kMaxFlags) + " options are recorded", **line);
        }
        auto flag = text_field(**line, 1);
        if (!flag) {
            return std::unexpected(flag.error());
        }
        configuration.flags.push_back(std::move(*flag));
    }

    auto unit = single_text(parser, "unit");
    if (!unit) {
        return std::unexpected(unit.error());
    }
    recorded.unit = std::move(*unit);

    std::string_view previous;
    while (true) {
        auto line = parser.take_if("source", 2);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            break;
        }
        const Line& source_line = **line;
        if (recorded.sources.size() == kMaxSources) {
            return malformed("more than " + std::to_string(kMaxSources) + " source files are recorded", source_line);
        }
        // Ordered by path, so no path is recorded twice.
        if (!previous.empty() && !(previous < source_line.fields[2])) {
            return malformed("source files are not in canonical order, or one is repeated", source_line);
        }
        previous = source_line.fields[2];
        auto digest = digest_field(source_line, 1);
        auto path = text_field(source_line, 2);
        if (!digest || !path) {
            return std::unexpected(!digest ? digest.error() : path.error());
        }
        recorded.sources.push_back(SourceFile{std::move(*path), *digest});
    }

    previous = {};
    while (true) {
        auto line = parser.take_if("entry", 1);
        if (!line) {
            return std::unexpected(line.error());
        }
        if (!line->has_value()) {
            break;
        }
        const Line& opening = **line;
        if (recorded.entries.size() == kMaxEntries) {
            return malformed("more than " + std::to_string(kMaxEntries) + " contracts are recorded", opening);
        }
        // Ordered by symbol, so no function is recorded twice.
        if (!previous.empty() && !(previous < opening.fields[1])) {
            return malformed("entries are not in canonical order, or one function is recorded twice", opening);
        }
        previous = opening.fields[1];
        auto entry = parse_entry(parser, opening);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        recorded.entries.push_back(std::move(*entry));
    }

    if (!parser.done()) {
        auto line = parser.peek();
        if (!line) {
            return std::unexpected(line.error());
        }
        return malformed("found " + quoted(line->key()) + " where an entry or the checksum was expected", *line);
    }
    return recorded;
}

} // namespace cppl::artifact
