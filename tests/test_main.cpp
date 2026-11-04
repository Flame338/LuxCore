#include "TestFramework.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;

    int ran = 0;
    int failed = 0;
    for (const testing::TestCase& test : testing::registry()) {
        if (filter != nullptr && test.name.find(filter) == std::string::npos) {
            continue;
        }

        const int failures_before = testing::failure_count();
        test.run();
        ran++;

        if (testing::failure_count() > failures_before) {
            failed++;
            std::cout << "FAIL  " << test.name << "\n";
        } else {
            std::cout << "PASS  " << test.name << "\n";
        }
    }

    std::cout << "\n" << ran << " tests, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
