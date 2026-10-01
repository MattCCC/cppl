// Every row of translation_edges.tsv: the facts and goal of an arithmetic step,
// translated by the kernel, give the system the row states, constraint for
// constraint and disjunction for disjunction, in the kernel's order.
// tools/formal/check.sh proves the same rows of the Coq model's translation
// (formal/coq/Linear.v), so the two are checked against one table
// (docs/KERNEL.md 17).
//
// A failing row names the system the kernel built, so a new row can be written
// with any system and corrected from the failure.

#include "cppl/kernel/context.hpp"
#include "cppl/kernel/linear.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <span>
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

std::string text_of(k::Wide value) {
    if (value == 0) {
        return "0";
    }
    const bool negative = value < 0;
    k::WideUnsigned magnitude =
        negative ? k::WideUnsigned{0} - static_cast<k::WideUnsigned>(value) : static_cast<k::WideUnsigned>(value);
    std::string digits;
    while (magnitude != 0) {
        digits.insert(digits.begin(), static_cast<char>('0' + static_cast<int>(magnitude % 10u)));
        magnitude /= 10u;
    }
    return negative ? "-" + digits : digits;
}

std::vector<std::string> fields(const std::string& line, char separator) {
    std::vector<std::string> found;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, separator)) {
        const std::size_t first = field.find_first_not_of(' ');
        if (first == std::string::npos) {
            continue;
        }
        found.push_back(field.substr(first, field.find_last_not_of(' ') - first + 1));
    }
    return found;
}

// A term in prefix notation: vN, type:value, or a primitive's name, its type
// and its operands.
class Reader {
  public:
    explicit Reader(const std::string& text) : tokens_(fields(text, ' ')) {}

    k::Term term() {
        const std::string token = next();
        if (token.front() == 'v') {
            return k::Term::variable(k::VarIndex{static_cast<std::uint32_t>(std::stoul(token.substr(1)))});
        }
        if (const std::size_t colon = token.find(':'); colon != std::string::npos) {
            return k::Term::literal(type_of(token.substr(0, colon)), value_of(token.substr(colon + 1)));
        }
        const auto primitive = kPrimitives.find(token);
        CPPL_CHECK(primitive != kPrimitives.end());
        const k::IntType type = type_of(next());
        const std::size_t arity = token == "Not" || token == "Convert" ? 1u : token == "Select" ? 3u : 2u;
        std::vector<k::Term> operands;
        operands.reserve(arity);
        for (std::size_t index = 0; index < arity; ++index) {
            operands.push_back(term());
        }
        return k::Term::primitive(primitive->second, type, std::move(operands));
    }

    std::string next() {
        CPPL_CHECK(position_ < tokens_.size());
        return tokens_[position_++];
    }

    [[nodiscard]] bool done() const {
        return position_ == tokens_.size();
    }

  private:
    std::vector<std::string> tokens_;
    std::size_t position_ = 0;
};

k::Proposition proposition_of(const std::string& text) {
    if (text == "False") {
        return k::Proposition::falsity();
    }
    Reader reader(text);
    const k::IntType type = type_of(reader.next());
    k::Term lhs = reader.term();
    k::Term rhs = reader.term();
    CPPL_CHECK(reader.done());
    return k::Proposition::equality(k::Type{type}, std::move(lhs), std::move(rhs));
}

std::string render(const k::LinearConstraint& constraint) {
    std::string out;
    for (const auto& [variable, coefficient] : constraint.terms) {
        out += std::to_string(variable) + ":" + text_of(coefficient) + " ";
    }
    return out + "=" + text_of(constraint.constant);
}

// The number of variables, every constraint and every disjunction, in order.
std::string render(const k::ArithmeticSystem& system) {
    std::string out = std::to_string(system.variables.size()) + " |";
    for (std::size_t index = 0; index < system.constraints.size(); ++index) {
        out += (index == 0 ? " " : " ; ") + render(system.constraints[index]);
    }
    out += system.constraints.empty() ? " - |" : " |";
    for (std::size_t index = 0; index < system.disjunctions.size(); ++index) {
        out += (index == 0 ? " " : " ; ") + render(system.disjunctions[index][0]) + " or " +
               render(system.disjunctions[index][1]);
    }
    return out + (system.disjunctions.empty() ? " -" : "");
}

std::string squeeze(const std::string& text) {
    std::string out;
    for (const std::string& word : fields(text, ' ')) {
        out += (out.empty() ? "" : " ") + word;
    }
    return out;
}

} // namespace

CPPL_TEST(every_translation_row_builds_the_system_it_states) {
    std::ifstream table(CPPL_TRANSLATION_EDGES);
    CPPL_CHECK(table.good());
    std::size_t rows = 0;
    std::string line;
    while (std::getline(table, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string> row = fields(line, '\t');
        CPPL_CHECK_EQ(row.size(), std::size_t{4});
        std::vector<k::Type> locals;
        if (row[0] != "-") {
            for (const std::string& name : fields(row[0], ' ')) {
                locals.push_back(k::Type{type_of(name)});
            }
        }
        std::vector<k::Proposition> facts;
        if (row[1] != "-") {
            for (const std::string& fact : fields(row[1], ';')) {
                facts.push_back(proposition_of(fact));
            }
        }
        const auto system =
            k::arithmetic_system({}, facts, proposition_of(row[2]), k::CoreLimits{}, std::span<const k::Type>(locals));
        const std::string built = system.has_value() ? render(*system) : "refused";
        if (built != squeeze(row[3])) {
            std::string message = "the kernel translates '";
            message += line;
            message += "' to '";
            message += built;
            message += "', not the system it states";
            ::cppl::testing::fail(__FILE__, __LINE__, message);
        }
        ++rows;
    }
    CPPL_CHECK(rows >= 80u);
}
