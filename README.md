# LuxCore

A copy of Apache Lucene built for learning purposes

## What LuxCore is

An embeddable, zero-dependency C++20 library for instant, typo-tolerant, search-as-you-type over small local corpora such as app content, notes, game text and command palettes. It needs only the C++ standard library.

Lucene is built for servers, clusters and huge corpora. LuxCore is the small version of the same ideas: an inverted index, analyzers, BM25 scoring, fuzzy matching and highlighting, kept in memory in one process.

## What LuxCore is not

- Not a server. There is no network layer, no sharding and no replication.
- Not built for large corpora. The index lives in memory.
- Not an on-disk store. The index lives in memory. It can be saved to one file and loaded back whole, but it is not updated on disk. See Persistence.
- Not thread safe yet. Do not search while another thread adds or removes documents.
- Not a drop-in for Lucene. The names and file formats differ.

## Quick start

```cpp
luxcore::Index index;
index.add("note-1", "Shopping list", "milk, eggs, bread and butter");
for (const luxcore::Hit& hit : index.search("shoping lis")) std::cout << hit.key << "\n";
```

`make lib` builds `build/<mode>/libluxcore.a`. Compile your code with `-I<luxcore>/api`, include `LuxCore.h`, and link the library. `api/` holds the public headers, `LuxCore.h` for C++ and `luxcore_c.h` for C. `include/` holds internals that the library uses and embedders do not need.

`make example` builds `examples/search.cpp` against `libluxcore.a` with only `api/` on the include path, then runs it. Pass a query with `make example ARGS="brea"`.

### Public API

`luxcore::Index` in `api/LuxCore.h`:

- `add(key, title, body)` adds a document under a string key. Adding an existing key replaces that document. An empty key throws `std::invalid_argument`.
- `remove(key)` returns whether the key was present. `size()` is the number of documents.
- `search(query, limit = 10)` returns `Hit`s with `key` and `score`, best first. Searches use BM25 with a boost on `title`, match the unfinished last word as a prefix, and tolerate typos in the finished words.
- `suggest(query, limit = 5)` returns completions of the unfinished last word, most common first.
- `stored(key, field)` returns the stored `title` or `body` text, or nothing when the key or field is unknown.
- `save(path)` writes the index to one file. `Index::load(path)` reads such a file and returns an index that behaves like the one that was saved. Both throw `std::runtime_error` on I/O errors and `load` also throws on a file that is not a valid index. See Persistence.

The public `Index` uses no stop words, so it never reads a file from the working directory. For stop words, highlighting, field-scoped search or query trees, use the classes in `include/` directly.

### C API

`api/luxcore_c.h` wraps `luxcore::Index` in flat C functions so that C, Python, C# and Rust can call it. It is plain C11 with `extern "C"` guards and needs no C++ headers. Link `libluxcore.a` with a C++ linker driver, or load the shared library.

- Every function that can fail returns a `lux_status`: `LUX_OK`, `LUX_INVALID_ARGUMENT`, `LUX_NOT_FOUND`, `LUX_OUT_OF_MEMORY` or `LUX_INTERNAL_ERROR`. No exception crosses the boundary. A null pointer, including a null string with length 0, gives `LUX_INVALID_ARGUMENT`, and so does an empty key.
- `lux_status_message(status)` returns static text for a status. The caller does not free it, and it is safe to call from any thread.
- Strings go in as UTF-8 `const char*` plus a `size_t` byte length. They need no NUL terminator and the library copies them before it returns.
- `lux_index_create(&index)` makes an index and `lux_index_free(index)` destroys it. The caller owns the index and frees it once.
- `lux_index_add`, `lux_index_remove` and `lux_index_size` mirror `add`, `remove` and `size`. `remove` reports whether the key was present through an `int*`.
- `lux_index_search`, `lux_index_suggest` and `lux_index_stored` return a result handle through an out pointer: `lux_hits`, `lux_strings` and `lux_text`. The caller owns each handle and frees it with `lux_hits_free`, `lux_strings_free` or `lux_text_free`. On any non-OK status the out pointer is set to null and nothing needs freeing. `lux_index_stored` returns `LUX_NOT_FOUND` when the key or field is unknown.
- Accessors return a pointer and a length into the handle: `lux_hits_key`, `lux_strings_get` and `lux_text_data` with `lux_text_length`. The pointer stays valid until the handle is freed. Copy the bytes to keep them longer. An out of range position returns null.
- The free functions accept null. Results do not depend on the index, so a result may outlive the index that made it.
- An index is not thread safe, as with the C++ class. Use one thread per index, or lock around calls.

`make shared` builds `build/<mode>/libluxcore.so`, or `libluxcore.dll` on Windows, with only the `lux_*` functions exported. The Windows DLL links the C++ runtime statically, so it depends only on system DLLs. Code that includes the header for the static library needs no macro. Code that links the Windows DLL defines `LUX_SHARED` so that the functions are imported.

`examples/python/luxcore.py` is a `ctypes` binding with an `Index` class and a demo. Run `make shared`, then `python examples/python/luxcore.py --lib build/debug/libluxcore.dll`, or set `LUXCORE_LIB` to the library path. It takes an optional query argument. `tests/c/test_c_api.c` is a C test that `make test` builds and runs.

### Size and build time

`make stats` rebuilds the release library and the example from scratch and prints the build time and file sizes. CI runs it on g++ release and adds the numbers to the job summary. On the developer machine (g++ 16.1 on Windows, one job) it measured 16.6 s, 528 KB for `libluxcore.a` and 2.9 MB for the statically linked example executable.

## Persistence

`Index::save(path)` writes the whole index to one binary file. `Index::load(path)` reads it back in a fraction of the time that building it takes.

```cpp
index.save("notes.lux");
luxcore::Index again = luxcore::Index::load("notes.lux");
```

A loaded index returns the same hits with the same scores, the same suggestions and the same stored text as the index that was saved. It stays fully usable, so `add`, replace and `remove` work after a load, and new documents get internal ids that continue after the highest id ever used. Saving does not change the index. Saving and loading an empty index works.

`save` and `load` throw `std::runtime_error`. `save` throws when the file cannot be written. `load` throws when the path is not a readable file, when the magic number is wrong, when the format version is not supported, when the file is truncated or has extra bytes, and when the content is inconsistent. The message says which. A bad file never crashes the program and never makes `load` allocate more than the file can justify. `save` writes straight to the target path, so a crash in the middle of a save can leave a truncated file, which `load` then rejects. Write to a temporary name and rename it when that matters.

The C API does not expose `save` and `load` yet.

### What is saved

The file holds the primary state only. That is the stored fields of every live document, including the gaps left by removed documents so that internal ids are never reused, the key of every keyed document, and for each field the sorted term dictionary with one posting list per term. A posting holds a document id, a term frequency and the positions of the term.

Everything else is rebuilt while loading. This covers the combined term map that `Searcher` uses, the fuzzy matching trie, the length of every field in every document, the document counts and averages that BM25 needs, and the per-document term lists that removal uses. A field length is the sum of the term frequencies in its postings, so the file does not repeat it and the two cannot disagree.

### Format

All numbers are unsigned. The 16 byte header is written little endian on every platform: the 4 bytes `LUXC` as magic number, a 32 bit format version, and a 64 bit count of the bytes that follow. The content has three sections in this order: the document store, the keys, and the field postings.

Every number in the content is a variable-length integer in LEB128 style, which takes 7 bits per byte. A string is a length followed by its UTF-8 bytes. Document ids in a posting list are stored as the gap from the previous id, and positions as the gap from the previous position, so a term that occurs in consecutive documents costs about one byte per id. Document ids in the store and in the key table are gaps too.

`load` checks every count and length against the bytes that remain before it allocates anything. It also checks that ids ascend, that every posting and every key points to a live document, that terms are sorted, and that field options are known. The one thing a file can claim without spending bytes is a removed id. A file may declare at most 65,536 removed ids plus 16 for each byte of its own size, and `load` rejects more.

The file has no checksum. Flipped bits inside text or numbers that leave the structure valid load as a different but safe index.

### Version policy

The current format version is 1. A build reads only the version it writes and throws on any other, so a file from a newer build is refused with a message that names both versions. There are no migrations yet. Any change to the layout bumps the version, and the file is only valid for the library version that wrote it.

The analyzer is not saved. The public `Index` always uses the same analyzer, and a file is only valid for the analyzer that built it. A later library version that changes tokenizing or folding can produce different terms from the same text, and queries then miss the terms in an old file. Rebuild the index from the source documents after such an upgrade. When you use `Indexer` directly, `Indexer::serialize()` returns the file as a string and `Indexer::deserialize(bytes, stop_words_path)` parses it. You must pass the same stop word file that the index was built with.

### Measured numbers

`make MODE=release bench` builds a corpus of 100,000 documents with a vocabulary of 40,000 words, saves it to a temporary file, loads it back, and compares the results of a sample of the benchmark queries (hits, scores and suggestions, with BM25) between the original and the loaded index. It prints an error and exits non-zero on any difference. The save time is `serialize` plus writing the file. The load time is reading the file plus parsing it and rebuilding the derived state.

Measured on an Intel Core i7-13700K, Windows 11, g++ 16.1.0 with `-O2 -DNDEBUG`, in seven runs:

| Step | Time | Note |
| --- | --- | --- |
| Build the index from documents | 2.63 to 2.96 s | analysis and indexing of 100,000 documents |
| Save | 66 to 102 ms | 37.3 MB file |
| Load | 211 to 244 ms | 11.5 to 13.8 times faster than building |

In one profiled run the 220 ms of a load split into about 25 ms for the stored documents, 11 ms for the keys, and 166 ms for the postings. Of the postings time, about 54 ms is parsing, 39 ms builds the per-document term entries, 33 ms fills their term lists, 15 ms builds the fuzzy trie, and the rest merges the per-field lists into the combined map. The reading of the file is the remaining 20 ms.

## Document keys

Applications identify a document by a string key. The index identifies it by an internal `int` id, as Lucene separates the application key from the internal doc id.

- `Indexer::add_document(key, document)` assigns the next internal id the first time a key is seen. Adding under a key that already exists updates that document in place and keeps its internal id.
- Internal ids are never reused. Removing a document leaves a gap in the document store, and adding the same key again gets a fresh id.
- `add_document(document)` without a key still works and returns the internal id. Such a document has an empty key and can be removed or updated by id only.
- `remove_document` and `update_document` accept the internal id, and `remove_document` also accepts the key.
- `SearchHit` carries both the internal `doc_id` and the `key`. The key is filled when the `Searcher` is given `Indexer::get_keys()`.


## Scoring

The scorer is chosen per `Searcher` with `Scoring`. `Scoring::tf_idf()` is the default and uses weighted tf times the Lucene-classic idf. `Scoring::bm25_with(k1, b)` uses BM25 with k1 = 1.2 and b = 0.75 by default.

BM25 is combined with the field boosts in the BM25F style. For each field a posting contributes `boost * tf / (1 - b + b * field_length / average_field_length)`. These values are summed over fields into one weighted tf, and the term scores `idf * tf / (k1 + tf)` with `idf = ln(1 + (N - n + 0.5) / (n + 0.5))`. Field length is the number of indexed tokens in that field after analysis, and the average is over documents that have the field. Prefix completions for the word being typed keep their own saturating formula and only take the idf from the chosen scorer.

Results are collected with a bounded priority queue that keeps the best `top_k` hits, ordered by score and then by doc id.

## Queries

`IndexSearcher` runs a `Query` tree against an `InvertedIndex` and returns `SearchHit`s ranked like `Searcher` does, with the same field boosts and the same tf-idf or BM25 scoring. `matching_ids` returns the matching doc ids in ascending order.

- `TermQuery(term, field)` matches an exact index term, so the term is not analyzed. Without a field it matches in any field.
- `BooleanQuery` holds clauses of `Occur::Must`, `Occur::Should` and `Occur::MustNot`. With at least one MUST clause the result is the intersection of the MUST clauses, minus the MUST_NOT clauses, and SHOULD clauses only add score. Without a MUST clause it is the union of the SHOULD clauses, minus the MUST_NOT clauses. A query with only MUST_NOT clauses matches nothing. Scores of matching clauses add up.
- `PhraseQuery` matches terms at the given relative positions inside one field and never across fields or across the values of a repeated field. Built from plain terms they must be adjacent. Built from `PhraseTerm`s with positions, the gaps are kept: the analyzer leaves a gap where it removes a stop word, so `"cat of hat"` matches `cat of hat` and `cat and hat` but not `cat hat`. A phrase scores as the sum of its term idfs times the weighted phrase frequency.

### Query parser

`QueryParser(analyzer).parse(text)` returns a `ParseResult` with either `query` or `error` (`message` and byte `offset`). It does not throw.

- `a b` and `a OR b` are OR. `a AND b` is AND, and AND binds tighter than OR, so `a OR b AND c` is `a OR (b AND c)`.
- `+a` is MUST, `-a` and `NOT a` are MUST_NOT, and parentheses group.
- `"two words"` is a phrase. `field:word`, `field:"two words"` and `field:(a OR b)` limit a clause to one field.
- Operators are upper case only. Words and phrases go through the analyzer, so case, accents and stop words behave as at index time. A word that analyzes to several tokens, such as `wi-fi`, becomes a phrase. A clause that analyzes to nothing is dropped. Blank input gives an empty query that matches nothing.

### Positions

The field index keeps the positions of every term occurrence next to its postings. A `PostingList` holds the sorted postings, one offset per posting, and one flat pool of positions that the offsets point into. The combined index used by `Searcher` keeps plain postings without positions. On the 100k document bench corpus the positions take 19 MB next to 38 MB of postings, and the peak working set grows from about 246 MB to 269 MB.

## Highlighting

A snippet shows exactly the words that made a document a hit. `Searcher` and `Highlighter` do not decide that separately. A query is turned into a match plan once, `Searcher` scores that plan, and `Highlighter` marks it.

`searcher.plan(query_string)` returns a `TextPlan`, and `searcher.search(plan)` scores it. `searcher.search(query_string)` is the same as `searcher.search(searcher.plan(query_string))`. `searcher.plan(query_string, field)` plans a search limited to one field, like `search_in`. `Highlighter(options).snippet(stored_text, plan)` returns the part of a stored text that best shows why it matched, with the matching words wrapped in markers. `snippet(document, field, plan)` does the same for a stored field and returns an empty string when the document has no such field. `field` only picks the stored value. The field scope of a text plan comes from `Searcher::plan`.

- `HighlightOptions::length` is the snippet length in Unicode code points and defaults to 160. Markers and ellipsis are not counted, and a cut never falls inside a UTF-8 sequence.
- `start_marker` and `end_marker` default to `<em>` and `</em>`. `ellipsis` defaults to `...` and is added at each edge where the text was cut. An empty string turns it off.
- A plan carries the `Analyzer` that made it. The stored text goes through `Analyzer::analyze_tokens` of that analyzer, so query terms and text tokens are always analyzed the same way. A plan keeps a pointer to the analyzer, so the analyzer must outlive the plan. Marks cover the original bytes through the token start and end offsets, so `cafe` marks `Café`.
- Tokens that overlap or touch are merged into one mark, so the CJK query `东京` marks both characters in one span and an identifier kept whole next to its parts is marked once.
- A text that fits in `length` comes back whole. Otherwise the window starts a quarter of `length` before a match, and the window covering the most distinct query terms wins, then the most matches, then the earliest. With no match the snippet is the beginning of the text, unmarked.
- A match cut by the window edge is marked up to the edge. An empty query or an empty text gives the text or an empty string, unmarked.

### What a text plan matches

A text token is marked when its term is in the plan. The plan holds exactly the terms the search scores:

- Each full query word, as it stands.
- The unfinished last word as a term, plus the completions the search kept. A one code point prefix keeps none, so `s` marks only the word `s`. A two code point prefix keeps the 4 most common completions, and a longer one keeps the 32 most common. Common means held by the most documents in the planned field scope, then by term. The prefix term and its completions count as one term when the window is chosen, unless the prefix is also a full query word, which then owns them.
- With `Fuzziness::On` on the `Searcher`, the fuzzy candidates of each full query word. A word of up to 2 code points has none, 3 to 5 allow one edit, and 6 or more allow two. Of the terms in the index within that limit, the search keeps the 20 nearest per query word, ordered by distance and then by how many documents hold them, and only those are marked. A candidate counts as the query word it is nearest to, the earlier word on a tie, and an exact or prefix match always wins over a fuzzy one. The unfinished last word is never fuzzed.

`Fuzziness` is a setting of the `Searcher` and has one meaning for both: the plan holds fuzzy candidates only when the `Searcher` that made it fuzzes, and whoever scores that plan scores them.

A plan is made from one index, so a word that is not in the index can only be marked when it is a query word itself. A typo in the stored text is marked when it is in the index, which holds when the text is a document of the index.

### Query trees

`make_match_plan(analyzer, query, field)` returns a `MatchPlan` for a parsed `Query` tree, so structure from `QueryParser` is respected. `field` is optional. A `TextPlan` is a `MatchPlan`, so `Highlighter` takes either.

- A `TermQuery` marks text tokens whose term equals its term. The term is an index term and is compared as it is, without analysis, so `TermQuery("Fox")` marks nothing.
- A `BooleanQuery` contributes its `Must` and `Should` clauses. A `MustNot` clause is skipped together with its whole subtree, so `fox -(bear OR wolf)` marks only `fox`. A nested group such as `(a OR b) AND c` marks all three.
- A `PhraseQuery` follows the same position rules as search: relative positions, a gap where a stop word was removed, and no slop. Only complete occurrences are marked, so `"quick brown"` leaves a lone `quick` unmarked. Each marked word keeps its own span, and the words of one occurrence count as one term when the window is chosen.
- When `field` is given, a `TermQuery` or `PhraseQuery` on another field contributes nothing. A query without a field matches in any field, and when `field` is not given every term counts whatever its query field.
- A query tree has no unfinished word and no fuzziness, so `searc` parsed into a query marks only the word `searc`.
- A `Query` subclass adds its leaves in `add_leaves`. One that adds none contributes nothing.

Choosing the window is one two-pointer pass over the matches, so a large text with many matches stays cheap. A text of 1.2 MB with 300,000 matches took about 35 ms in a release build. Known limits: the snippet is the text as stored, so it can start or end inside a word. Only one window is returned, never several fragments. A `length` of 0 returns only the ellipsis.
