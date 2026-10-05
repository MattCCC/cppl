// Every row of normalization_edges.tsv: a primitive applied to literals at the
// edges of its type, or to a variable, normalizes, in the kernel, to the term
// the row states.
// tools/formal/check.sh proves the same rows of the Coq model's normalizer, so
// the two are checked against one table (docs/KERNEL.md 17).

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace k = cppl::kernel;

const std::map<std::string, k::PrimOp> kPrimitives = {
    {"AddWrap", k::PrimOp::AddWrap},
    {"SubWrap", k::PrimOp::SubWrap},
    {"MulWrap", k::PrimOp::MulWrap},
    {"Equal", k::PrimOp::Equal},
    {"NotEqual", k::PrimOp::NotEqual},
    {"Less", k::PrimOp::Less},
    {"LessEqual", k::PrimOp::LessEqual},
    {"Greater", k::PrimOp::Greater},
    {"GreaterEqual", k::PrimOp::GreaterEqual},
    {"Not", k::PrimOp::Not},
    {"Select", k::PrimOp::Select},
    {"AddFits", k::PrimOp::AddFits},
    {"SubFits", k::PrimOp::SubFits},
    {"MulFits", k::PrimOp::MulFits},
    {"Quotient", k::PrimOp::Quotient},
    {"Remainder", k::PrimOp::Remainder},
    {"Convert", k::PrimOp::Convert},
};

k::IntType type_of(const std::string& name) {
    const auto width = static_cast<std::uint16_t>(std::stoul(name.substr(1)));
    return k::IntType{width, name.front() == 'i' ? k::Signedness::Signed : k::Signedness::Unsigned};
}

k::Wide value_of(const std::string& text) {
    const bool negative = text.front() == '-';
    k::WideUnsigned magnitude = 0;
    for (std::size_t index = negative ? 1 : 0; index < text.size(); ++index) {
        magnitude = magnitude * 10u + static_cast<unsigned>(text[index] - '0');
    }
    return negative ? -static_cast<k::Wide>(magnitude) : static_cast<k::Wide>(magnitude);
}

// A literal written "type:value", or a variable written "vN:type".
k::Term literal_of(const std::string& written) {
    const std::size_t colon = written.find(':');
    if (written.front() == 'v') {
        return k::Term::variable(k::VarIndex{static_cast<std::uint32_t>(std::stoul(written.substr(1, colon - 1)))});
    }
    return k::Term::literal(type_of(written.substr(0, colon)), value_of(written.substr(colon + 1)));
}

std::vector<std::string> fields(const std::string& line, char separator) {
    std::vector<std::string> found;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, separator)) {
        if (!field.empty()) {
            found.push_back(field);
        }
    }
    return found;
}

} // namespace

CPPL_TEST(every_edge_row_normalizes_to_the_term_it_states) {
    std::ifstream table(CPPL_NORMALIZATION_EDGES);
    CPPL_CHECK(table.good());
    std::size_t rows = 0;
    std::string line;
    while (std::getline(table, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string> row = fields(line, '\t');
        CPPL_CHECK_EQ(row.size(), std::size_t{4});
        const auto primitive = kPrimitives.find(row[0]);
        CPPL_CHECK(primitive != kPrimitives.end());
        std::vector<k::Term> operands;
        for (const std::string& operand : fields(row[2], ' ')) {
            operands.push_back(literal_of(operand));
        }
        const k::Term term = k::Term::primitive(primitive->second, type_of(row[1]), std::move(operands));
        const auto normalized = k::normalize({}, term, {});
        if (!normalized.has_value() || !(*normalized == literal_of(row[3]))) {
            ::cppl::testing::fail(__FILE__, __LINE__,
                                  "the kernel does not normalize '" + line + "' to the term it states");
        }
        ++rows;
    }
    CPPL_CHECK(rows >= 140u);
}
