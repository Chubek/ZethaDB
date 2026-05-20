#include "../../ZethaINDEX.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string payload(reinterpret_cast<const char*>(data), size);
    try {
        std::vector<std::string> args{"query", payload, payload};
        (void)zetha::index::run_cli(args);
    } catch (...) {}

    try {
        std::vector<std::string> args{"build", "--input", payload, "--adapter", "txt", "--output", payload};
        (void)zetha::index::run_cli(args);
    } catch (...) {}
    return 0;
}
