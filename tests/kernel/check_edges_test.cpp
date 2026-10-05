// Every row of check_edges.tsv: the kernel accepts or refuses the evidence for
// the proposition exactly as the row states. tools/formal/check.sh proves the
// same verdict of the Coq model's checker (formal/coq/Checker.v, with the
// normalizer of Normalize.v and the arithmetic of Linear.v), so the two are
// checked against one table of every rule's acceptances and near misses
// (docs/KERNEL.md 17).
//
// Run with CPPL_CHECK_EDGES_PRINT=1 to print the kernel's verdict and, for a
// refusal, its reason, for each row.

#include "cppl/kernel/check.hpp"
#include "cppl/kernel/proof.hpp"
#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/testing/test.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
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

k::IntType int_type(const std::string& name) {
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

// Propositions, terms, evidence and certificates in prefix notation
// (check_edges.tsv states the grammar).
class Reader {
  public:
    explicit Reader(const std::string& text) : tokens_(fields(text, ' ')) {}

    std::string next() {
        CPPL_CHECK(position_ < tokens_.size());
        return tokens_[position_++];
    }

    std::uint32_t index() {
        return static_cast<std::uint32_t>(std::stoul(next()));
    }

    k::Type type() {
        return k::Type{int_type(next())};
    }

    k::Term term() {
        const std::string token = next();
        if (token.front() == 'v') {
            return k::Term::variable(k::VarIndex{static_cast<std::uint32_t>(std::stoul(token.substr(1)))});
        }
        if (const std::size_t colon = token.find(':'); colon != std::string::npos) {
            return k::Term::literal(int_type(token.substr(0, colon)), value_of(token.substr(colon + 1)));
        }
        const auto primitive = kPrimitives.find(token);
        CPPL_CHECK(primitive != kPrimitives.end());
        const k::IntType type = int_type(next());
        const std::size_t arity = token == "Not" || token == "Convert" ? 1u : token == "Select" ? 3u : 2u;
        std::vector<k::Term> operands;
        for (std::size_t operand = 0; operand < arity; ++operand) {
            operands.push_back(term());
        }
        return k::Term::primitive(primitive->second, type, std::move(operands));
    }

    k::Proposition proposition() {
        const std::string token = next();
        if (token == "eq") {
            k::Type type = this->type();
            k::Term lhs = term();
            return k::Proposition::equality(std::move(type), std::move(lhs), term());
        }
        if (token == "all") {
            k::Type binder = type();
            return k::Proposition::for_all(std::move(binder), proposition());
        }
        if (token == "imp" || token == "and" || token == "or") {
            k::Proposition left = proposition();
            k::Proposition right = proposition();
            return token == "imp"   ? k::Proposition::implication(std::move(left), std::move(right))
                   : token == "and" ? k::Proposition::conjunction(std::move(left), std::move(right))
                                    : k::Proposition::disjunction(std::move(left), std::move(right));
        }
        CPPL_CHECK(token == "false");
        return k::Proposition::falsity();
    }

    bool side() {
        const std::string token = next();
        CPPL_CHECK(token == "left" || token == "right");
        return token == "right";
    }

    k::ArithmeticCertificate certificate() {
        const std::string token = next();
        if (token == "farkas") {
            const std::uint32_t count = index();
            std::vector<std::pair<std::uint32_t, k::Wide>> multipliers;
            for (std::uint32_t entry = 0; entry < count; ++entry) {
                const std::vector<std::string> pair = fields(next(), ':');
                multipliers.emplace_back(static_cast<std::uint32_t>(std::stoul(pair[0])), value_of(pair[1]));
            }
            return k::ArithmeticCertificate{k::FarkasSum{std::move(multipliers)}};
        }
        if (token == "split") {
            const std::uint32_t count = index();
            std::vector<std::pair<std::uint32_t, std::int64_t>> terms;
            for (std::uint32_t entry = 0; entry < count; ++entry) {
                const std::vector<std::string> pair = fields(next(), ':');
                terms.emplace_back(static_cast<std::uint32_t>(std::stoul(pair[0])),
                                   static_cast<std::int64_t>(value_of(pair[1])));
            }
            const auto constant = static_cast<std::int64_t>(value_of(next()));
            k::ArithmeticCertificate at_most_zero = certificate();
            k::ArithmeticCertificate at_least_one = certificate();
            return k::ArithmeticCertificate{k::IntegerSplit{std::move(terms), constant,
                                                            k::Box<k::ArithmeticCertificate>{std::move(at_most_zero)},
                                                            k::Box<k::ArithmeticCertificate>{std::move(at_least_one)}}};
        }
        CPPL_CHECK(token == "cases");
        const std::uint32_t disjunction = index();
        k::ArithmeticCertificate first = certificate();
        k::ArithmeticCertificate second = certificate();
        return k::ArithmeticCertificate{k::DisjunctionCases{disjunction,
                                                            k::Box<k::ArithmeticCertificate>{std::move(first)},
                                                            k::Box<k::ArithmeticCertificate>{std::move(second)}}};
    }

    k::ProofTerm evidence() {
        const std::string token = next();
        if (token == "refl") {
            return k::ProofTerm::reflexivity();
        }
        if (token == "alli") {
            k::Type binder = type();
            return k::ProofTerm::forall_introduction(std::move(binder), evidence());
        }
        if (token == "alle") {
            k::Proposition quantified = proposition();
            k::ProofTerm inner = evidence();
            return k::ProofTerm::forall_elimination(std::move(quantified), std::move(inner), term());
        }
        if (token == "hyp") {
            return k::ProofTerm::hypothesis(k::HypothesisIndex{index()});
        }
        if (token == "impi") {
            k::Proposition premise = proposition();
            return k::ProofTerm::implication_introduction(std::move(premise), evidence());
        }
        if (token == "impe") {
            k::Proposition implication = proposition();
            k::ProofTerm inner = evidence();
            return k::ProofTerm::implication_elimination(std::move(implication), std::move(inner), evidence());
        }
        if (token == "eqe") {
            k::Type type = this->type();
            k::Term lhs = term();
            k::Term rhs = term();
            k::Proposition motive = proposition();
            k::ProofTerm equality = evidence();
            return k::ProofTerm::equality_elimination(std::move(type), std::move(lhs), std::move(rhs),
                                                      std::move(motive), std::move(equality), evidence());
        }
        if (token == "cond") {
            k::Type type = this->type();
            k::Term condition = term();
            k::Term when_true = term();
            k::Term when_false = term();
            k::Proposition motive = proposition();
            k::ProofTerm true_case = evidence();
            return k::ProofTerm::conditional_elimination(std::move(type), std::move(condition), std::move(when_true),
                                                         std::move(when_false), std::move(motive), std::move(true_case),
                                                         evidence());
        }
        if (token == "lin") {
            const std::uint32_t count = index();
            std::vector<k::ArithmeticFact> facts;
            for (std::uint32_t entry = 0; entry < count; ++entry) {
                k::Proposition fact = proposition();
                facts.push_back(k::ArithmeticFact{std::move(fact), k::Box<k::ProofTerm>{evidence()}});
            }
            return k::ProofTerm::linear_arithmetic(std::move(facts), certificate());
        }
        if (token == "andi") {
            k::ProofTerm left = evidence();
            return k::ProofTerm::conjunction_introduction(std::move(left), evidence());
        }
        if (token == "ande") {
            k::Proposition conjunction = proposition();
            k::ProofTerm inner = evidence();
            return k::ProofTerm::conjunction_elimination(std::move(conjunction), std::move(inner), side());
        }
        if (token == "ori") {
            k::ProofTerm inner = evidence();
            return k::ProofTerm::disjunction_introduction(std::move(inner), side());
        }
        if (token == "ore") {
            k::Proposition disjunction = proposition();
            k::ProofTerm inner = evidence();
            k::ProofTerm left_case = evidence();
            return k::ProofTerm::disjunction_elimination(std::move(disjunction), std::move(inner), std::move(left_case),
                                                         evidence());
        }
        if (token == "falsee") {
            return k::ProofTerm::falsity_elimination(evidence());
        }
        CPPL_CHECK(token == "ind");
        k::Type binder = type();
        k::ProofTerm base = evidence();
        return k::ProofTerm::unsigned_induction(std::move(binder), std::move(base), evidence());
    }

    [[nodiscard]] bool done() const {
        return position_ == tokens_.size();
    }

  private:
    std::vector<std::string> tokens_;
    std::size_t position_ = 0;
};

} // namespace

CPPL_TEST(every_check_row_gets_the_verdict_it_states) {
    std::ifstream table(CPPL_CHECK_EDGES);
    CPPL_CHECK(table.good());
    const bool print = std::getenv("CPPL_CHECK_EDGES_PRINT") != nullptr;
    std::size_t rows = 0;
    std::size_t accepted = 0;
    std::string line;
    while (std::getline(table, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string> row = fields(line, '\t');
        CPPL_CHECK_EQ(row.size(), std::size_t{3});
        Reader goal_reader(row[0]);
        const k::Proposition goal = goal_reader.proposition();
        CPPL_CHECK(goal_reader.done());
        Reader evidence_reader(row[1]);
        const k::ProofTerm evidence = evidence_reader.evidence();
        CPPL_CHECK(evidence_reader.done());
        const auto verdict = k::check({}, goal, evidence, k::CoreLimits{});
        const std::string stated = verdict.has_value() ? "accept" : "refuse";
        if (print) {
            std::cout << rows + 1 << '\t' << stated << '\t' << row[2]
                      << (verdict.has_value() ? std::string{} : "\t" + verdict.error().detail) << '\n';
        } else if (stated != row[2]) {
            ::cppl::testing::fail(__FILE__, __LINE__, "the kernel's verdict on '" + line + "' is " + stated);
        }
        accepted += verdict.has_value() ? 1u : 0u;
        ++rows;
    }
    CPPL_CHECK(rows >= 60u);
    CPPL_CHECK(accepted >= 25u);
}
