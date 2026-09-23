// SPEC: CASE-007
// The refused half of a matched pair whose accepted half is `crate_by_reference`
// in `fixtures/structural_cases.cpp`. There the class template is defined, so
// the specialization can be instantiated and has components. Here it is only
// declared, so the specialization is incomplete and has no components to
// expose. It is refused by the provider, by name, not by a C++ error.
template <typename T> struct Crate;

proof undefined_crate(const Crate<int>& c)
    proves (Eq<bool>(true, true))
{
    decompose c {
        components(item) => {
            refl;
        }
    }
}

int main() {
    return 0;
}
