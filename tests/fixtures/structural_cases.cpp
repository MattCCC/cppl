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

// 3. std::variant. Every alternative is an index; `valueless` is never omitted.
proof variant_alternatives(Repeated v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(first) => {
            refl;
        }

        alternative<1>(second) => {
            refl;
        }

        alternative<2>(third) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}

// An alias denotes the same type, so it decomposes identically.
proof variant_through_alias(AliasOfAliased v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(number) => {
            refl;
        }

        alternative<1>(flag) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}

// cv-qualification and reference binding do not change the state space.
proof variant_qualified(const Aliased& v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(number) => {
            refl;
        }

        alternative<1>(flag) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}

// 18/21. A dependent form resolves by canonical identity after substitution,
// and cv-qualification does not change the state space.
template <typename T> using Sum = std::variant<T, bool>;

proof variant_dependent(Sum<unsigned> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(number) => {
            refl;
        }

        alternative<1>(flag) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}

proof product_qualified(const Point& p)
    proves (Eq<bool>(true, true))
{
    decompose p {
        components(x, y) => {
            refl;
        }
    }
}

// 4. std::optional. The payload is bound only in `some`.
proof optional_states(std::optional<unsigned> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(payload) => {
            refl;
        }

        none => {
            refl;
        }
    }
}

proof optional_template(Wrapped<bool> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(payload) => {
            refl;
        }

        none => {
            refl;
        }
    }
}

// 6. Pointers carry exactly two states and nothing about lifetime or ownership.
proof pointer_states(const int* p)
    proves (Eq<bool>(true, true))
{
    cases p {
        null => {
            refl;
        }

        non_null => {
            refl;
        }
    }
}

// 7. Products: records, pair, tuple, std::array and built-in arrays.
proof record_fields(Point p)
    proves (Eq<bool>(true, true))
{
    decompose p {
        components(x, y) => {
            refl;
        }
    }
}

proof pair_fields(std::pair<int, bool> p)
    proves (Eq<bool>(true, true))
{
    decompose p {
        components(first, second) => {
            refl;
        }
    }
}

proof tuple_fields(std::tuple<int, bool, unsigned> t)
    proves (Eq<bool>(true, true))
{
    decompose t {
        components(a, b, c) => {
            refl;
        }
    }
}

proof std_array_fields(std::array<int, 3> a)
    proves (Eq<bool>(true, true))
{
    decompose a {
        components(zero, one, two) => {
            refl;
        }
    }
}

proof builtin_array_fields(int (&a)[2])
    proves (Eq<bool>(true, true))
{
    decompose a {
        components(zero, one) => {
            refl;
        }
    }
}

// 8. Nesting composes generically: a product inside a product.
proof nested_product(Nested n)
    proves (Eq<bool>(true, true))
{
    decompose n {
        components(origin, flagged) => {
            decompose origin {
                components(x, y) => {
                    refl;
                }
            }
        }
    }
}

// 33. Cross-provider composition in both directions.
proof variant_of_optional(std::variant<std::optional<int>, bool> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(inner) => {
            cases inner {
                some(payload) => {
                    refl;
                }

                none => {
                    refl;
                }
            }
        }

        alternative<1>(flag) => {
            refl;
        }

        valueless => {
            refl;
        }
    }
}

proof optional_of_variant(std::optional<Aliased> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(inner) => {
            cases inner {
                alternative<0>(number) => {
                    refl;
                }

                alternative<1>(flag) => {
                    refl;
                }

                valueless => {
                    refl;
                }
            }
        }

        none => {
            refl;
        }
    }
}

proof optional_of_product(std::optional<Point> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(inner) => {
            decompose inner {
                components(x, y) => {
                    refl;
                }
            }
        }

        none => {
            refl;
        }
    }
}

proof product_of_optional(std::pair<std::optional<int>, bool> p)
    proves (Eq<bool>(true, true))
{
    decompose p {
        components(maybe, flag) => {
            cases maybe {
                some(payload) => {
                    refl;
                }

                none => {
                    refl;
                }
            }
        }
    }
}

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

proof pinned_components(Holder h)
    proves (Eq<bool>(true, true))
{
    decompose h {
        components(pinned, flagged) => {
            decompose pinned {
                components(v) => {
                    refl;
                }
            }
        }
    }
}

// 8/29. Binder types resolve to a fixpoint, so nesting depth is what costs, not
// a fixed pass count. Four levels stay well inside the limit.
proof deeply_nested(std::optional<std::optional<std::optional<std::optional<int>>>> o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(a) => {
            cases a {
                some(b) => {
                    cases b {
                        some(c) => {
                            cases c {
                                some(d) => {
                                    refl;
                                }

                                none => {
                                    refl;
                                }
                            }
                        }

                        none => {
                            refl;
                        }
                    }
                }

                none => {
                    refl;
                }
            }
        }

        none => {
            refl;
        }
    }
}

// 25. Decomposition composes with the rest of the proof system.
proof under_quantifier(std::optional<unsigned> o)
    proves (forall(unsigned x) { x == x })
{
    cases o {
        some(payload) => {
            refl;
        }

        none => {
            refl;
        }
    }
}

law settled(unsigned x)
    proves (x == x);

// A law is discharged at its own parameters by a proof whose body splits cases.
proof settled_holds(unsigned x)
    proves (settled(x))
{
    exact helper(x);
}

proof helper(unsigned x)
    proves (x == x)
{
    refl;
}

// A case split inside a proof that itself depends on a law-backed step.
proof splits_then_applies(std::optional<unsigned> o, unsigned x)
    proves (settled(x))
{
    cases o {
        some(payload) => {
            exact settled_holds(x);
        }

        none => {
            exact settled_holds(x);
        }
    }
}

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

// Through a member: the subject is a member access, resolved by Clang like
// every other expression a proof mentions, and the decomposition is the
// member's own.
proof member_subject(HasOptional h)
    proves (Eq<bool>(true, true))
{
    cases h.maybe {
        some(payload) => {
            refl;
        }

        none => {
            refl;
        }
    }
}

// Through a member of a reference parameter: the storage is the caller's, so
// another object may designate it. The subject is still the value that storage
// holds here, which is why no aliasing exception is needed (SPEC.md CASE-009).
proof aliased_member_subject(const HasOptional& h)
    proves (Eq<bool>(true, true))
{
    cases h.maybe {
        some(payload) => {
            refl;
        }

        none => {
            refl;
        }
    }
}

// Through an element of a built-in array taken by reference, so the storage is
// the caller's rather than this body's. `std::array` is deliberately not used
// here: its `operator[]` is a call, which this implementation refuses as a
// subject rather than modeling.
proof element_subject(std::optional<int> (&a)[2])
    proves (Eq<bool>(true, true))
{
    cases a[0] {
        some(payload) => {
            refl;
        }

        none => {
            refl;
        }
    }
}

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

proof is_cargo(Cargo c)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof crate_by_reference(const Crate<Cargo>& c)
    proves (Eq<bool>(true, true))
{
    decompose c {
        components(item, sealed) => {
            exact is_cargo(item);
        }
    }
}

// The payload binder of an optional taken by reference is itself a reference to
// a specialization nothing instantiated.
proof crate_inside_optional_reference(const std::optional<Crate<long>>& o)
    proves (Eq<bool>(true, true))
{
    cases o {
        some(crate) => {
            decompose crate {
                components(item, sealed) => {
                    refl;
                }
            }
        }

        none => {
            refl;
        }
    }
}

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

proof is_left(Left x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof is_right(Right x)
    proves (Eq<bool>(true, true))
{
    refl;
}

proof is_other(Other x)
    proves (Eq<bool>(true, true))
{
    refl;
}

// 33. A product inside a sum: the alternative's payload is a tuple whose
// components are bound in order.
proof variant_of_tuple(std::variant<std::tuple<Left, Right>, Other> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(pair) => {
            decompose pair {
                components(first, second) => {
                    exact is_right(second);
                }
            }
        }

        alternative<1>(single) => {
            exact is_other(single);
        }

        valueless => {
            refl;
        }
    }
}

// 33. Sums inside a record, reached by decomposing the record first.
struct Tagged {
    std::variant<Left, Right> choice;
    std::optional<Other> extra;
};

proof record_of_sums(Tagged t)
    proves (Eq<bool>(true, true))
{
    decompose t {
        components(choice, extra) => {
            cases choice {
                alternative<0>(left) => {
                    exact is_left(left);
                }

                alternative<1>(right) => {
                    cases extra {
                        some(other) => {
                            exact is_other(other);
                        }

                        none => {
                            exact is_right(right);
                        }
                    }
                }

                valueless => {
                    refl;
                }
            }
        }
    }
}

// 33. Optionals inside an array, each element decomposed on its own.
proof array_of_optional(std::array<std::optional<Left>, 2> a)
    proves (Eq<bool>(true, true))
{
    decompose a {
        components(first, second) => {
            cases second {
                some(payload) => {
                    exact is_left(payload);
                }

                none => {
                    refl;
                }
            }
        }
    }
}

// 33. A pointer inside a sum decomposes into null and non-null and binds nothing,
// wherever it sits.
proof variant_of_pointer(std::variant<const Left*, Right> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(pointer) => {
            cases pointer {
                null => {
                    refl;
                }

                non_null => {
                    refl;
                }
            }
        }

        alternative<1>(flag) => {
            exact is_right(flag);
        }

        valueless => {
            refl;
        }
    }
}

// 35. A variant inside a variant: each has its own index space and its own
// `valueless`.
proof nested_variants(std::variant<std::variant<Left, Right>, Other> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(inner) => {
            cases inner {
                alternative<0>(left) => {
                    exact is_left(left);
                }

                alternative<1>(right) => {
                    exact is_right(right);
                }

                valueless => {
                    refl;
                }
            }
        }

        alternative<1>(other) => {
            exact is_other(other);
        }

        valueless => {
            refl;
        }
    }
}

// 35. A variant of one alternative still has two states.
proof one_alternative(std::variant<Left> v)
    proves (Eq<bool>(true, true))
{
    cases v {
        alternative<0>(only) => {
            exact is_left(only);
        }

        valueless => {
            refl;
        }
    }
}

// 35. Pointers: a const pointer, a reference to a pointer and an alias all have
// the same two states as the pointer they denote.
using LeftPointer = const Left*;

proof const_pointer(Left* const p)
    proves (Eq<bool>(true, true))
{
    cases p {
        null => {
            refl;
        }

        non_null => {
            refl;
        }
    }
}

proof pointer_reference(Left*& p)
    proves (Eq<bool>(true, true))
{
    cases p {
        null => {
            refl;
        }

        non_null => {
            refl;
        }
    }
}

proof pointer_alias(LeftPointer p)
    proves (Eq<bool>(true, true))
{
    cases p {
        null => {
            refl;
        }

        non_null => {
            refl;
        }
    }
}

// 35. Products: a class with public members, move-only fields, a class template
// and an alias of one. None needs a copy, since a binder is a projection.
class Open {
  public:
    Left first;
    Right second;
};

proof class_fields(Open o)
    proves (Eq<bool>(true, true))
{
    decompose o {
        components(first, second) => {
            exact is_right(second);
        }
    }
}

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

proof move_only_fields(HoldsMoveOnly h)
    proves (Eq<bool>(true, true))
{
    decompose h {
        components(owned, unique, flag) => {
            decompose owned {
                components(inner) => {
                    exact is_left(inner);
                }
            }
        }
    }
}

template <typename T> struct Box {
    T item;
    Right tag;
};

using LeftBox = Box<Left>;

proof template_record(Box<Other> b)
    proves (Eq<bool>(true, true))
{
    decompose b {
        components(item, tag) => {
            exact is_other(item);
        }
    }
}

proof alias_record(const LeftBox& b)
    proves (Eq<bool>(true, true))
{
    decompose b {
        components(item, tag) => {
            exact is_left(item);
        }
    }
}

// 33. None of this may reach the runtime.
int main() {
    std::printf("%d\n", 7);
}
