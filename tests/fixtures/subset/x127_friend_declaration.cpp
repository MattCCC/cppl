// SPEC: CONSTRUCT-127
// RFC 0022, the V1 verified subset: a verified body may use this construct, friend declaration,
// and it is modeled. Its refused twin is negative/subset/x127_friend_declaration.cpp.

class Account {
    unsigned balance;

    friend unsigned peek(const Account& account);

  public:
    explicit Account(unsigned value) : balance(value) {}
};

unsigned peek(const Account& account) {
    return account.balance;
}

verified unsigned probe(unsigned x)
    ensures (result == x)
{
    return x;
}

int main() {
    const Account account(2u);
    return peek(account) == 2u && probe(2u) == 2u ? 0 : 1;
}
