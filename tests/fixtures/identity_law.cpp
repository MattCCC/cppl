#include <iostream>

pure int identity(int x) {
    return x;
}

law identity_returns_input(int x)
    ensures(identity(x) == x);

int main() {
    std::cout << identity(41) << "\n";
    return 0;
}
