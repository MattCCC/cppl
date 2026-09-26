// SPEC: UNSAFE-003, UNSAFE-005, CLASS-010
// The implicit object is caller storage an unsafe block may write, so after the
// block nothing is known of its members: not after a block that writes one,
// and not after a block that names none of them either, since what it calls
// may reach the object through a pointer it kept. The accepted half reads `pos`
// before the block: `Cursor::before_unsafe` in `fixtures/verified_methods.cpp`.
unsafe void poke() {
}

class Cursor {
  public:
    explicit Cursor(unsigned p) : pos(p) {}

    verified unsigned after_unsafe()
        expects (pos == 1u)
        ensures (result == 1u)
    {
        unsafe {
            pos = 7u;
        }
        return pos;
    }

    verified unsigned after_unnamed()
        expects (pos == 1u)
        ensures (result == 1u)
    {
        unsafe {
            poke();
        }
        return pos;
    }

  private:
    unsigned pos;
};

int main() {
    Cursor cursor{1u};
    return static_cast<int>(cursor.after_unsafe() + cursor.after_unnamed());
}
