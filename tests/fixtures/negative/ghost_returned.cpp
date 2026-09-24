// SPEC: GHOST-001, GHOST-002
// Ghost state leaves the program before it runs, so it cannot be what a
// function returns: the erased program would return nothing it computed.
verified int five()
    ensures (result == 5)
{
    ghost int g = 5;
    return g;
}

int main() {
    return five();
}
