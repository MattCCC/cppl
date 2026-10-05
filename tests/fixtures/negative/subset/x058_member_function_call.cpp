// SPEC: CONSTRUCT-058
// RFC 0022: the refused twin of subset/x058_member_function_call.cpp (member function call), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

struct Counter {
    unsigned count;

    verified unsigned next() const
        expects (count < 100u)
        ensures (result == count + 1u)
    {
        return count + 1u;
    }
};

verified unsigned probe(Counter counter)
    expects (counter.count < 200u)
    ensures (result == counter.count + 1u)
{
    return counter.next();
}

int main() { return probe(Counter{2u}) == 3u ? 0 : 1; }
