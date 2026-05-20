#include "ZethaINDEX.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "zethaindex_cli_test";
    fs::create_directories(dir);
    const auto input = dir / "doc.txt";
    const auto index = dir / "cli.zidx";

    std::ofstream(input) << "neural network performance";

    {
        std::vector<std::string> args{
            "build", "--input", input.string(), "--adapter", "txt", "--output", index.string(), "--stopwords", "none"
        };
        const int rc = zetha::index::run_cli(args);
        assert(rc == 0);
    }
    assert(fs::exists(index));

    {
        std::vector<std::string> args{"inspect", index.string()};
        const int rc = zetha::index::run_cli(args);
        assert(rc == 0);
    }
    {
        std::vector<std::string> args{"query", index.string(), "neural"};
        const int rc = zetha::index::run_cli(args);
        assert(rc == 0);
    }
    {
        std::vector<std::string> args{"export", index.string()};
        const int rc = zetha::index::run_cli(args);
        assert(rc == 0);
    }

    return 0;
}
