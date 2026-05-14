#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "DSLUtils.hpp"

namespace zethadb {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class ValueType {
    Int,
    Double,
    Bool,
    String
};

inline std::string to_string(ValueType t) {
    switch (t) {
        case ValueType::Int: return "int";
        case ValueType::Double: return "double";
        case ValueType::Bool: return "bool";
        case ValueType::String: return "string";
    }
    return "unknown";
}

using Value = std::variant<std::int64_t, double, bool, std::string>;

inline ValueType value_type_of(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) return ValueType::Int;
    if (std::holds_alternative<double>(v)) return ValueType::Double;
    if (std::holds_alternative<bool>(v)) return ValueType::Bool;
    return ValueType::String;
}

inline std::string value_to_string(const Value& v) {
    struct Visitor {
        std::string operator()(std::int64_t x) const { return std::to_string(x); }
        std::string operator()(double x) const {
            std::ostringstream out;
            out << x;
            return out.str();
        }
        std::string operator()(bool x) const { return x ? "true" : "false"; }
        std::string operator()(const std::string& x) const { return x; }
    };
    return std::visit(Visitor{}, v);
}

inline bool value_equals(const Value& a, const Value& b) {
    if (a.index() == b.index()) return a == b;

    auto is_num = [](const Value& v) {
        return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
    };

    if (is_num(a) && is_num(b)) {
        const double da = std::holds_alternative<std::int64_t>(a)
            ? static_cast<double>(std::get<std::int64_t>(a))
            : std::get<double>(a);
        const double db = std::holds_alternative<std::int64_t>(b)
            ? static_cast<double>(std::get<std::int64_t>(b))
            : std::get<double>(b);
        return da == db;
    }

    return false;
}

inline int value_compare(const Value& a, const Value& b) {
    auto is_num = [](const Value& v) {
        return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
    };

    if (is_num(a) && is_num(b)) {
        const double da = std::holds_alternative<std::int64_t>(a)
            ? static_cast<double>(std::get<std::int64_t>(a))
            : std::get<double>(a);
        const double db = std::holds_alternative<std::int64_t>(b)
            ? static_cast<double>(std::get<std::int64_t>(b))
            : std::get<double>(b);
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }

    if (a.index() != b.index()) {
        throw Error("cannot compare values of different non-numeric types");
    }

    if (std::holds_alternative<bool>(a)) {
        const bool av = std::get<bool>(a);
        const bool bv = std::get<bool>(b);
        if (av == bv) return 0;
        return av ? 1 : -1;
    }

    if (std::holds_alternative<std::string>(a)) {
        const auto& av = std::get<std::string>(a);
        const auto& bv = std::get<std::string>(b);
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }

    throw Error("unsupported comparison");
}

inline Value parse_typed_value(ValueType t, const Value& input) {
    switch (t) {
        case ValueType::Int:
            if (std::holds_alternative<std::int64_t>(input)) return input;
            if (std::holds_alternative<double>(input)) return static_cast<std::int64_t>(std::get<double>(input));
            break;
        case ValueType::Double:
            if (std::holds_alternative<double>(input)) return input;
            if (std::holds_alternative<std::int64_t>(input)) return static_cast<double>(std::get<std::int64_t>(input));
            break;
        case ValueType::Bool:
            if (std::holds_alternative<bool>(input)) return input;
            break;
        case ValueType::String:
            if (std::holds_alternative<std::string>(input)) return input;
            break;
    }

    throw Error("type mismatch: expected " + to_string(t) + ", got " + to_string(value_type_of(input)));
}

struct Column {
    std::string name;
    ValueType type;
};

struct Schema {
    std::string table_name;
    std::vector<Column> columns;
    std::unordered_map<std::string, std::size_t> index_by_name;

    void rebuild_index() {
        index_by_name.clear();
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (index_by_name.find(columns[i].name) != index_by_name.end()) {
                throw Error("duplicate column: " + columns[i].name);
            }
            index_by_name[columns[i].name] = i;
        }
    }

    std::size_t column_index(const std::string& name) const {
        auto it = index_by_name.find(name);
        if (it == index_by_name.end()) throw Error("unknown column: " + name);
        return it->second;
    }

    const Column& column(const std::string& name) const {
        return columns[column_index(name)];
    }
};

using Row = std::vector<Value>;

enum class CompareOp {
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge
};

struct Predicate {
    std::string column;
    CompareOp op;
    Value value;
};

struct InsertQuery {
    std::string table;
    std::vector<std::pair<std::string, Value>> values;
};

struct SelectQuery {
    std::string table;
    std::optional<Predicate> where;
};

struct DeleteQuery {
    std::string table;
    std::optional<Predicate> where;
};

struct UpdateQuery {
    std::string table;
    std::vector<std::pair<std::string, Value>> assignments;
    std::optional<Predicate> where;
};

using Query = std::variant<InsertQuery, SelectQuery, DeleteQuery, UpdateQuery>;

struct QueryResult {
    std::vector<std::string> columns;
    std::vector<Row> rows;
    std::size_t affected = 0;
};

class Table {
public:
    Table() = default;
    explicit Table(Schema schema) : schema_(std::move(schema)) { schema_.rebuild_index(); }

    const Schema& schema() const { return schema_; }
    const std::vector<Row>& rows() const { return rows_; }

    void insert(const std::vector<std::pair<std::string, Value>>& fields) {
        Row row(schema_.columns.size());
        std::vector<bool> assigned(schema_.columns.size(), false);

        for (const auto& [name, value] : fields) {
            std::size_t idx = schema_.column_index(name);
            row[idx] = parse_typed_value(schema_.columns[idx].type, value);
            assigned[idx] = true;
        }

        for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
            if (!assigned[i]) throw Error("missing column in insert: " + schema_.columns[i].name);
        }

        rows_.push_back(std::move(row));
    }

    QueryResult select(std::optional<Predicate> pred = std::nullopt) const {
        QueryResult out;
        for (const auto& c : schema_.columns) out.columns.push_back(c.name);
        for (const auto& row : rows_) {
            if (!pred || matches(row, *pred)) out.rows.push_back(row);
        }
        out.affected = out.rows.size();
        return out;
    }

    std::size_t erase(std::optional<Predicate> pred = std::nullopt) {
        const std::size_t old_size = rows_.size();
        rows_.erase(
            std::remove_if(rows_.begin(), rows_.end(), [&](const Row& r) {
                return !pred || matches(r, *pred);
            }),
            rows_.end());
        return old_size - rows_.size();
    }

    std::size_t update(const std::vector<std::pair<std::string, Value>>& assignments,
                       std::optional<Predicate> pred = std::nullopt) {
        std::size_t count = 0;
        for (auto& row : rows_) {
            if (pred && !matches(row, *pred)) continue;

            for (const auto& [name, value] : assignments) {
                std::size_t idx = schema_.column_index(name);
                row[idx] = parse_typed_value(schema_.columns[idx].type, value);
            }
            ++count;
        }
        return count;
    }

private:
    bool matches(const Row& row, const Predicate& pred) const {
        const std::size_t idx = schema_.column_index(pred.column);
        const Value& lhs = row[idx];
        const Value rhs = parse_typed_value(schema_.columns[idx].type, pred.value);

        switch (pred.op) {
            case CompareOp::Eq: return value_equals(lhs, rhs);
            case CompareOp::Ne: return !value_equals(lhs, rhs);
            case CompareOp::Lt: return value_compare(lhs, rhs) < 0;
            case CompareOp::Le: return value_compare(lhs, rhs) <= 0;
            case CompareOp::Gt: return value_compare(lhs, rhs) > 0;
            case CompareOp::Ge: return value_compare(lhs, rhs) >= 0;
        }
        return false;
    }

    Schema schema_;
    std::vector<Row> rows_;
};

class Database {
public:
    bool has_table(const std::string& name) const {
        return tables_.find(name) != tables_.end();
    }

    void create_table(const Schema& schema) {
        if (schema.table_name.empty()) throw Error("table name cannot be empty");
        if (has_table(schema.table_name)) throw Error("table already exists: " + schema.table_name);

        Schema s = schema;
        s.rebuild_index();
        const std::string table_name = s.table_name;
        tables_.emplace(table_name, Table(std::move(s)));
    }

    Table& table(const std::string& name) {
        auto it = tables_.find(name);
        if (it == tables_.end()) throw Error("unknown table: " + name);
        return it->second;
    }

    const Table& table(const std::string& name) const {
        auto it = tables_.find(name);
        if (it == tables_.end()) throw Error("unknown table: " + name);
        return it->second;
    }

    QueryResult execute(const Query& q) {
        return std::visit([this](const auto& qq) { return execute_one(qq); }, q);
    }

private:
    QueryResult execute_one(const InsertQuery& q) {
        table(q.table).insert(q.values);
        QueryResult r;
        r.affected = 1;
        return r;
    }

    QueryResult execute_one(const SelectQuery& q) {
        return table(q.table).select(q.where);
    }

    QueryResult execute_one(const DeleteQuery& q) {
        QueryResult r;
        r.affected = table(q.table).erase(q.where);
        return r;
    }

    QueryResult execute_one(const UpdateQuery& q) {
        QueryResult r;
        r.affected = table(q.table).update(q.assignments, q.where);
        return r;
    }

    std::unordered_map<std::string, Table> tables_;
};

namespace detail {

inline void skip_ws(dsl::ParsecInput& in) {
    while (!in.eof() && std::isspace(static_cast<unsigned char>(in.peek()))) {
        in.consume();
    }
}

inline dsl::Parser<char> symbol(char c) {
    return dsl::parser([c](dsl::ParsecInput& in) -> std::optional<char> {
        skip_ws(in);
        if (!in.eof() && in.peek() == c) {
            return in.consume();
        }
        return std::nullopt;
    });
}

inline dsl::Parser<std::string> identifier() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<std::string> {
        skip_ws(in);
        if (in.eof()) return std::nullopt;
        const char first = in.peek();
        if (!(std::isalpha(static_cast<unsigned char>(first)) || first == '_')) return std::nullopt;

        std::string out;
        out.push_back(in.consume());
        while (!in.eof()) {
            const char c = in.peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                out.push_back(in.consume());
            } else {
                break;
            }
        }
        return out;
    });
}

inline dsl::Parser<std::string> keyword(const std::string& kw) {
    return dsl::parser([kw](dsl::ParsecInput& in) -> std::optional<std::string> {
        skip_ws(in);
        const std::size_t save = in.pos;
        for (char c : kw) {
            if (in.eof() || in.peek() != c) {
                in.pos = save;
                return std::nullopt;
            }
            in.consume();
        }

        if (!in.eof()) {
            const char next = in.peek();
            if (std::isalnum(static_cast<unsigned char>(next)) || next == '_') {
                in.pos = save;
                return std::nullopt;
            }
        }

        return kw;
    });
}

inline dsl::Parser<std::string> string_literal() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<std::string> {
        skip_ws(in);
        if (in.eof() || in.peek() != '"') return std::nullopt;
        in.consume();

        std::string out;
        while (!in.eof()) {
            const char c = in.consume();
            if (c == '"') return out;
            if (c == '\\') {
                if (in.eof()) return std::nullopt;
                const char e = in.consume();
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    });
}

inline dsl::Parser<std::int64_t> integer_literal() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<std::int64_t> {
        skip_ws(in);
        const std::size_t save = in.pos;

        std::string text;
        if (!in.eof() && in.peek() == '-') text.push_back(in.consume());
        if (in.eof() || !std::isdigit(static_cast<unsigned char>(in.peek()))) {
            in.pos = save;
            return std::nullopt;
        }

        while (!in.eof() && std::isdigit(static_cast<unsigned char>(in.peek()))) {
            text.push_back(in.consume());
        }

        return static_cast<std::int64_t>(std::strtoll(text.c_str(), nullptr, 10));
    });
}

inline dsl::Parser<double> float_literal() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<double> {
        skip_ws(in);
        const std::size_t save = in.pos;

        std::string text;
        if (!in.eof() && in.peek() == '-') text.push_back(in.consume());
        if (in.eof() || !std::isdigit(static_cast<unsigned char>(in.peek()))) {
            in.pos = save;
            return std::nullopt;
        }

        while (!in.eof() && std::isdigit(static_cast<unsigned char>(in.peek()))) {
            text.push_back(in.consume());
        }

        if (in.eof() || in.peek() != '.') {
            in.pos = save;
            return std::nullopt;
        }
        text.push_back(in.consume());

        if (in.eof() || !std::isdigit(static_cast<unsigned char>(in.peek()))) {
            in.pos = save;
            return std::nullopt;
        }

        while (!in.eof() && std::isdigit(static_cast<unsigned char>(in.peek()))) {
            text.push_back(in.consume());
        }

        return std::strtod(text.c_str(), nullptr);
    });
}

inline dsl::Parser<bool> bool_literal() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<bool> {
        const std::size_t save = in.pos;
        if (keyword("true")(in)) return true;
        in.pos = save;
        if (keyword("false")(in)) return false;
        return std::nullopt;
    });
}

inline dsl::Parser<Value> value_literal() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<Value> {
        const std::size_t save = in.pos;
        const auto try_parse = [&in, save](const auto& parser, auto map_fn) -> std::optional<Value> {
            in.pos = save;
            if (auto parsed = parser(in)) return map_fn(*parsed);
            return std::nullopt;
        };
        if (auto s = try_parse(string_literal(), [](const std::string& v) { return Value{v}; })) return s;
        if (auto d = try_parse(float_literal(), [](double v) { return Value{v}; })) return d;
        if (auto i = try_parse(integer_literal(), [](std::int64_t v) { return Value{v}; })) return i;
        if (auto b = try_parse(bool_literal(), [](bool v) { return Value{v}; })) return b;
        in.pos = save;
        return std::nullopt;
    });
}

inline ValueType parse_type_name(const std::string& s) {
    if (s == "int") return ValueType::Int;
    if (s == "double") return ValueType::Double;
    if (s == "bool") return ValueType::Bool;
    if (s == "string") return ValueType::String;
    throw Error("unknown type: " + s);
}

inline dsl::Parser<CompareOp> comparison_op() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<CompareOp> {
        const std::size_t save = in.pos;
        const auto parse_token = [&in, save](std::string_view token, CompareOp op) -> std::optional<CompareOp> {
            in.pos = save;
            if (keyword(std::string(token))(in)) return op;
            return std::nullopt;
        };
        if (auto op = parse_token("==", CompareOp::Eq)) return op;
        if (auto op = parse_token("!=", CompareOp::Ne)) return op;
        if (auto op = parse_token("<=", CompareOp::Le)) return op;
        if (auto op = parse_token(">=", CompareOp::Ge)) return op;
        if (auto op = parse_token("<", CompareOp::Lt)) return op;
        if (auto op = parse_token(">", CompareOp::Gt)) return op;
        in.pos = save;
        return std::nullopt;
    });
}

inline dsl::Parser<Predicate> predicate() {
    return dsl::parser([](dsl::ParsecInput& in) -> std::optional<Predicate> {
        auto id = identifier()(in);
        if (!id) return std::nullopt;
        auto op = comparison_op()(in);
        if (!op) return std::nullopt;
        auto value = value_literal()(in);
        if (!value) return std::nullopt;
        return Predicate{*id, *op, *value};
    });
}

inline void expect_end(dsl::ParsecInput& in, const char* context) {
    skip_ws(in);
    if (!in.eof()) {
        throw Error(std::string("unexpected trailing input in ") + context);
    }
}

} // namespace detail

inline Schema parse_schema(std::string_view text) {
    dsl::ParsecInput in{text, 0};

    if (!detail::keyword("table")(in)) {
        throw Error("schema must start with 'table'");
    }

    auto table = detail::identifier()(in);
    if (!table) throw Error("expected table name");

    if (!detail::symbol('{')(in)) throw Error("expected '{' after table name");

    Schema schema;
    schema.table_name = *table;

    while (true) {
        detail::skip_ws(in);
        if (detail::symbol('}')(in)) break;

        auto col_name = detail::identifier()(in);
        if (!col_name) throw Error("expected column name");
        if (!detail::symbol(':')(in)) throw Error("expected ':' after column name");

        auto type_name = detail::identifier()(in);
        if (!type_name) throw Error("expected column type");

        Column col;
        col.name = *col_name;
        col.type = detail::parse_type_name(*type_name);

        if (!detail::symbol(';')(in)) throw Error("expected ';' after column declaration");
        schema.columns.push_back(std::move(col));
    }

    schema.rebuild_index();
    detail::expect_end(in, "schema");
    return schema;
}

inline Query parse_query(std::string_view text) {
    dsl::ParsecInput in{text, 0};

    if (detail::keyword("insert")(in)) {
        InsertQuery q;
        auto table = detail::identifier()(in);
        if (!table) throw Error("expected table name after insert");
        q.table = *table;

        if (!detail::symbol('{')(in)) throw Error("expected '{' in insert");

        while (true) {
            detail::skip_ws(in);
            if (detail::symbol('}')(in)) break;

            auto name = detail::identifier()(in);
            if (!name) throw Error("expected column name in insert");
            if (!detail::symbol(':')(in)) throw Error("expected ':' in insert assignment");
            auto value = detail::value_literal()(in);
            if (!value) throw Error("expected literal in insert assignment");

            q.values.push_back({*name, *value});

            detail::skip_ws(in);
            if (detail::symbol(',')(in)) continue;
            if (detail::symbol('}')(in)) break;
            throw Error("expected ',' or '}' in insert");
        }

        detail::expect_end(in, "insert query");
        return q;
    }

    if (detail::keyword("select")(in)) {
        SelectQuery q;
        auto table = detail::identifier()(in);
        if (!table) throw Error("expected table name after select");
        q.table = *table;

        const std::size_t save = in.pos;
        if (detail::keyword("where")(in)) {
            auto pred = detail::predicate()(in);
            if (!pred) throw Error("expected predicate after where");
            q.where = *pred;
        } else {
            in.pos = save;
        }

        detail::expect_end(in, "select query");
        return q;
    }

    if (detail::keyword("update")(in)) {
        UpdateQuery q;
        auto table = detail::identifier()(in);
        if (!table) throw Error("expected table name after update");
        q.table = *table;

        if (!detail::keyword("set")(in)) throw Error("expected 'set' in update query");

        while (true) {
            auto name = detail::identifier()(in);
            if (!name) throw Error("expected column name in update assignment");
            if (!detail::symbol('=')(in)) throw Error("expected '=' in update assignment");
            auto value = detail::value_literal()(in);
            if (!value) throw Error("expected literal value in update assignment");

            q.assignments.push_back({*name, *value});

            detail::skip_ws(in);
            if (detail::symbol(',')(in)) continue;
            break;
        }

        const std::size_t save = in.pos;
        if (detail::keyword("where")(in)) {
            auto pred = detail::predicate()(in);
            if (!pred) throw Error("expected predicate after where");
            q.where = *pred;
        } else {
            in.pos = save;
        }

        detail::expect_end(in, "update query");
        return q;
    }

    if (detail::keyword("delete")(in)) {
        DeleteQuery q;
        auto table = detail::identifier()(in);
        if (!table) throw Error("expected table name after delete");
        q.table = *table;

        const std::size_t save = in.pos;
        if (detail::keyword("where")(in)) {
            auto pred = detail::predicate()(in);
            if (!pred) throw Error("expected predicate after where");
            q.where = *pred;
        } else {
            in.pos = save;
        }

        detail::expect_end(in, "delete query");
        return q;
    }

    throw Error("unknown query");
}

inline void exec_schema(Database& db, std::string_view schema_text) {
    db.create_table(parse_schema(schema_text));
}

inline QueryResult exec_query(Database& db, std::string_view query_text) {
    return db.execute(parse_query(query_text));
}

} // namespace zethadb
