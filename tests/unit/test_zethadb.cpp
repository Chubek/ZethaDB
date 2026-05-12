#include "../../ZethaDB.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("parse schema accepts valid definition") {
    auto schema = zethadb::parse_schema("table users { id: int; name: string; active: bool; }");
    REQUIRE(schema.table_name == "users");
    REQUIRE(schema.columns.size() == 3);
}

TEST_CASE("parse schema rejects duplicate columns") {
    REQUIRE_THROWS_AS(
        zethadb::parse_schema("table users { id: int; id: int; }"),
        zethadb::Error);
}

TEST_CASE("parse schema rejects unknown type") {
    REQUIRE_THROWS_AS(
        zethadb::parse_schema("table users { id: uuid; }"),
        zethadb::Error);
}

TEST_CASE("parse schema rejects malformed punctuation") {
    REQUIRE_THROWS_AS(
        zethadb::parse_schema("table users { id int; }"),
        zethadb::Error);
}

TEST_CASE("parse insert query") {
    auto q = zethadb::parse_query("insert users { id: 1, name: \"alice\" }");
    auto* iq = std::get_if<zethadb::InsertQuery>(&q);
    REQUIRE(iq != nullptr);
    REQUIRE(iq->table == "users");
    REQUIRE(iq->values.size() == 2);
}

TEST_CASE("parse select where query") {
    auto q = zethadb::parse_query("select users where id >= 3");
    auto* sq = std::get_if<zethadb::SelectQuery>(&q);
    REQUIRE(sq != nullptr);
    REQUIRE(sq->where.has_value());
}

TEST_CASE("parse update query") {
    auto q = zethadb::parse_query("update users set active = false where id == 3");
    auto* uq = std::get_if<zethadb::UpdateQuery>(&q);
    REQUIRE(uq != nullptr);
    REQUIRE(uq->assignments.size() == 1);
    REQUIRE(uq->where.has_value());
}

TEST_CASE("parse delete query") {
    auto q = zethadb::parse_query("delete users where id == 2");
    auto* dq = std::get_if<zethadb::DeleteQuery>(&q);
    REQUIRE(dq != nullptr);
    REQUIRE(dq->where.has_value());
}

TEST_CASE("runtime insert then select") {
    zethadb::Database db;
    zethadb::exec_schema(db, "table users { id: int; name: string; }");
    zethadb::exec_query(db, "insert users { id: 1, name: \"alice\" }");
    auto result = zethadb::exec_query(db, "select users");
    REQUIRE(result.rows.size() == 1);
}

TEST_CASE("runtime update then select") {
    zethadb::Database db;
    zethadb::exec_schema(db, "table users { id: int; name: string; }");
    zethadb::exec_query(db, "insert users { id: 1, name: \"alice\" }");
    auto updated = zethadb::exec_query(db, "update users set name = \"bob\" where id == 1");
    REQUIRE(updated.affected == 1);
    auto selected = zethadb::exec_query(db, "select users where id == 1");
    REQUIRE(std::get<std::string>(selected.rows[0][1]) == "bob");
}

TEST_CASE("runtime delete then select") {
    zethadb::Database db;
    zethadb::exec_schema(db, "table users { id: int; }");
    zethadb::exec_query(db, "insert users { id: 1 }");
    auto deleted = zethadb::exec_query(db, "delete users where id == 1");
    REQUIRE(deleted.affected == 1);
    auto selected = zethadb::exec_query(db, "select users");
    REQUIRE(selected.rows.empty());
}

TEST_CASE("runtime rejects missing field") {
    zethadb::Database db;
    zethadb::exec_schema(db, "table users { id: int; name: string; }");
    REQUIRE_THROWS_AS(
        zethadb::exec_query(db, "insert users { id: 1 }"),
        zethadb::Error);
}

TEST_CASE("whitespace-heavy input parses") {
    zethadb::Database db;
    zethadb::exec_schema(db, " table users  {  id: int;  active: bool; } ");
    zethadb::exec_query(db, " insert users { id: 1, active: true } ");
    auto result = zethadb::exec_query(db, " select users where id == 1 ");
    REQUIRE(result.rows.size() == 1);
}
