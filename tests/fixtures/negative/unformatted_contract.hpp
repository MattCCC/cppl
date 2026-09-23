// A verified function whose contract is not laid out canonically. The layout
// is this header's to report where the header itself is open, not a document's
// that includes it (tests/unit/lsp_server_test.cpp).
#pragma once

verified inline int identity_of(int x) ensures (result == x) { return x; }
