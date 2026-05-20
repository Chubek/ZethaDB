#include "../../ZethaINDEX.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);
    try {
        (void)zetha::index::parse_filter(input);
    } catch (...) {}

    try {
        zetha::index::Index idx;
        zetha::index::DocumentMeta doc;
        doc.path = "fuzz.txt";
        doc.adapter = "txt";
        doc.metadata["path"] = "fuzz.txt";
        doc.tokens = zetha::index::tokenize(input);
        idx.documents.push_back(doc);

        zetha::index::TermEntry entry;
        entry.term = "seed";
        entry.postings.push_back(zetha::index::Posting{0, {0}});
        idx.terms.push_back(entry);
        idx.rebuild();
        (void)zetha::index::query(idx, input);
    } catch (...) {}
    return 0;
}
