// SPEC: CONSTRUCT-058
// RFC 0022, the V1 verified subset: a verified body may use this construct, member function call,
// and it is modeled. Its refused twin is negative/subset/x058_member_function_call.cpp.

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
    expects (counter.count < 50u)
    ensures (result == counter.count + 1u)
{
    return counter.next();
}

int main() {
    return probe(Counter{2u}) == 3u ? 0 : 1;
}
