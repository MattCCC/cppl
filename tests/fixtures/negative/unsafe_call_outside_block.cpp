// SPEC: UNSAFE-002
// A verified body crosses into an unsafe function only inside an unsafe block,
// where what the call does is modeled as unknown. Called on a verified path,
// its result would be a value nothing describes.
unsafe unsigned read_device() {
    return 42u;
}

verified unsigned direct_call()
    ensures (result < 100u)
{
    unsigned x = read_device();
    if (x < 100u) {
        return x;
    }
    return 0u;
}

int main() {
    return static_cast<int>(direct_call());
}
