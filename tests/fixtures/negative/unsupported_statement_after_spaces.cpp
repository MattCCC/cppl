// A statement this implementation does not support, written after another on
// the same line and a run of spaces. The preprocessor writes that run as one
// space; the refusal still points at the column the statement was written at.
proof p(int a)
    proves (a == a)
{
    refl;      frobnicate a;
}
