#include "library.hpp"

#include "cppl/kernel/proposition.hpp"
#include "cppl/kernel/term.hpp"
#include "cppl/kernel/types.hpp"
#include "cppl/obligations/contracts.hpp"
#include "cppl/source/location.hpp"
#include "cppl/source/representation.hpp"
#include "cppl/vir/expr.hpp"
#include "lowering.hpp"

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cppl::obligations::detail {

namespace {

using Op = source::LibraryOperation;

// The one observation a modeled sequence's value has, its length, and the
// unsigned type it is stated at (RFC 0020 §2). Anything else is not a
// sequence's value, and a summary is never stated over it.
std::optional<kernel::IntType> length_type(const kernel::Type& type) {
    const auto* value = std::get_if<kernel::ValueType>(&type.node);
    if (value == nullptr || value->projections.size() != 1 || !value->projections.front().is_integer()) {
        return std::nullopt;
    }
    const kernel::IntType integer = value->projections.front().integer_type();
    if (integer.signedness != kernel::Signedness::Unsigned) {
        return std::nullopt;
    }
    return integer;
}

// The statements of one summary, over `binders` variables: the parameters in
// order, then, for a postcondition, the result.
class SummaryTerms {
  public:
    SummaryTerms(const std::vector<kernel::Type>& parameters, std::size_t binders)
        : parameters_(parameters),
          binders_(binders) {}

    [[nodiscard]] kernel::Term variable(std::size_t position) const {
        return kernel::Term::variable(kernel::parameter_reference(binders_, position));
    }

    // The length of the sequence at `position`, whose type is `domain`.
    [[nodiscard]] kernel::Term length(std::size_t position, const kernel::Type& domain) const {
        return kernel::Term::project(domain, 0, variable(position));
    }

    [[nodiscard]] kernel::Term length(std::size_t position) const {
        return length(position, parameters_[position]);
    }

    [[nodiscard]] static kernel::Proposition compare(kernel::PrimOp op, const kernel::IntType& type, kernel::Term lhs,
                                                     kernel::Term rhs) {
        return kernel::predicate(kernel::Term::primitive(op, type, {std::move(lhs), std::move(rhs)}), true);
    }

  private:
    const std::vector<kernel::Type>& parameters_;
    std::size_t binders_;
};

kernel::Proposition both(kernel::Proposition left, kernel::Proposition right) {
    return kernel::Proposition::conjunction(std::move(left), std::move(right));
}

} // namespace

std::expected<LibrarySummary, Failure> library_summary(const vir::Call& call, const vir::Expr& site) {
    const source::SourceLocation& location = site.provenance.range.begin;
    const auto refuse = [&](const std::string& reason) {
        return std::unexpected(
            Failure{"the library operation '" + call.callee_name + "' is not modeled: " + reason, location, {}});
    };
    if (!call.library.has_value()) {
        return refuse("it is not a library call");
    }
    LibrarySummary summary;
    summary.symbol = call.callee.usr;
    summary.name = call.callee_name;
    summary.library = *call.library;
    for (const vir::Expr& argument : call.arguments) {
        const std::optional<kernel::Type> type = core_type(argument.type);
        if (!type.has_value()) {
            return refuse("an argument has a type the formal core does not represent");
        }
        summary.parameters.push_back(*type);
    }
    const std::optional<kernel::Type> result = core_type(site.type);
    if (!result.has_value()) {
        return refuse("its result has a type the formal core does not represent");
    }
    summary.result = *result;

    const std::size_t count = summary.parameters.size();
    const SummaryTerms pre(summary.parameters, count);
    const SummaryTerms post(summary.parameters, count + 1);
    // Every sequence a summary mentions is stated at one length type, the one
    // the first sequence argument's value has.
    const auto sequences = [&](std::initializer_list<std::size_t> positions) -> std::optional<kernel::IntType> {
        std::optional<kernel::IntType> shared;
        for (const std::size_t position : positions) {
            if (position >= count) {
                return std::nullopt;
            }
            const std::optional<kernel::IntType> type = length_type(summary.parameters[position]);
            if (!type.has_value() || (shared.has_value() && !(*shared == *type))) {
                return std::nullopt;
            }
            shared = type;
        }
        return shared;
    };
    const auto same = [&](std::size_t lhs, std::size_t rhs) {
        return lhs < count && rhs < count && summary.parameters[lhs] == summary.parameters[rhs];
    };
    // The result's length, stated with the result's own type as its domain.
    const auto result_length = [&]() {
        return kernel::Term::project(summary.result, 0, post.variable(count));
    };

    switch (call.library->operation) {
        // A sequence of a stated length: default (0), a list (its element
        // count), a count constructor, a string literal (its length up to the
        // first null character).
        case Op::Construct: {
            const std::optional<kernel::IntType> length = length_type(summary.result);
            if (count != 1 || !length.has_value() || !summary.parameters[0].is_integer() ||
                !(summary.parameters[0].integer_type() == *length)) {
                return refuse("a construction states one length of the sequence's size type");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, result_length(), post.variable(0));
            break;
        }
        // A copy has the source's length.
        case Op::Copy: {
            const std::optional<kernel::IntType> length = sequences({0});
            if (count != 1 || !length.has_value() || !(summary.result == summary.parameters[0])) {
                return refuse("a copy takes one sequence of its own type");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, result_length(), post.length(0));
            break;
        }
        // A move takes the source's storage and length; the source, written,
        // is left in a valid but unspecified state, of which nothing is stated.
        case Op::Move: {
            const std::optional<kernel::IntType> length = sequences({0, 1});
            if (count != 2 || !length.has_value() || !same(0, 1) || !(summary.result == summary.parameters[0])) {
                return refuse("a move takes the sequence moved from, as written and as it was");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, result_length(), post.length(1));
            break;
        }
        // A span of a whole sequence views every element it has.
        case Op::ViewOf: {
            const std::optional<kernel::IntType> length = sequences({0});
            const std::optional<kernel::IntType> viewed = length_type(summary.result);
            if (count != 1 || !length.has_value() || !viewed.has_value() || !(*viewed == *length)) {
                return refuse("a span is formed over one sequence of its size type");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, result_length(), post.length(0));
            break;
        }
        // A data pointer states nothing of its own: what it permits is a
        // capability over the sequence's length, owed where it is passed.
        case Op::DataOf: {
            if (count != 1 || !sequences({0}).has_value()) {
                return refuse("a data pointer is taken of one sequence");
            }
            break;
        }
        // One more element. A successful push never wraps the length: it stays
        // below `max_size()`, which is below the largest `size_type`, and
        // exceeding it throws, which is no normal return.
        case Op::PushBack: {
            const std::optional<kernel::IntType> length = sequences({0, 1});
            if (count != 2 || !length.has_value() || !same(0, 1)) {
                return refuse("a push states the sequence as written and as it was");
            }
            summary.postcondition = both(
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, post.length(0),
                                      kernel::Term::primitive(kernel::PrimOp::AddWrap, *length,
                                                              {post.length(1), kernel::Term::literal(*length, 1)})),
                SummaryTerms::compare(kernel::PrimOp::Less, *length, post.length(1), post.length(0)));
            break;
        }
        // One fewer, of a sequence that is not empty: the precondition is owed
        // like any callee's.
        case Op::PopBack: {
            const std::optional<kernel::IntType> length = sequences({0, 1});
            if (count != 2 || !length.has_value() || !same(0, 1)) {
                return refuse("a pop states the sequence as written and as it was");
            }
            summary.preconditions.push_back(SummaryTerms::compare(kernel::PrimOp::NotEqual, *length, pre.length(1),
                                                                  kernel::Term::literal(*length, 0)));
            summary.postcondition = both(
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, post.length(0),
                                      kernel::Term::primitive(kernel::PrimOp::SubWrap, *length,
                                                              {post.length(1), kernel::Term::literal(*length, 1)})),
                SummaryTerms::compare(kernel::PrimOp::Less, *length, post.length(0), post.length(1)));
            break;
        }
        case Op::Clear: {
            const std::optional<kernel::IntType> length = sequences({0, 1});
            if (count != 2 || !length.has_value() || !same(0, 1)) {
                return refuse("a clear states the sequence as written and as it was");
            }
            summary.postcondition = SummaryTerms::compare(kernel::PrimOp::Equal, *length, post.length(0),
                                                          kernel::Term::literal(*length, 0));
            break;
        }
        // The same length; the storage may have been replaced, which the
        // caller's generation already records.
        case Op::Reserve: {
            const std::optional<kernel::IntType> length = sequences({0, 1});
            if (count != 3 || !length.has_value() || !same(0, 1) || !summary.parameters[2].is_integer() ||
                !(summary.parameters[2].integer_type() == *length)) {
                return refuse("a reserve states the sequence as written and as it was, and a count");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, post.length(0), post.length(1));
            break;
        }
        // Another string's characters after its own. The sum does not wrap for
        // the same reason a push does not, so both lengths are below it.
        case Op::Append: {
            const std::optional<kernel::IntType> length = sequences({0, 1, 2});
            if (count != 3 || !length.has_value() || !same(0, 1) || !same(1, 2)) {
                return refuse("an append states the string as written and as it was, and the string appended");
            }
            summary.postcondition =
                both(SummaryTerms::compare(
                         kernel::PrimOp::Equal, *length, post.length(0),
                         kernel::Term::primitive(kernel::PrimOp::AddWrap, *length, {post.length(1), post.length(2)})),
                     both(SummaryTerms::compare(kernel::PrimOp::LessEqual, *length, post.length(1), post.length(0)),
                          SummaryTerms::compare(kernel::PrimOp::LessEqual, *length, post.length(2), post.length(0))));
            break;
        }
        case Op::Assign: {
            const std::optional<kernel::IntType> length = sequences({0, 1, 2});
            if (count != 3 || !length.has_value() || !same(0, 1) || !same(1, 2)) {
                return refuse("an assignment states the sequence as written and as it was, and its source");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, post.length(0), post.length(2));
            break;
        }
        // The source's storage and length; the source is then unspecified.
        case Op::MoveAssign: {
            const std::optional<kernel::IntType> length = sequences({0, 1, 2, 3});
            if (count != 4 || !length.has_value() || !same(0, 1) || !same(1, 2) || !same(2, 3)) {
                return refuse("a move assignment states both sequences as written and as they were");
            }
            summary.postcondition =
                SummaryTerms::compare(kernel::PrimOp::Equal, *length, post.length(0), post.length(3));
            break;
        }
    }
    return summary;
}

} // namespace cppl::obligations::detail
