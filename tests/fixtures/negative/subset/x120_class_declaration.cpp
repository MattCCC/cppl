// SPEC: CONSTRUCT-120
// RFC 0022: the refused twin of subset/x120_class_declaration.cpp (class declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

class Account {
  public:
    unsigned balance;

    verified unsigned get() const
        ensures (result == balance)
    {
        return balance + 1u;
    }
};

int main() {
    const Account account{2u};
    return account.get() == 2u ? 0 : 1;
}
