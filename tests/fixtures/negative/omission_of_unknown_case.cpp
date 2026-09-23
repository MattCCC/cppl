// An omission names a case of the subject's own partition, or nothing
// (SPEC.md CASE-004). A label the provider never described cannot be accounted
// for, so it cannot be omitted either.
law omits_a_case_that_does_not_exist(int* p)
    expects (p != nullptr)
    proves (Eq<bool>(p == nullptr, false));

proof omits_a_case_that_does_not_exist_holds(int* p)
    proves (omits_a_case_that_does_not_exist(p))
{
    assume not_null : p != nullptr;
    cases p {
        null => {
            rewrite not_null;
            refl;
        }

        omit no_such_case by contradiction not_null;
    }
}

int main() {
    return 0;
}
