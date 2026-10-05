// SPEC: CONSTRUCT-120
// RFC 0022, the V1 verified subset: a verified body may use this construct, class declaration,
// and it is modeled. Its refused twin is negative/subset/x120_class_declaration.cpp.

class Account {
  public:
    unsigned balance;

    verified unsigned get() const
        ensures (result == balance)
    {
        return balance;
    }
};

int main() {
    const Account account{2u};
    return account.get() == 2u ? 0 : 1;
}
