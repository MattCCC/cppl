// SPEC: STDMODEL-015
// Appending to a string may move its characters, short-string storage
// included: a span of it is stale afterwards. Accepted twin: `span_local`.
#include <span>
#include <string>

verified char stale_characters()
    ensures (true)
{
    std::string s = "ab";
    std::span<const char> view(s);
    s += 'c';
    return view[0];
}

int main() {
    return 0;
}
