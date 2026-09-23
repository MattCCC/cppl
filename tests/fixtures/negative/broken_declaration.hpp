// A header with a C++ error in it. An editor showing a document that includes
// it, directly or through another header, reports the error on that
// document's `#include`, never at this line of the document
// (tests/unit/lsp_server_test.cpp).
#pragma once

int broken_declaration = ;
