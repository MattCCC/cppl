// SPEC: RUNTIMECHECK-018
// A validation names the refinement it tests by the name its declaration gives
// it.
namespace units {
type Positive = int where (self > 0);
}

verified int positive_or_one(int raw)
    ensures (result > 0)
{
    if (validate<units::Positive>(raw)) {
        return raw;
    }
    return 1;
}

int main() {
    return positive_or_one(5);
}
