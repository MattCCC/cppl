// SPEC: CLASS-010
// An object passed by reference may be the implicit object itself: called as
// `cursor.write_then_read(cursor)`, writing `pos` writes `other.pos`, and what
// `other.pos` held on entry is not what a read after the write sees. The
// accepted half reads before the write: `Cursor::read_then_write` in
// `fixtures/verified_methods.cpp`.
class Cursor {
  public:
    explicit Cursor(unsigned p) : pos(p) {}

    verified unsigned write_then_read(const Cursor& other)
        expects (other.pos == 4u)
        ensures (result == 4u)
    {
        pos = 9u;
        return other.pos;
    }

  private:
    unsigned pos;
};

int main() {
    Cursor cursor{4u};
    return static_cast<int>(cursor.write_then_read(cursor));
}
