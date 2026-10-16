// Host-runnable unit tests for the portable core.
// Build: g++ -std=c++17 -I src tests/test_core.cpp src/core/*.cpp -o test_core
#include <iostream>

#include "core_tests.h"

int main() {
    const int failures = wrg::testing::runCoreTests(std::cout);
    return failures == 0 ? 0 : 1;
}
