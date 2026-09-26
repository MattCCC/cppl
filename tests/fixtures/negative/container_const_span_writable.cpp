// SPEC: STDMODEL-016, VERIFIED-036
// A span of const elements grants no write, whatever the storage it views
// permits. Accepted twin: `span_at`, which asks `readable` of such a span.
#include <span>

verified unsigned w(std::span<const unsigned> s)
    expects (writable(s))
    ensures (true)
{
    return 0u;
}

int main() {
    return 0;
}
