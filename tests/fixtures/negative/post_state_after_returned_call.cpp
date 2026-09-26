// SPEC: VERIFIED-031, VERIFIED-014
//
// `claims` never writes `r`, so after it returns `r` holds whatever the caller
// passed: its postcondition is false whenever that is not 5. The returned value
// is a call, and the obligation for the return was once stated with the
// post-state of `r` lowered before that call's result was in scope, so `r` was
// read as the call's result, which is 5, and the claim was proven. The accepted
// half writes `r` first: `writes_then_calls` in `fixtures/post_state_calls.cpp`.
verified unsigned five()
    ensures (result == 5u)
{
    return 5u;
}

verified unsigned claims(unsigned& r)
    ensures (r == 5u)
{
    return five();
}

int main() {
    unsigned x = 1u;
    const unsigned returned = claims(x);
    return static_cast<int>(returned + x);
}
