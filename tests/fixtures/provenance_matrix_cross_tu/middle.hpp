#pragma once

verified unsigned middle_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u);

verified unsigned middle_all(unsigned x, int raw)
    expects (x < 10u)
    ensures (result == result);

verified unsigned middle_plain(unsigned x)
    ensures (result == x);
