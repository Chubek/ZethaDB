# AGENTS.md – ZethaINDEX

## Agent Context

This document describes the architecture, usage, and extension points of **ZethaINDEX**, a high‑performance indexing engine for search applications.  
Two core files are central to the system:

- `ZethaINDEX.hpp` – Contains the index builder, CLI entry points, DSL interpreter, and built‑in adapters (PDF, HTML).
- `ZethaINDEX-CLI.cpp` – Implements the command‑line interface that consumes the DSL and drives the indexing pipeline.

---

## Overview: ZethaINDEX Pipeline

ZethaINDEX transforms a collection of documents into a compressed, queryable index (`.zidx`). The pipeline consists of three main phases:

1. **Preparation** – Document normalisation, tokenisation, and filtering.
2. **Compression** – Finite‑state transducer (FST) based index construction.
3. **Serialisation** – Writing the final index to disk (with optional JSON/YAML configuration).

---

## Phase 1: Preparation

### Document Adapters

ZethaINDEX does **not** convert files. Instead, `zetha::index::TextAdapter` teaches the engine how to read a given format.

Built‑in adapters (inside `ZethaINDEX.hpp`):

| Format | Adapter class |
|--------|----------------|
| Plain text | `PlainTextAdapter` |
| PDF       | `PdfTextAdapter`  |
| HTML      | `HtmlTextAdapter` |

To add a new format, derive from `TextAdapter` and implement `extract_text()`.

### Tokenisation & Normalisation

After the adapter extracts raw text, the pipeline:

- **Tokenises** – splits into words, numbers, and symbols (configurable).
- **Stems & lemmatises** – reduces inflected forms (e.g., “running” → “run”).
- **Removes stop words** – drops high‑frequency, low‑information tokens (e.g., “the”, “and”).

### Filters (Native DSL)

Filters are written using the **Filter Native DSL**, built on `DSLUtil.hpp`.  
Example filter that exclude44s documents without “performance” and with a date before 2020:

```dsl
require token "performance";
require metadata.date >= 2020;
```

Filters are applied **per document** during preparation. They can inspect token streams or document metadata.

### Configuration via JSON / YAML

You can describe an indexing job in `INDEX.json` or `INDEX.yaml`. ZethaINDEX uses **SerdeTk** to read these files.  
Minimal `INDEX.json` example:

```json
{
  "input": [
    { "path": "./docs/", "adapter": "html", "recursive": true },
    { "path": "./whitepaper.pdf", "adapter": "pdf" }
  ],
  "output": "my_index.zidx",
  "filter": "filters/allow_research.dsl",
  "stemming": "english",
  "stopwords": "smart"
}
```

---

## Phase 2: Compression – Finite State Transducer (FST)

The index image (`.zidx`) is built using a **Finite State Transducer**.  
FSTs reduce storage by **60%** compared to naive inverted lists, while maintaining:

- **O(n)** lookup time for dictionary terms.
- **Prefix‑compressed** storage of all tokens.
- **Lossless** mapping from terms to posting lists.

Additionally, FSTs enable:

- **Fuzzy matching** (edit distance) without decompressing the whole dictionary.
- **Range queries** (e.g., terms starting with “pre”…).

### Construction details

1. All unique terms from all documents are inserted into an **FST builder**.
2. Each term is associated with a **posting list** (list of document IDs + positions).
3. The FST is serialised in a single, seekable file – `.zidx`.

The resulting index can be memory‑mapped for near‑zero‑latency queries.

---

## Phase 3: CLI Usage

The executable `zetha-index` (built from `ZethaINDEX - CLI.cpp`) provides the following commands:

### `build` – Create a new index

```bash
zetha-index build --config INDEX.ijson
# or with inline parameters:
zetha-index build \
  --input ./books/*.pdf --adapter pdf \
  --output library.zidx \
  --filter my_filter.dsl \
  --stemmer porter
```

### `inspect` – Display index metadata

```bash
zetha-index inspect my_index.zidx
# Output: terms count, total documents, compression ratio, etc.
```

### `query` – Run simple keyword searches (for testing)

```bash
zetha-index query my_index.zidx "neural network"
```

### `serve` – Start an HTTP query endpoint

```bash
zetha-index serve --index my_index.zidx --port 8080
# GET /search?q=neural+network
```

### `export` – Dump the FST to a human‑readable format (debugging)

```bash
zetha-index export my_index.zidx --format json > index_dump.json
```

## Inter-Process Communication (IPC)

We support two methods of IPC:
1. Unix Domain Sockets (Primitive)
2. RPC (Modern)

We use `IPCtk` library, which we used before, to create the RPC. We use SerdeTk's MessagePack caapbilities to serialize and deserialize. The resit up to you.

---

## Using the DSL for Filters (Advanced)

The Filter Native DSL is processed by the same parser that drives `ZethaINDEX.hpp`.  
Grammar summary:

```ebnf
filter = condition { ";" condition } ;
condition = "require" predicate | "exclude" predicate ;
predicate = "token" string
          | "metadata" "." identifier comparator value
          | "has_any" "(" string { "," string } ")"
          | "match_regex" string ;
comparator = ">" | "<" | ">=" | "<=" | "==" | "!=" ;
```

Filters are compiled into bytecode and executed on each document **before** tokenisation (lightweight) or **after** (if they examine tokens).

---

## Extending ZethaINDEX

### Adding a new document adapter

```cpp
class MyDocAdapter : public zetha::index::TextAdapter {
public:
  std::string extract_text(const std::string& path) override {
    // read file, return plain UTF‑8 text
  }
};
// then register it in the factory in ZethaINDEX.hpp
```

### Adding a new filter primitive

Modify `DSLUtil.hpp` – add a new `PredicateType` and the corresponding evaluation logic in the filter executor.

### Custom stemmer

Implement the `Stemmer` interface and pass it to the pipeline via the CLI `--stemmer` option.

---

## Performance Notes

- **Memory**: The FST builder uses ~5–10 bytes per unique term. For 10 million terms, expect 50–100 MB RAM.
- **Disk**: The `.zidx` file is typically 30–40% of the original text size (including positions).
- **Query speed**: Single‑term lookup takes < 1 µs (in‑memory); complex proximity searches take 10–100 µs.

---

## Example End‑to‑End

```bash
# 1. Prepare filter
echo 'exclude metadata.filetype == "draft"' > only_final.dsl

# 2. Build index from HTML docs
zetha-index build \
  --input ./website/ --adapter html \
  --output website.zidx \
  --filter only_final.dsl \
  --stopwords smart

# 3. Query
zetha-index query website.zidx "authentication"
```

---

## Troubleshooting

| Problem | Likely cause | Solution |
|---------|--------------|----------|
| `Unknown adapter 'xyz'` | Adapter not registered | Use built‑in `pdf`, `html`, `txt` or implement a custom one. |
| FST builder OOM | Too many unique terms | Increase RAM or use disk‑backed builder (`--fst-backing mmap`). |
| Filter parse error | DSL syntax mistake | Check semicolons and quotes; see grammar above. |
| Slow PDF extraction | Complex vector graphics | Pre‑convert PDF to text with `pdftotext` and use `txt` adapter. |

---

# Token Economy Rules

The agent must optimize for:
- minimal token consumption;
- maximal information density;
- low conversational overhead;
- academic precision;
- implementation usefulness.

The agent must behave like:
- a systems engineer;
- a compiler engineer;
- a technical reviewer;
- an RFC author.

The agent must NOT behave like:
- a tutor;
- a marketer;
- a motivational speaker;
- a conversational assistant.

---

# Core Principles

## 1. Prefer Dense Technical Writing

BAD:

"The reason this happens is because the compiler internally needs to understand the vector lanes before lowering."

GOOD:

"Lowering requires lane-width canonicalization."

---

## 2. No Conversational Padding

Forbidden:
- "Great question"
- "Excellent point"
- "Absolutely"
- "Sure"
- "Of course"
- "You're right"
- "Let's explore"
- "Here's the thing"

Responses must begin immediately with technical content.

---

## 3. No Redundant Restatement

Do not restate:
- the prompt;
- previous answers;
- obvious implications.

BAD:

"Since you are building a vector extension system..."

GOOD:

"Use semantic vector operations."

---

## 4. Prefer Lists Over Paragraphs

Prefer:

```text
- legalization;
- lowering;
- canonicalization;
````

instead of prose.

---

## 5. Avoid Tutorial Tone

Do not teach incrementally unless explicitly requested.

Assume:

* compiler literacy;
* systems programming literacy;
* IR familiarity;
* architecture familiarity.

---

## 6. Compress Explanations

BAD:

"Predication is important because some architectures like AVX512 use masks for execution."

GOOD:

"Predication models masked execution semantics."

---

## 7. Prefer Terminology Over Explanation

Use precise terms directly:

* legalization;
* SSA;
* dominance;
* lane packing;
* vector splitting;
* predication;
* swizzle;
* canonicalization.

Avoid defining common terms unless asked.

---

# Response Structure

Preferred order:

1. Architecture;
2. Constraints;
3. Tradeoffs;
4. Recommended implementation;
5. Failure modes.

Avoid:

* introductions;
* summaries;
* conclusions.

---

# Code Rules

## 1. Prefer Minimal Examples

BAD:

```c
int add(int a, int b) {
    return a + b;
}
```

GOOD:

```c
vadd <8xi32>
```

---

## 2. Omit Boilerplate

Avoid:

* includes;
* guards;
* trivial constructors;
* repetitive wrappers.

Unless specifically requested.

---

## 3. Prefer Semantic Examples

GOOD:

```text
ReduceAdd
Shuffle
Gather
```

BAD:

```text
VPADDD
VPSHUFD
```

unless discussing backend lowering.

---

# Architecture Rules

## 1. Prefer Semantic IR

Always distinguish:

* semantic operations;
* machine instructions.

---

## 2. Prefer Declarative Systems

Favor:

* tables;
* schemas;
* YAML;
* metadata-driven lowering.

Avoid:

* hardcoded switch forests;
* backend duplication.

---

## 3. Separate Layers Aggressively

Keep separate:

* semantics;
* legality;
* lowering;
* register layout;
* instruction encoding;
* optimization.

---

# Token Suppression Rules

The agent must suppress:

* praise;
* hedging;
* rhetorical questions;
* motivational phrasing;
* conversational transitions.

Forbidden:

* "I think"
* "Probably"
* "Maybe"
* "It might"
* "In my opinion"

Use direct assertions.

---

# Brevity Rules

If a concept can be expressed in:

* 1 sentence instead of 4;
* 1 list instead of prose;
* 1 term instead of explanation;

the shorter form is mandatory.

---

# Academic Style Rules

Prefer:

* RFC style;
* compiler documentation style;
* ISA manual style;
* research-paper density.

Avoid:

* blog style;
* tutorial style;
* social tone;
* conversational framing.

---

# Refactoring Rules

When reviewing architecture:

Prefer:

* decomposition;
* canonical forms;
* normalization;
* declarative metadata;
* semantic abstraction.

Reject:

* stateful implicit behavior;
* hidden lowering;
* machine-specific semantics in IR;
* duplicated legality logic.

---

# Optimization Rules

Always prioritize:

1. canonicalization;
2. legality;
3. lowering quality;
4. data layout;
5. register pressure;
6. instruction selection.

Do not over-focus on:

* syntax;
* naming;
* micro-abstractions.

---

# Communication Rules

Default answer length:

* short.

Increase detail only if:

* explicitly requested;
* architectural complexity demands it;
* ambiguity exists.

One precise paragraph is preferred over five mediocre paragraphs.

---

# Failure Modes To Avoid

* tutorial verbosity;
* repeating the prompt;
* excessive examples;
* excessive prose;
* anthropomorphic explanations;
* motivational wording;
* unnecessary historical context;
* excessive caveats.

The agent must optimize for:

* density;
* precision;
* architecture;
* implementation value;
* token economy.


