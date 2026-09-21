#include <cstdio>

// Ordinary C++ names stay ordinary outside formal contexts.
template <class T> int Eq(T, T) {
    return 7;
}
template <class T> struct IdentityType {
    using type = T;
};
using U = unsigned;
pure U identity(U x) {
    return x;
}
pure int identity(int x) {
    return x;
}

law explicit_identity(U x)
    proves ((Eq<IdentityType<U>::type>(identity(x), x)));
proof explicit_identity_holds(U x)
    proves (explicit_identity(x))
{
    refl;
}
proof signed_identity(int x)
    proves (Eq<int>(identity(x), x))
{
    refl;
}
proof unsigned_identity(U x)
    proves (Eq<U>(x, x))
{
    refl;
}
proof reused(U x)
    proves (Eq<U>(identity(x), x))
{
    exact unsigned_identity(x);
}
proof nested_use(U x)
    proves (Eq<U>(identity(identity(x)), x))
{
    apply reused(x);
}
proof normalized(U x, U y)
    proves (Eq<U>(x + y, y + x))
{
    refl;
}
proof boolean_identity(bool x)
    proves (Eq<bool>(x, x))
{
    refl;
}

law conditional(U x, U y)
    expects (Eq<U>(x, y))
    proves (Eq<U>(identity(x), y));
proof conditional_holds(U x, U y)
    proves (conditional(x, y))
{
    assume h : Eq<U>(x, y);
    rewrite h;
    refl;
}

verified U keep(U x)
    expects (Eq<U>(x, 2u))
    ensures (Eq<U>(result, 2u))
{
    return x;
}

int main() {
    std::printf("%u %d\n", keep(2u), Eq(1, 2));
}
