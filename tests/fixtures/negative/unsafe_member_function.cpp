// SPEC: UNSAFE-001
// This implementation recognizes unsafe functions at namespace scope only. A
// member marked unsafe is refused rather than left callable from a verified
// path as though it were ordinary.
struct Device {
    unsafe unsigned read();
};

unsigned Device::read() {
    return 42u;
}

int main() {
    Device device;
    return static_cast<int>(device.read());
}
