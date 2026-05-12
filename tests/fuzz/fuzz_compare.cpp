#include "../../ZethaDB.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);
    try {
        zethadb::Database db;
        zethadb::exec_schema(db, "table t { id: int; name: string; active: bool; score: double; }");
        (void)zethadb::exec_query(db, "insert t { id: 1, name: \"x\", active: true, score: 0.5 }");
        (void)zethadb::exec_query(db, input);
    } catch (...) {
    }
    return 0;
}
