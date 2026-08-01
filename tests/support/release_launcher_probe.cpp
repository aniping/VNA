#include <cstdlib>

int main() {
    const auto* requestedExit = std::getenv("VNA_TEST_EXIT_CODE");
    return requestedExit == nullptr ? EXIT_SUCCESS : std::atoi(requestedExit);
}
