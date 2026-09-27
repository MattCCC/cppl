// SPEC: CONSTRUCT-138
// RFC 0022, the V1 verified subset: a verified body that uses this construct,
// module import, is refused.

import probe_module;

int main() { return probe(2u) == 2u ? 0 : 1; }
