// SPEC: RUNTIMECHECK-007, REFINEOBL-007
// The refined member is written on the path where the check failed. Twin of
// `checked_member`.
type Positive = int where (self > 0);

struct Reading {
    Positive level;
    int raw;
};

verified int unchecked_member(int raw)
    ensures (result > 0)
{
    Reading reading{1, raw};
    if (raw <= 0) {
        reading.level = raw;
    }
    return reading.level;
}

int main() {
    return unchecked_member(-2);
}
