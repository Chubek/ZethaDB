#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "SerdeTk/SerdeTk.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace zetha::index {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct TextAdapter {
    virtual ~TextAdapter() = default;
    virtual std::string name() const = 0;
    virtual std::string extract_text(const std::filesystem::path& path) const = 0;
};

struct PlainTextAdapter final : TextAdapter {
    std::string name() const override { return "txt"; }
    std::string extract_text(const std::filesystem::path& path) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw Error("cannot open " + path.string());
        std::ostringstream out;
        out << input.rdbuf();
        return out.str();
    }
};

struct HtmlTextAdapter final : TextAdapter {
    std::string name() const override { return "html"; }
    std::string extract_text(const std::filesystem::path& path) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw Error("cannot open " + path.string());
        std::ostringstream out;
        out << input.rdbuf();
        const std::string html = out.str();
        std::string text;
        bool in_tag = false;
        for (char ch : html) {
            if (ch == '<') { in_tag = true; continue; }
            if (ch == '>') { in_tag = false; text.push_back(' '); continue; }
            if (!in_tag) text.push_back(ch);
        }
        return text;
    }
};

struct PdfTextAdapter final : TextAdapter {
    std::string name() const override { return "pdf"; }
    std::string extract_text(const std::filesystem::path& path) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw Error("cannot open " + path.string());
        std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::string text;
        text.reserve(data.size());
        for (unsigned char ch : data) {
            if (std::isprint(ch) || std::isspace(ch)) text.push_back(static_cast<char>(ch));
        }
        return text;
    }
};

inline std::unique_ptr<TextAdapter> adapter_for(std::string_view name) {
    if (name == "txt" || name == "text" || name == "plain") return std::make_unique<PlainTextAdapter>();
    if (name == "html" || name == "htm") return std::make_unique<HtmlTextAdapter>();
    if (name == "pdf") return std::make_unique<PdfTextAdapter>();
    throw Error("unknown adapter: " + std::string(name));
}

struct DocumentSource {
    std::filesystem::path path;
    std::string adapter;
    bool recursive{false};
};

struct Config {
    std::vector<DocumentSource> inputs;
    std::filesystem::path output{"index.zidx"};
    std::string filter_file;
    std::string stemming{"none"};
    std::string stopwords{"smart"};
};

struct DocumentMeta {
    std::filesystem::path path;
    std::string adapter;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> tokens;
};

struct Posting {
    std::uint32_t doc{0};
    std::vector<std::uint32_t> positions;
};

struct TermEntry {
    std::string term;
    std::vector<Posting> postings;
};

struct Index {
    std::vector<DocumentMeta> documents;
    std::vector<TermEntry> terms;
    std::unordered_map<std::string, std::size_t> term_to_index;

    void rebuild() {
        term_to_index.clear();
        for (std::size_t i = 0; i < terms.size(); ++i) term_to_index[terms[i].term] = i;
    }
};

struct BuildOptions {
    std::vector<DocumentSource> inputs;
    std::filesystem::path output{"index.zidx"};
    std::string filter_file;
    std::string stemming{"none"};
    std::string stopwords{"smart"};
};

struct QueryHit {
    std::string term;
    std::vector<std::uint32_t> documents;
};

inline std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

inline bool is_token_char(unsigned char ch) {
    return std::isalnum(ch) || ch == '\'';
}

inline std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (unsigned char ch : text) {
        if (is_token_char(ch)) {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));
    return tokens;
}

inline std::string stem(std::string token, std::string_view mode) {
    if (mode == "none") return token;
    auto drop = [&](std::string_view suffix) {
        if (token.size() > suffix.size() + 2 && token.ends_with(suffix)) {
            token.erase(token.size() - suffix.size());
            return true;
        }
        return false;
    };
    drop("ingly") || drop("edly") || drop("ing") || drop("ed") || drop("es") || drop("s");
    return token;
}

inline std::unordered_set<std::string> smart_stopwords() {
    return {
        "a","an","and","are","as","at","be","by","for","from","has","have","he","in","is","it","its","of","on","or","that","the","to","was","were","with","this","these","those"
    };
}

inline std::unordered_set<std::string> stopword_set(std::string_view mode) {
    if (mode == "none") return {};
    return smart_stopwords();
}

enum class FilterKind { Token, MetadataCompare, HasAny, Regex };
enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

struct FilterRule {
    bool require{true};
    FilterKind kind{FilterKind::Token};
    std::string key;
    CompareOp op{CompareOp::Eq};
    std::string value;
    std::vector<std::string> values;
};

struct FilterProgram {
    std::vector<FilterRule> rules;
    bool empty() const { return rules.empty(); }
};

inline bool compare_values(std::string_view lhs, CompareOp op, std::string_view rhs) {
    const auto numeric = [](std::string_view text, long long& out) {
        const auto* begin = text.data();
        const auto* end = begin + text.size();
        auto [ptr, ec] = std::from_chars(begin, end, out);
        return ec == std::errc{} && ptr == end;
    };
    long long li{}, ri{};
    const bool ln = numeric(lhs, li);
    const bool rn = numeric(rhs, ri);
    const int cmp = (ln && rn) ? ((li < ri) ? -1 : (li > ri ? 1 : 0)) : (lhs < rhs ? -1 : (lhs > rhs ? 1 : 0));
    switch (op) {
        case CompareOp::Eq: return cmp == 0;
        case CompareOp::Ne: return cmp != 0;
        case CompareOp::Lt: return cmp < 0;
        case CompareOp::Le: return cmp <= 0;
        case CompareOp::Gt: return cmp > 0;
        case CompareOp::Ge: return cmp >= 0;
    }
    return false;
}

inline std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

inline std::vector<std::string> split_statements(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    bool in_quote = false;
    char quote = 0;
    for (char ch : text) {
        if ((ch == '"' || ch == '\'') && (!in_quote || ch == quote)) {
            in_quote = !in_quote;
            quote = in_quote ? ch : 0;
            current.push_back(ch);
        } else if (ch == ';' && !in_quote) {
            auto t = trim(current);
            if (!t.empty()) out.push_back(std::move(t));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    auto t = trim(current);
    if (!t.empty()) out.push_back(std::move(t));
    return out;
}

inline std::string unquote(std::string text) {
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

inline FilterProgram parse_filter(const std::string& text) {
    FilterProgram program;
    for (auto stmt : split_statements(text)) {
        FilterRule rule;
        if (stmt.rfind("require ", 0) == 0) {
            rule.require = true;
            stmt.erase(0, 8);
        } else if (stmt.rfind("exclude ", 0) == 0) {
            rule.require = false;
            stmt.erase(0, 8);
        } else {
            throw Error("invalid filter statement: " + stmt);
        }

        stmt = trim(stmt);
        if (stmt.rfind("token ", 0) == 0) {
            rule.kind = FilterKind::Token;
            rule.value = unquote(trim(stmt.substr(6)));
        } else if (stmt.rfind("metadata.", 0) == 0) {
            rule.kind = FilterKind::MetadataCompare;
            auto rest = stmt.substr(9);
            auto op_pos = rest.find_first_of("=!<>");
            if (op_pos == std::string::npos) throw Error("invalid metadata filter: " + stmt);
            rule.key = trim(rest.substr(0, op_pos));
            std::string op_text = trim(rest.substr(op_pos, rest.find_first_of(" \t", op_pos) - op_pos));
            if (op_text == "==") rule.op = CompareOp::Eq;
            else if (op_text == "!=") rule.op = CompareOp::Ne;
            else if (op_text == "<") rule.op = CompareOp::Lt;
            else if (op_text == "<=") rule.op = CompareOp::Le;
            else if (op_text == ">") rule.op = CompareOp::Gt;
            else if (op_text == ">=") rule.op = CompareOp::Ge;
            else throw Error("invalid comparator: " + op_text);
            rule.value = unquote(trim(rest.substr(op_pos + op_text.size())));
        } else if (stmt.rfind("has_any(", 0) == 0 && stmt.back() == ')') {
            rule.kind = FilterKind::HasAny;
            auto inner = stmt.substr(8, stmt.size() - 9);
            std::stringstream ss(inner);
            std::string item;
            while (std::getline(ss, item, ',')) rule.values.push_back(unquote(trim(item)));
        } else if (stmt.rfind("match_regex ", 0) == 0) {
            rule.kind = FilterKind::Regex;
            rule.value = unquote(trim(stmt.substr(12)));
        } else {
            throw Error("invalid filter predicate: " + stmt);
        }
        program.rules.push_back(std::move(rule));
    }
    return program;
}

inline bool filter_matches(const FilterProgram& program, const DocumentMeta& doc, const std::string& text, const std::vector<std::string>& tokens) {
    for (const auto& rule : program.rules) {
        bool matched = false;
        switch (rule.kind) {
            case FilterKind::Token:
                matched = std::find(tokens.begin(), tokens.end(), lower(rule.value)) != tokens.end();
                break;
            case FilterKind::MetadataCompare: {
                auto it = doc.metadata.find(rule.key);
                matched = it != doc.metadata.end() && compare_values(it->second, rule.op, rule.value);
                break;
            }
            case FilterKind::HasAny:
                for (const auto& value : rule.values) {
                    if (std::find(tokens.begin(), tokens.end(), lower(value)) != tokens.end()) { matched = true; break; }
                }
                break;
            case FilterKind::Regex:
                matched = text.find(rule.value) != std::string::npos;
                break;
        }
        if (rule.require != matched) return false;
    }
    return true;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("cannot open " + path.string());
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

inline std::string value_as_string(const serdetk::Value& value) {
    if (value.is_string()) return value.as_string();
    if (value.is_int()) return std::to_string(value.is_int() ? std::get<std::int64_t>(value.data) : 0);
    if (value.is_uint()) return std::to_string(std::get<std::uint64_t>(value.data));
    if (value.is_double()) {
        std::ostringstream out;
        out << std::get<double>(value.data);
        return out.str();
    }
    if (value.is_bool()) return std::get<bool>(value.data) ? "true" : "false";
    return {};
}

inline bool value_as_bool(const serdetk::Value& value) {
    if (value.is_bool()) return std::get<bool>(value.data);
    if (value.is_int()) return std::get<std::int64_t>(value.data) != 0;
    if (value.is_uint()) return std::get<std::uint64_t>(value.data) != 0;
    if (value.is_string()) {
        const auto s = lower(value.as_string());
        return s == "true" || s == "1" || s == "yes";
    }
    return false;
}

inline Config config_from_document(const serdetk::Document& doc) {
    if (!doc.root.is_object()) throw Error("config root must be an object");
    const auto& root = doc.root.as_object().fields;
    Config cfg;

    auto it = root.find("output");
    if (it != root.end()) cfg.output = value_as_string(it->second);
    it = root.find("filter");
    if (it != root.end()) cfg.filter_file = value_as_string(it->second);
    it = root.find("stemming");
    if (it != root.end()) cfg.stemming = value_as_string(it->second);
    it = root.find("stopwords");
    if (it != root.end()) cfg.stopwords = value_as_string(it->second);

    auto inputs_it = root.find("input");
    if (inputs_it == root.end() || !inputs_it->second.is_array()) throw Error("config missing input array");
    for (const auto& entry : inputs_it->second.as_array().items) {
        if (!entry.is_object()) throw Error("input entry must be an object");
        const auto& obj = entry.as_object().fields;
        DocumentSource source;
        auto path_it = obj.find("path");
        auto adapter_it = obj.find("adapter");
        if (path_it == obj.end() || adapter_it == obj.end()) throw Error("input entry missing path/adapter");
        source.path = value_as_string(path_it->second);
        source.adapter = lower(value_as_string(adapter_it->second));
        auto rec_it = obj.find("recursive");
        if (rec_it != obj.end()) source.recursive = value_as_bool(rec_it->second);
        cfg.inputs.push_back(std::move(source));
    }
    return cfg;
}

inline Config load_config(const std::filesystem::path& path) {
    const auto ext = lower(path.extension().string());
    serdetk::Document doc;
    if (ext == ".json") doc = serdetk::builtins::json().load_file(path);
    else if (ext == ".yaml" || ext == ".yml") doc = serdetk::builtins::yaml().load_file(path);
    else throw Error("unsupported config format: " + path.string());
    return config_from_document(doc);
}

struct PostingsBuilder {
    std::unordered_map<std::string, std::map<std::uint32_t, std::vector<std::uint32_t>>> postings;

    void add(std::string term, std::uint32_t doc, std::uint32_t position) {
        postings[std::move(term)][doc].push_back(position);
    }
};

inline std::vector<std::filesystem::path> enumerate_documents(const DocumentSource& source) {
    std::vector<std::filesystem::path> out;
    if (std::filesystem::is_regular_file(source.path)) {
        out.push_back(source.path);
    } else if (std::filesystem::is_directory(source.path)) {
        if (source.recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(source.path)) if (entry.is_regular_file()) out.push_back(entry.path());
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(source.path)) if (entry.is_regular_file()) out.push_back(entry.path());
        }
    } else {
        throw Error("missing input path: " + source.path.string());
    }
    return out;
}

inline std::string infer_adapter(std::filesystem::path path) {
    const auto ext = lower(path.extension().string());
    if (ext == ".html" || ext == ".htm") return "html";
    if (ext == ".pdf") return "pdf";
    return "txt";
}

inline std::string load_filter_text(const std::string& path) {
    if (path.empty()) return {};
    return read_file(path);
}

inline Index build_index(const BuildOptions& options) {
    const FilterProgram filter = options.filter_file.empty() ? FilterProgram{} : parse_filter(load_filter_text(options.filter_file));
    const auto stopwords = stopword_set(options.stopwords);
    PostingsBuilder builder;
    Index index;

    for (const auto& source : options.inputs) {
        auto adapter = adapter_for(source.adapter.empty() ? infer_adapter(source.path) : source.adapter);
        for (const auto& file : enumerate_documents(source)) {
            DocumentMeta meta;
            meta.path = file;
            meta.adapter = adapter->name();
            meta.metadata["path"] = file.string();
            meta.metadata["name"] = file.filename().string();
            meta.metadata["extension"] = lower(file.extension().string());
            const std::string text = adapter->extract_text(file);
            auto tokens = tokenize(text);
            std::vector<std::string> filtered;
            filtered.reserve(tokens.size());
            for (auto token : tokens) {
                token = stem(std::move(token), options.stemming);
                if (!token.empty() && !stopwords.contains(token)) filtered.push_back(std::move(token));
            }
            meta.tokens = filtered;
            if (!filter.empty() && !filter_matches(filter, meta, text, meta.tokens)) continue;

            const std::uint32_t doc_id = static_cast<std::uint32_t>(index.documents.size());
            index.documents.push_back(meta);
            for (std::uint32_t pos = 0; pos < filtered.size(); ++pos) builder.add(filtered[pos], doc_id, pos);
        }
    }

    index.terms.reserve(builder.postings.size());
    std::vector<std::string> sorted_terms;
    sorted_terms.reserve(builder.postings.size());
    for (auto& [term, _] : builder.postings) sorted_terms.push_back(term);
    std::sort(sorted_terms.begin(), sorted_terms.end());
    for (const auto& term : sorted_terms) {
        TermEntry entry;
        entry.term = term;
        const auto& doc_map = builder.postings[term];
        for (const auto& [doc, positions] : doc_map) entry.postings.push_back(Posting{doc, positions});
        index.terms.push_back(std::move(entry));
    }
    index.rebuild();
    return index;
}

inline void write_u32(std::ostream& out, std::uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline void write_u64(std::ostream& out, std::uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

inline std::uint32_t read_u32(std::istream& in) {
    std::uint32_t value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline std::uint64_t read_u64(std::istream& in) {
    std::uint64_t value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

inline void write_string(std::ostream& out, const std::string& text) {
    write_u32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline std::string read_string(std::istream& in) {
    const auto size = read_u32(in);
    std::string text(size, '\0');
    in.read(text.data(), static_cast<std::streamsize>(size));
    return text;
}

inline void save_index(const Index& index, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw Error("cannot write " + path.string());
    out.write("ZIDX0001", 8);
    write_u64(out, index.documents.size());
    write_u64(out, index.terms.size());
    for (const auto& doc : index.documents) {
        write_string(out, doc.path.string());
        write_string(out, doc.adapter);
        write_u32(out, static_cast<std::uint32_t>(doc.metadata.size()));
        for (const auto& [k, v] : doc.metadata) { write_string(out, k); write_string(out, v); }
    }
    std::string previous;
    for (const auto& term : index.terms) {
        std::size_t prefix = 0;
        while (prefix < previous.size() && prefix < term.term.size() && previous[prefix] == term.term[prefix]) ++prefix;
        write_u32(out, static_cast<std::uint32_t>(prefix));
        write_string(out, term.term.substr(prefix));
        write_u32(out, static_cast<std::uint32_t>(term.postings.size()));
        for (const auto& posting : term.postings) {
            write_u32(out, posting.doc);
            write_u32(out, static_cast<std::uint32_t>(posting.positions.size()));
            for (auto pos : posting.positions) write_u32(out, pos);
        }
        previous = term.term;
    }
}

inline Index load_index(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("cannot open " + path.string());
    char magic[8]{};
    in.read(magic, 8);
    if (std::string_view(magic, 8) != "ZIDX0001") throw Error("invalid index file");
    Index index;
    const auto doc_count = read_u64(in);
    const auto term_count = read_u64(in);
    index.documents.resize(doc_count);
    for (std::uint64_t i = 0; i < doc_count; ++i) {
        index.documents[i].path = read_string(in);
        index.documents[i].adapter = read_string(in);
        const auto meta_count = read_u32(in);
        for (std::uint32_t m = 0; m < meta_count; ++m) index.documents[i].metadata[read_string(in)] = read_string(in);
    }
    std::string previous;
    index.terms.reserve(term_count);
    for (std::uint64_t i = 0; i < term_count; ++i) {
        const auto prefix = read_u32(in);
        const auto suffix = read_string(in);
        TermEntry entry;
        entry.term = previous.substr(0, prefix) + suffix;
        const auto posting_count = read_u32(in);
        for (std::uint32_t p = 0; p < posting_count; ++p) {
            Posting posting;
            posting.doc = read_u32(in);
            const auto position_count = read_u32(in);
            posting.positions.resize(position_count);
            for (std::uint32_t q = 0; q < position_count; ++q) posting.positions[q] = read_u32(in);
            entry.postings.push_back(std::move(posting));
        }
        previous = entry.term;
        index.terms.push_back(std::move(entry));
    }
    index.rebuild();
    return index;
}

inline std::vector<std::uint32_t> postings_for_term(const Index& index, std::string_view term) {
    auto it = index.term_to_index.find(std::string(term));
    if (it == index.term_to_index.end()) return {};
    std::vector<std::uint32_t> docs;
    for (const auto& posting : index.terms[it->second].postings) docs.push_back(posting.doc);
    return docs;
}

inline std::vector<std::string> all_terms(const Index& index) {
    std::vector<std::string> out;
    out.reserve(index.terms.size());
    for (const auto& term : index.terms) out.push_back(term.term);
    return out;
}

inline std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev.back();
}

inline std::vector<QueryHit> query(const Index& index, std::string_view text, std::size_t fuzzy_distance = 1) {
    std::vector<std::string> terms = tokenize(std::string(text));
    std::vector<QueryHit> hits;
    for (const auto& term : terms) {
        if (auto docs = postings_for_term(index, term); !docs.empty()) {
            hits.push_back(QueryHit{term, std::move(docs)});
            continue;
        }
        QueryHit best{std::string(term), {}};
        for (const auto& candidate : index.terms) {
            if (std::abs(static_cast<int>(candidate.term.size()) - static_cast<int>(term.size())) > static_cast<int>(fuzzy_distance)) continue;
            if (edit_distance(term, candidate.term) <= fuzzy_distance) {
                for (const auto& posting : candidate.postings) best.documents.push_back(posting.doc);
            }
        }
        if (!best.documents.empty()) hits.push_back(std::move(best));
    }
    return hits;
}

inline std::vector<std::string> prefix_search(const Index& index, std::string_view prefix) {
    std::vector<std::string> out;
    for (const auto& term : index.terms) if (term.term.starts_with(prefix)) out.push_back(term.term);
    return out;
}

inline std::string json_escape(std::string_view text) {
    std::string out;
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

inline std::string query_result_json(const Index& index, std::string_view q, std::size_t fuzzy_distance = 1) {
    const auto hits = query(index, q, fuzzy_distance);
    std::ostringstream out;
    out << "{\"query\":\"" << json_escape(q) << "\",\"hits\":[";
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (i) out << ',';
        out << "{\"term\":\"" << json_escape(hits[i].term) << "\",\"docs\":[";
        for (std::size_t j = 0; j < hits[i].documents.size(); ++j) {
            if (j) out << ',';
            out << hits[i].documents[j];
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

inline std::string inspect_json(const Index& index) {
    std::ostringstream out;
    out << "{\"documents\":" << index.documents.size() << ",\"terms\":" << index.terms.size() << "}";
    return out.str();
}

inline std::string export_text(const Index& index) {
    std::ostringstream out;
    for (const auto& term : index.terms) {
        out << term.term << ':';
        for (const auto& posting : term.postings) out << ' ' << posting.doc;
        out << '\n';
    }
    return out.str();
}

inline int serve_http(const Index& index, std::uint16_t port) {
#if defined(_WIN32)
    (void)index; (void)port;
    throw Error("HTTP server unsupported on this platform");
#else
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw Error("socket failed");
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) throw Error("bind failed");
    if (::listen(fd, 16) < 0) throw Error("listen failed");
    for (;;) {
        const int client = ::accept(fd, nullptr, nullptr);
        if (client < 0) continue;
        char buffer[4096];
        const auto n = ::read(client, buffer, sizeof(buffer) - 1);
        if (n <= 0) { ::close(client); continue; }
        buffer[n] = '\0';
        std::string request(buffer);
        std::string q;
        if (auto pos = request.find("GET /search?q="); pos != std::string::npos) {
            pos += 14;
            auto end = request.find(' ', pos);
            q = request.substr(pos, end - pos);
        }
        std::string body = query_result_json(index, q);
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << body.size() << "\r\n\r\n" << body;
        const auto payload = response.str();
        ::write(client, payload.data(), payload.size());
        ::close(client);
    }
#endif
}

inline std::vector<std::string> args_to_vector(int argc, char** argv) {
    std::vector<std::string> out;
    for (int i = 1; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

inline std::optional<std::string> take_flag(std::vector<std::string>& args, std::string_view flag) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == flag && i + 1 < args.size()) {
            std::string value = args[i + 1];
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i + 2));
            return value;
        }
    }
    return std::nullopt;
}

inline bool has_flag(const std::vector<std::string>& args, std::string_view flag) {
    return std::find(args.begin(), args.end(), flag) != args.end();
}

inline int run_cli(std::vector<std::string> args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout << "zetha-index build|inspect|query|serve|export\n";
        return 0;
    }
    const std::string cmd = args.front();
    args.erase(args.begin());
    try {
        if (cmd == "build") {
            if (auto config = take_flag(args, "--config")) {
                const auto cfg = load_config(*config);
                const auto index = build_index(BuildOptions{cfg.inputs, cfg.output, cfg.filter_file, cfg.stemming, cfg.stopwords});
                save_index(index, cfg.output);
            } else {
                BuildOptions opts;
                if (auto output = take_flag(args, "--output")) opts.output = *output;
                if (auto filter = take_flag(args, "--filter")) opts.filter_file = *filter;
                if (auto stem = take_flag(args, "--stemmer")) opts.stemming = *stem;
                if (auto stop = take_flag(args, "--stopwords")) opts.stopwords = *stop;
                for (std::size_t i = 0; i < args.size();) {
                    if (args[i] != "--input") throw Error("unexpected argument: " + args[i]);
                    if (i + 1 >= args.size()) throw Error("--input requires a path");
                    DocumentSource source;
                    source.path = args[i + 1];
                    i += 2;
                    while (i < args.size() && args[i] != "--input") {
                        if (args[i] == "--adapter") {
                            if (i + 1 >= args.size()) throw Error("--adapter requires a value");
                            source.adapter = args[i + 1];
                            i += 2;
                        } else if (args[i] == "--recursive") {
                            source.recursive = true;
                            ++i;
                        } else {
                            throw Error("unexpected argument: " + args[i]);
                        }
                    }
                    opts.inputs.push_back(std::move(source));
                }
                if (opts.inputs.empty()) throw Error("build requires --input or --config");
                const auto index = build_index(opts);
                save_index(index, opts.output);
            }
            return 0;
        }
        if (cmd == "inspect") {
            if (args.empty()) throw Error("inspect requires index path");
            const auto index = load_index(args[0]);
            std::cout << inspect_json(index) << '\n';
            return 0;
        }
        if (cmd == "query") {
            if (args.size() < 2) throw Error("query requires index path and query text");
            const auto index = load_index(args[0]);
            std::cout << query_result_json(index, args[1]) << '\n';
            return 0;
        }
        if (cmd == "serve") {
            auto index_path = take_flag(args, "--index");
            auto port = take_flag(args, "--port");
            if (!index_path || !port) throw Error("serve requires --index and --port");
            const auto index = load_index(*index_path);
            return serve_http(index, static_cast<std::uint16_t>(std::stoul(*port)));
        }
        if (cmd == "export") {
            if (args.empty()) throw Error("export requires index path");
            const auto index = load_index(args[0]);
            std::cout << export_text(index);
            return 0;
        }
        throw Error("unknown command: " + cmd);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}

} // namespace zetha::index
