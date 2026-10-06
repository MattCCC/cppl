#pragma once

#include "cppl/clang/ast.hpp"
#include "places.hpp"

#include <clang-c/Index.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Whole struct values in a verified body (TRUST.md TCB-AGGREGATE-001,
// TCB-AGGREGATE-002): a struct the body tracks as one place per scalar leaf,
// read whole, copied, assigned and handed to a reference parameter. The body
// lowering in bridge.cpp calls these where such a value flows, and lends them
// what of its own state they need through `Lowering`.
namespace cppl::clangbridge::detail::aggregates {

// What of a body's signature the lowering of struct values reads: where each
// written parameter stands among the verified callable's parameters, after
// the implicit object's leaves, and the class of that object, if there is one.
struct Frame {
    const std::vector<CXCursor>* parameters = nullptr;
    std::uint32_t leaves = 0;
    CXCursor receiver = clang_getNullCursor();

    // The callable position of the written parameter `declaration` names.
    [[nodiscard]] std::optional<std::uint32_t> position_of(CXCursor declaration) const;
};

// Whether a value of `type` is one a body tracks as one place per scalar leaf: a
// record or an array, built in or `std::array`, whose members are all modeled
// (SPEC.md 12.10).
[[nodiscard]] bool structural(const Type& type);

// The path of every scalar leaf of a value of `type` below `prefix`, in
// component order, appended to `paths`. False when some member is one this
// implementation does not track.
bool leaf_paths(const Type& type, const std::vector<PlaceStep>& prefix, std::vector<std::vector<PlaceStep>>& paths);

// The member of the value `whole` at `path`: an assembled value's operand where
// `whole` is one, its projection otherwise. Nothing when a step selects no
// component of the value's type.
[[nodiscard]] std::optional<Expr> member_at(Expr whole, const std::vector<PlaceStep>& path);

// How the storage `declaration` designates at `path` is written, for a
// diagnostic.
[[nodiscard]] std::string spelled_access(CXCursor declaration, const std::vector<PlaceStep>& path);

// The value of an object a body tracks member by member, read whole where
// `cursor` stands, assembled from its leaves: the object `declaration` names
// (`object_value`), a member or an element of one that is itself a record or
// an array (`member_value`), and the implicit object or such a member of it
// (`receiver_value`). Nothing when the expression names no such object; an
// `Unsupported` expression when it names one this body cannot state whole.
[[nodiscard]] std::optional<Expr> object_value(CXCursor cursor, CXCursor declaration, const Locals& locals,
                                               const Frame& frame);
[[nodiscard]] std::optional<Expr> member_value(CXCursor cursor, const Locals& locals, const Frame& frame);
[[nodiscard]] std::optional<Expr> receiver_value(CXCursor cursor, const ResolvedAccess& access, const Locals& locals,
                                                 const Frame& frame);

// The two ways one object takes another's value: by being constructed from it,
// or by being assigned it.
enum class Copying : std::uint8_t { Construction, Assignment };

// Why copying or moving a value of `type` may run code of the program, if it
// may: a copy or move constructor or assignment operator of its class, or of
// the class of a member or an element of it at any depth, that the program
// provides.
[[nodiscard]] std::optional<std::string> user_provided_copy(CXType type, Copying copying = Copying::Construction,
                                                            unsigned depth = 0);

// The braced list an initializer is, written alone or as the operand of a
// functional cast to the very type it initializes. A null cursor otherwise.
[[nodiscard]] CXCursor braced_list(CXCursor initializer);

// The operand of `construction`, a constructor call, when the constructor copies
// or moves a value C++ defines memberwise; otherwise why it is not modeled.
[[nodiscard]] std::expected<CXCursor, std::string> copied_operand(CXCursor construction);

// The value a copy or move expression copies, through every copy or move C++
// defines memberwise that wraps it; `cursor` itself when it is no such copy.
[[nodiscard]] CXCursor copied_value(CXCursor cursor);

// Whether `initializer`, of a struct local, is a whole value of its type
// rather than an aggregate initializer: a copy, a move or a call's result.
[[nodiscard]] bool initializes_whole(CXCursor initializer);

// Whether `statement` is `a = b` between two objects of one record or array
// type, whichever assignment operator Clang selected.
[[nodiscard]] bool assigns_whole(CXCursor statement);

// A struct argument this body tracks as one place per scalar leaf: the object
// the argument designates, which one reference parameter binds, and the places
// it is tracked as.
struct ArgumentGroup {
    std::uint32_t argument = 0; // the callee's position, past its implicit object's leaves
    CXCursor declaration = clang_getNullCursor();
    std::vector<PlaceStep> prefix;   // where the object lies within its declaration's storage
    std::vector<std::size_t> leaves; // its leaf places, each reached by constant steps
    std::vector<std::size_t> others; // element places inside it formed at a term
    Type type;                       // the struct's, as the parameter is declared at
    bool writable = false;           // bound by a reference the callee may write through
};

// The group of places a struct expression designates, when this body tracks
// that object member by member and it is bound as a `bound` as it is. Nothing
// when it designates no such object.
[[nodiscard]] std::optional<ArgumentGroup> argument_group(CXCursor argument, CXType bound, const Locals& state,
                                                          const Frame& frame);

// Adds to `written_storage` every place of each group the callee may write.
void reach_written(const std::vector<ArgumentGroup>& groups, bool unsafe_callee,
                   std::vector<std::size_t>& written_storage);

// What follows a write or a declaration, lowered in the state it leaves.
using Rest = std::function<std::optional<Expr>(const Locals&)>;

// A leaf of a struct type, as a local of that type is tracked.
struct TypeLeaf {
    std::vector<PlaceStep> path;
    Type type; // with the refinement the member declared
    std::string spelling;
};

// What the lowering of struct values asks of the body lowering it is part of:
// each is that lowering's own operation (bridge.cpp, `BodyLowering`).
class Lowering {
  public:
    Lowering() = default;
    Lowering(const Lowering&) = delete;
    Lowering& operator=(const Lowering&) = delete;
    Lowering(Lowering&&) = delete;
    Lowering& operator=(Lowering&&) = delete;

    virtual std::uint32_t fresh_version() = 0;
    // The version holds its place's refinement (SPEC.md REFINE-060).
    virtual void establish(std::uint32_t version) = 0;
    // The leaf at `version` takes `value`, its member of a post-state value.
    virtual void rebind(std::uint32_t version, Expr value) = 0;
    virtual std::nullopt_t reject(std::string reason) = 0;
    virtual std::optional<Expr> evaluate(CXCursor cursor, Locals& state, std::vector<std::size_t>& invalidated) = 0;
    virtual std::optional<std::size_t> written_local(CXCursor target, Locals& state) = 0;
    virtual Expr bind(std::uint32_t version, Place place, Expr value, Expr body, CXCursor at, Type declared) = 0;
    virtual Expr unknown(const Locals& state, std::size_t entry, Expr body, CXCursor at) = 0;
    virtual std::optional<Expr> write_then(std::size_t local, Expr value, CXCursor statement, const Locals& state,
                                           const Rest& rest) = 0;
    virtual std::optional<std::string> type_leaves(const Type& type, const std::string& name,
                                                   std::vector<TypeLeaf>& leaves) = 0;

  protected:
    ~Lowering() = default;
};

// A call's struct arguments, and what the rest of its lowering decided: the
// places it gave post-call versions and whether a place may be one it writes.
struct GroupCall {
    CXCursor cursor = clang_getNullCursor();
    CXCursor callee = clang_getNullCursor();
    bool unsafe_callee = false;
    const std::vector<ArgumentGroup>* groups = nullptr;
    const std::vector<std::size_t>* targets = nullptr;
    std::function<bool(std::size_t)> reached_by_a_write;
};

// The post-state each struct argument of a call takes, appended to `effects`,
// and its leaves rebound to their members of it. False when the call is
// refused, through `body`.
bool post_states(Lowering& body, const GroupCall& call, Locals& state, std::vector<std::size_t>& invalidated,
                 std::vector<CallEffect>& effects);

// `a = b` between two values of a struct type whose assignment C++ defines
// memberwise, then `rest`.
std::optional<Expr> lower_whole_assignment(Lowering& body, CXCursor statement, const Locals& locals, const Frame& frame,
                                           const Rest& rest);

// A struct local `name` declared by `declaration`, initialized from a whole
// value of its type, then `rest` with its leaves tracked.
std::optional<Expr> lower_initialization(Lowering& body, CXCursor declaration, const std::string& name,
                                         const Type& type, CXCursor initializer, const Locals& locals,
                                         const Rest& rest);

} // namespace cppl::clangbridge::detail::aggregates
