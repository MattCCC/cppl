// SPEC: VERIFIED-045, CLASS-013, CONTRACT-005
// `Ledger::free_on_page` of integration/ledger.cpp with the claim written in
// the out-of-line member definition itself rather than in the verified
// `page_room` it calls. A claim that a path cannot occur is checked only in a
// function marked `verified`, and an out-of-line member definition cannot be
// marked so, since it would restate the contract its class declares. The claim
// is refused, never read as ordinary C++ and never assumed.
type Lines = long long where (self >= 0ll && self <= 10000ll);

trusted law page_holds(long long lines_on_page)
    proves (lines_on_page <= 99ll);

proof page_bound(long long used)
    proves (used <= 99ll)
{
    exact page_holds(used);
}

struct Ledger {
    Lines lines;

    verified long long free_on_page() const
        ensures (0ll <= result && result <= 99ll);
};

long long Ledger::free_on_page() const {
    const long long used = lines;
    if (used > 99ll) {
        contradiction page_bound(used);
    }
    return 99ll - used;
}

int main() {
    return 0;
}
