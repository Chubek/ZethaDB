# Chapter 5 - Parser Design with DSLUtils

ZethaDB uses `DSLUtils.hpp` parser primitives (`dsl::ParsecInput`, `dsl::ExpectedResult`, `dsl::parser`) and composes small token parsers:
- whitespace skipper
- keyword parser
- symbol parser
- identifier parser
- literal parsers
- predicate parser

Schema and query parse functions orchestrate these combinators and then build AST values.
