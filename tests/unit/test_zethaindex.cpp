#include "ZethaINDEX.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "zethaindex_test";
    fs::create_directories(dir);
    const auto html = dir / "doc.html";
    const auto txt = dir / "doc.txt";
    const auto cfg = dir / "INDEX.json";
    const auto idx = dir / "out.zidx";
    const auto filter = dir / "filter.dsl";

    {
        std::ofstream(html) << "<html><body>Performance and neural network</body></html>";
        std::ofstream(txt) << "neural network performance";
        std::ofstream(filter) << "require token \"performance\";";
        std::ofstream(cfg) << R"({"input":[{"path":")" << dir.string() << R"(","adapter":"html","recursive":false}],"output":")" << idx.string() << R"(","filter":")" << filter.string() << R"(","stemming":"none","stopwords":"smart"})";
    }

    const auto config = zetha::index::load_config(cfg);
    const auto built = zetha::index::build_index(zetha::index::BuildOptions{config.inputs, config.output, config.filter_file, config.stemming, config.stopwords});
    zetha::index::save_index(built, idx);
    const auto loaded = zetha::index::load_index(idx);
    assert(!loaded.terms.empty());
    assert(!zetha::index::query(loaded, "performance").empty());
    return 0;
}
