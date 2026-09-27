// SPEC: CONSTRUCT-127
// RFC 0022: the refused twin of subset/x127_friend_declaration.cpp (friend declaration), the same program
// with one thing changed, so the construct is shown modeled rather than passed over.

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
    return x + 1u;
}

int main() {
    const Account account(2u);
    return peek(account) == 2u && probe(2u) == 2u ? 0 : 1;
}
