// Ordinary C++: `providers.cpp` erased by hand as SPEC.md Annex M says it
// erases. Every proof and Law is gone, whole; every type, alias and function the
// program runs stays as written.
#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

// Alternatives are named by index, so repeated and aliased types stay distinct.
using Repeated = std::variant<int, int, bool>;
using Aliased = std::variant<int, bool>;
using AliasOfAliased = Aliased;

template <typename T> using Wrapped = std::optional<T>;

struct Point {
    int x;
    int y;
};

struct Nested {
    Point origin;
    bool flagged;
};

// 18/21. A dependent form resolves by canonical identity after substitution,
// and cv-qualification does not change the state space.
template <typename T> using Sum = std::variant<T, bool>;

// 21. A binding denotes the existing subobject. A type that cannot be copied,
// moved or default-constructed still decomposes: if a binder introduced any of
// those operations, this would not compile.
struct Pinned {
    int v;
    Pinned() = delete;
    Pinned(const Pinned&) = delete;
    Pinned(Pinned&&) = delete;
    Pinned& operator=(const Pinned&) = delete;
    Pinned& operator=(Pinned&&) = delete;
};

struct Holder {
    Pinned pinned;
    bool flagged;
};

// 32. A subject is a value read from storage, never the storage itself
// (SPEC.md CASE-008). These pin the read paths other than a bare parameter, so
// the one-version property is not true merely because every subject above
// happens to be an identifier. Each denotes the value that storage holds at
// this step; a later write would create a new version through the ordinary
// storage/effect model (SPEC.md 12.10) rather than changing this one.
struct HasOptional {
    std::optional<int> maybe;
    int other;
};

// SPEC: CASE-007
// Decomposing needs the subject's type complete, as a member access would. A
// class template specialization reached only through a reference is never
// instantiated by C++ on its own, so nothing else in this file names
// `Crate<Cargo>` or `Crate<long>` by value. The refused half of this matched
// pair, a template that is declared but never defined, is
// `fixtures/negative/decompose_undefined_template.cpp`.
template <typename T> struct Crate {
    T item;
    bool sealed;
};

struct Cargo {
    int weight;
};

// 33/35. Each binder below is handed to a lemma that accepts exactly one type,
// so a binder bound to the wrong component, alternative or payload does not
// type-check; nothing is converted implicitly between these three records. The
// refused halves of these matched pairs are in `fixtures/negative/`, driven by
// `negative/case_providers.sh`.
struct Left {
    int l;
};

struct Right {
    bool r;
};

struct Other {
    unsigned o;
};

// 33. Sums inside a record, reached by decomposing the record first.
struct Tagged {
    std::variant<Left, Right> choice;
    std::optional<Other> extra;
};

// 35. Pointers: a const pointer, a reference to a pointer and an alias all have
// the same two states as the pointer they denote.
using LeftPointer = const Left*;

// 35. Products: a class with public members, move-only fields, a class template
// and an alias of one. None needs a copy, since a binder is a projection.
class Open {
  public:
    Left first;
    Right second;
};

struct MoveOnly {
    Left inner;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&) = delete;
};

struct HoldsMoveOnly {
    MoveOnly owned;
    std::unique_ptr<int> unique;
    Right flag;
};

template <typename T> struct Box {
    T item;
    Right tag;
};

using LeftBox = Box<Left>;

// None of this may reach the runtime, and no record changes layout.
int main() {
    Tagged tagged{Right{true}, Other{3u}};
    std::printf("%zu %zu %zu %zu %zu %zu %zu\n", sizeof(Point), sizeof(Nested), sizeof(Holder), sizeof(Tagged),
                sizeof(HoldsMoveOnly), sizeof(Box<Left>), tagged.choice.index());
}
