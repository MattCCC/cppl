// SPEC: CONSTRUCT-056
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// overloaded subscript, is refused.

struct Table {
    unsigned entries[4];

    unsigned operator[](unsigned index) const {
        return entries[index % 4u];
    }
};

verified unsigned probe(Table table)
    ensures (result == result)
{
    return table[1u];
}

int main() { return probe(Table{{1u, 2u, 3u, 4u}}) == 2u ? 0 : 1; }
