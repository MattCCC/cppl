// A construct whose delimiters never close is refused, not read to the end of
// the file.
//
// These matter because the recognizer decides where C++L syntax stops and
// ordinary C++ resumes. An unterminated specification that swallowed the rest
// of the translation unit could silently take other declarations into itself,
// so each one fails where it opened.
#include <iostream>

pure unsigned identity(unsigned x) { return x; }

// The predicate of a refinement type never closes.
type Unclosed =
    unsigned where (self < 10u;

// A Law's specification expression never closes.
law unclosed_proposition(unsigned x)
    proves (identity(x) == x;

law unreached(unsigned x)
    proves (identity(x) == x);

int main() {
    std::cout << identity(41u) << "\n";
    return 0;
}
