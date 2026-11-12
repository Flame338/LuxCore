# LuxCore

Embeddable full-text search library in C++20. Ranked, typo-tolerant search-as-you-type with snippets and a C API.

LuxCore indexes small local collections, such as notes, app content or file names, and searches them while the user types. It completes the last word, tolerates typos, ranks results with BM25 and highlights the words that matched. It needs only the C++ standard library.

It follows the design of Apache Lucene at a smaller scale. The index lives in memory, in one process.

## What it is not

- A server. It has no network layer, sharding or replication.
- A store for large collections. The whole index is held in memory. You can save it to one file and load it back.
- A drop-in replacement for Lucene. The names and the file format differ.

## Build

You need make and a C++20 compiler (g++ or clang).

| Command | What it does |
| --- | --- |
| `make lib` | Builds the static library `build/debug/libluxcore.a`. |
| `make shared` | Builds `libluxcore.so`, or `libluxcore.dll` on Windows, for the C API. |
| `make test` | Builds and runs the C++ and C tests. `make test T=threads` runs only tests whose name contains `threads`. |
| `make example` | Builds and runs `examples/search.cpp`. `make example ARGS="brea"` searches for `brea`. |
| `make MODE=release bench` | Runs the benchmarks. |

Add `MODE=release` to any command for an optimized build in `build/release`.

To use LuxCore in your project, add `-I<luxcore>/api`, include `LuxCore.h` and link `libluxcore.a`. On Linux, also link with `-pthread`.

## Quick start

```cpp
#include "LuxCore.h"

luxcore::Index index;
index.add("note-1", "Shopping list", "milk, eggs, bread and butter");

for (const luxcore::Hit& hit : index.search("shoping lis")) {
    std::cout << hit.key << "\n";
}
```

This prints `note-1`. The search treats `lis` as an unfinished word and completes it to `list`, and it matches `shoping` to `shopping` despite the typo.

## Query syntax

`search` is for text a user is typing. `search_query` is for exact queries:

```cpp
luxcore::Index index(luxcore::IndexConfig{.positions = true});
index.add("note-2", "Reading list", "a book about how Lucene ranks search results");

auto hits = index.search_query("title:list +\"search results\" -train");
auto text = index.snippet_query("note-2", "body", "\"search results\"");
```

`text` holds `a book about how Lucene ranks <em>search</em> <em>results</em>`.

| Syntax | Meaning |
| --- | --- |
| `a b` or `a OR b` | Either word. |
| `a AND b` | Both words. `AND` binds tighter than `OR`. |
| `+a` | `a` must match. |
| `-a` or `NOT a` | `a` must not match. |
| `(a OR b)` | Groups clauses. |
| `"two words"` | The words must appear together, in that order. |
| `title:word` | Searches only one field, `title` or `body`. |

Operators are upper case. Phrases need an index built with `IndexConfig{.positions = true}`. Query syntax matches whole words only, with no completion and no typo tolerance. A malformed query throws `luxcore::QueryError`, and its `offset()` is the byte position of the problem.

## API

Everything public is in `api/LuxCore.h`. Each document has a string key, a title and a body.

| Method | What it does |
| --- | --- |
| `add(key, title, body)` | Adds a document. If the key exists, it replaces that document. |
| `remove(key)` | Removes a document and returns whether the key existed. |
| `size()` | Returns the number of documents. |
| `search(text, limit = 10)` | Returns hits, best first. Each hit has a `key` and a `score`. |
| `suggest(text, limit = 5)` | Returns completions of the last word, most common first. |
| `search_query(text, limit = 10)` | Runs a query in the syntax above. |
| `stored(key, field)` | Returns the stored `title` or `body`, or nothing if the key is unknown. |
| `snippet(key, field, text)` | Returns the part of a stored field that matched `text`, with the matched words marked. `snippet_query` does the same for query syntax. |
| `snippet_text(body, text)` | Does the same for text you pass in, such as a file you read yourself. |
| `prepare(title, body)` | Analyzes a document. It is safe to call from many threads at once. |
| `commit(key, prepared)` | Adds a prepared document. `prepare` and `commit` together split `add` in two, so you can analyze in parallel. |
| `save(path)` | Writes the index to one file. |
| `Index::load(path)` | Reads a saved file back into a new index. |

`SnippetOptions` sets the snippet `length` in characters (default 160), the `start_marker` and `end_marker` (default `<em>` and `</em>`) and the `ellipsis` (default `...`).

An empty key throws `std::invalid_argument`. File errors throw `luxcore::IoError`, and a file that is not a valid index throws `luxcore::FormatError`. Both derive from `std::runtime_error`.

## Saving and loading

```cpp
index.save("notes.lux");
luxcore::Index again = luxcore::Index::load("notes.lux");
```

A loaded index gives the same results as the one you saved, and you can keep adding to it. On 100,000 documents, loading took about 11 times less time than building the index again.

- `load` drops phrase positions. Pass `IndexConfig{.positions = true}` to keep them.
- A damaged or truncated file throws `FormatError`. It never crashes the program.
- A file only loads in the library version that wrote it. Rebuild the index after an upgrade.
- `save` writes straight to the path. To be safe from a crash during a save, write to a temporary name and rename it.

## Threads

One `Index` can be shared between threads.

- Reads run in parallel. These are `search`, `search_query`, `suggest`, `snippet`, `stored`, `size` and `save`.
- Writes take turns. These are `add`, `remove` and `commit`. A search sees the index before a write or after it, never in between.
- The lock is first come, first served, so a bulk indexer cannot freeze a search box.
- Do not move or destroy an `Index` while another thread uses it.
- The classes in `include/` have no lock. Use them from one thread only.

## C API

`api/luxcore_c.h` exposes the same functions to C, and through C to Python, C# or Rust. `lux_index_search` mirrors `search`, and so on.

- Every call returns a `lux_status`. `lux_status_message(status)` turns it into text. No C++ exception crosses into C.
- Strings go in as a UTF-8 pointer and a byte length, with no NUL terminator needed.
- Results come back as handles (`lux_hits`, `lux_strings`, `lux_text`). Free each one with its matching `_free` function. When a call fails, the result pointer is null and there is nothing to free.
- Link `libluxcore.a` with a C++ linker, or load the shared library. Code that links the Windows DLL must define `LUX_SHARED`.

`examples/python/luxcore.py` is a Python binding built on `ctypes`. Run `make shared`, then `python examples/python/luxcore.py --lib build/debug/libluxcore.dll`.

## Project layout

| Path | Contents |
| --- | --- |
| `api/` | The public headers. This is the supported interface. |
| `include/`, `src/` | Internals. They can change without notice. |
| `tests/` | The C++ tests, and the C tests in `tests/c/`. |
| `examples/` | The C++ example and the Python binding. |
| `bench/` | The benchmark and its recorded results. |
| `docs/DESIGN.md` | How it works inside: the file format, scoring, query and highlighting rules, locking, and benchmark numbers. |
