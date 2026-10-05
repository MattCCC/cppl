#pragma once

verified unsigned imported_trusted(unsigned x)
    expects (x < 10u)
    ensures (result != 7u);

verified unsigned imported_unsafe(unsigned x)
    ensures (result == x);

verified unsigned imported_model()
    ensures (result == 2u);

verified int imported_validation(int raw)
    ensures (result > 0);

verified unsigned imported_plain(unsigned x)
    ensures (result == x);
