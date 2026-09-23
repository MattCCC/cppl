#include "cppl/testing/test.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cppl::testing {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

Registrar::Registrar(std::string name, std::function<void()> body) {
    registry().push_back(TestCase{std::move(name), std::move(body)});
}

void fail(const char* file, int line, const std::string& message) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + message);
}

} // namespace cppl::testing

int main() {
    std::size_t failures = 0;
    for (const cppl::testing::TestCase& test : cppl::testing::registry()) {
        try {
            test.body();
            std::cout << "ok   " << test.name << "\n";
        } catch (const std::exception& error) {
            std::cout << "FAIL " << test.name << "\n     " << error.what() << "\n";
            ++failures;
        }
    }

    std::cout << cppl::testing::registry().size() - failures << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
