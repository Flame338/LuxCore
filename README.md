# LuxCore

A copy of Apache Lucene built for learning purposes

## What LuxCore is

An embeddable, zero-dependency C++20 library for instant, typo-tolerant, search-as-you-type over small local corpora such as app content, notes, game text and command palettes. It needs only the C++ standard library.

Lucene is built for servers, clusters and huge corpora. LuxCore is the small version of the same ideas: an inverted index, analyzers, BM25 scoring, fuzzy matching and highlighting, kept in memory in one process.

## What LuxCore is not

- Not a server. There is no network layer, no sharding and no replication.
- Not built for large corpora. The index lives in memory.
- Not an on-disk store. The index lives in memory. It can be saved to one file and loaded back whole, but it is not updated on disk. See Persistence.
- Not a concurrent engine. One `luxcore::Index` can be shared between threads, but all writes take turns behind a single lock. See Thread safety.
- Not a drop-in for Lucene. The names and file formats differ.

## Quick start

```cpp
luxcore::Index index;
index.add("note-1", "Shopping list", "milk, eggs, bread and butter");
for (const luxcore::Hit& hit : index.search("shoping lis")) std::cout << hit.key << "\n";
```

`make lib` builds `build/<mode>/libluxcore.a`. Compile your code with `-I<luxcore>/api`, include `LuxCore.h`, and link the library. On Linux add `-pthread` to the link line, because `Index` locks with `std::mutex` and `std::condition_variable`. `api/` holds the public headers, `LuxCore.h` for C++ and `luxcore_c.h` for C. `include/` holds internals that the library uses and embedders do not need.

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
- An index is as thread safe as the C++ class, because it wraps one. Any number of threads may call the `lux_index_*` functions on the same index at once, and writes take turns. Do not call `lux_index_free` while another thread still uses the index. Result handles are plain data. A handle that one thread holds is not shared with the others. See Thread safety.

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

## Thread safety

A GUI app can index in the background while its UI thread searches on every keystroke. `luxcore::Index` supports that. It holds one reader-writer lock around the whole index. The lock is `FairSharedMutex` in `include/FairSharedMutex.h`, a small first come, first served lock that this library ships instead of the platform's `std::shared_mutex`.

### What is safe

- `search`, `suggest`, `stored`, `size` and `save` take the lock in shared mode. Any number of threads can call them at the same time.
- `add` and `remove` take the lock in exclusive mode. One writer or many writers can call them. Writes take turns. A write waits for the searches that are running and blocks the ones that arrive after it while it runs.
- A search sees the index before a write or after it. It never sees half of a document.
- Every result is returned by value, so a `Hit`, a suggestion or a stored string stays valid after the call returns, whatever the index does next. The `Searcher` that runs a query is built inside each call and dies before the lock is released, so nothing can dangle.
- `save` holds the shared lock only while it builds the bytes. It writes the file after it lets go.
- The lock is first come, first served. A search that waits for a write waits for at most the writes that were queued before it. A write that waits for searches is never starved by searches that arrive later.
- The C API wraps `Index`, has no state of its own, and inherits all of this.

### What is not safe

- Moving, assigning or destroying an `Index`, or calling `lux_index_free`, while another thread still uses it. Stop the other threads first.
- Using the classes in `include/` (`Indexer`, `Searcher`, `InvertedIndex`) from several threads with a writer. They have no lock, and a `Searcher` holds references into its `Indexer`. A write during any read is a data race.
- Expecting fast writes next to a reader that never pauses. Reads and writes alternate, so a writer in a tight loop adds about one document per search. See the trade-offs below.

### What was already true

Concurrent reads of an unchanged `Indexer` and `Searcher` were safe before the lock, and they still are. The const methods share no mutable state. There is no `mutable` member and no static or thread local cache in `include/` or `src/`. The test `threads_concurrent_reads_on_an_unchanged_searcher_match_single_thread` runs 8 threads, half on one shared `Searcher` and half on a `Searcher` of their own, and each result must equal the single-threaded result, including scores. Its twin for `Index` does the same through the public class. Both passed before any production change, and ThreadSanitizer reports nothing for either of them on the code without the lock.

### Why a lock

| Design | Cost of a write | Complexity | Verdict |
| --- | --- | --- | --- |
| Reader-writer lock | The write holds the lock for one document. Building the 100,000 document corpus averaged about 35 µs per `add`. | One small lock class and a few lines in `Index`. Internals stay unsynchronized. | Chosen. A reader waits at most for the writes queued ahead of it. |
| Copy-on-write snapshot | Copies the whole index on every write. A 100,000 document index saves to a 37 MB file, so each `add` would copy tens of megabytes. | Readers never wait, but memory and time per write grow with the index. | Rejected. Indexing per keystroke or per file would be unaffordable. |
| Immutable segments | A write makes a small segment. Cheap for the writer and for readers. | Segment merging, tombstones for removals, and corpus statistics across segments for BM25 and fuzzy terms. This is the Lucene design. | Rejected. It is a redesign that a small, single process, in-memory library does not need. |

### Trade-offs

- A search holds the shared lock for its whole run, fuzzy matching included. A writer waits for it, about 2 ms at the 99th percentile on the 100,000 document corpus below.
- The lock is a `std::mutex` and a `std::condition_variable`. Every lock and unlock takes that mutex for a moment, and a hand-off to a waiting thread costs a wake-up. A reader and a writer that never pause alternate, one search per write. In the tight loop below the median keystroke rises from about 0.23 ms to 0.42 ms, and the writer adds 2,900 to 3,300 documents per second. One typist and a bulk indexer that reads and parses between adds do not notice.
- `FairSharedMutex` is why a bulk indexer cannot stall the UI. The platform's `std::shared_mutex` could. Glibc's read-write lock prefers readers, so readers that search without a pause starve a writer. The Windows runtime used here (winpthreads in g++ 16.1) lets a writer that retakes the lock at once keep it, so a waiting reader can wait many writes. The first lock of a fresh winpthreads `std::shared_mutex` also raced when several threads made it together. `FairSharedMutex` has no such first-use step, so the `Index` constructor does not need to take the lock once.
- `std::thread`, `std::mutex` and `std::condition_variable` need `-pthread` when linking on Linux. The Makefile passes it to every link rule. Add flags with `EXTRA_LDFLAGS`.

### Measured numbers

`make MODE=release bench ARGS="--indexing 1"` builds the 100,000 document corpus a second time as a `luxcore::Index`, then types the same keystroke list three times. Each keystroke is one `search` plus one `suggest`. The three measurements are idle, while one background thread adds documents, and idle again on the index that the writer grew. The writer adds a new document, removes the oldest one it added after every 10th add, and pauses `--writer_pause_us` microseconds (default 200, 0 is a tight loop). The bench prints how many documents the writer added, checks that the final size equals the start plus adds minus removes, and exits non-zero if it does not.

`Index` uses no stop words, BM25 and fuzzy matching, so it is slower than the `Searcher` baseline that the bench prints first (`METRIC p99_us` 345 to 351 µs over three runs, with stop words, tf-idf and no fuzzing). Compare the rows within one run.

Measured on an Intel Core i7-13700K, Windows 11, g++ 16.1.0 with `-O2 -DNDEBUG`, three runs each. The last column is the 99th percentile while writing divided by the idle one. The index grows during the measurement, and the idle-again column shows what growth alone costs.

| Writer | Documents added during the run | Idle p99 | While writing p99 | Idle again p99 | Ratio |
| --- | --- | --- | --- | --- | --- |
| Pause 200 µs (default) | 8,456 to 8,645 | 2.11 to 2.27 ms | 2.33 to 2.42 ms | 2.24 to 2.57 ms | 1.03 to 1.11 |
| Pause 50 µs | 11,179 to 11,333 | 2.18 to 2.25 ms | 2.38 to 2.52 ms | 2.37 to 2.43 ms | 1.06 to 1.14 |
| Tight loop, pause 0 | 827 to 835 | 2.35 to 2.49 ms | 2.46 to 2.68 ms | 2.32 to 2.43 ms | 1.04 to 1.11 |

The tight loop rows use `--passes 1 --queries 30`, 458 keystrokes, which take 0.3 s. The writer adds one document per search there, because the reader never pauses and the lock hands over in turn. The median keystroke takes 0.42 to 0.44 ms while the writer runs, against 0.23 to 0.25 ms idle. The paced writers add about as many documents as they did under the platform lock.

The first version of the lock was the platform's `std::shared_mutex`. With the same tight loop on this machine it gave a p99 of 99.9 to 156.5 ms while writing against 2.18 to 2.28 ms idle (ratio 45.8 to 71.1) and a median keystroke of 21 to 30 ms, while the writer added 122,620 to 163,825 documents and the index doubled. A reader that arrived while the writer kept releasing and retaking the lock waited many writes. That is why the library has its own lock.

On Ubuntu 24.04 under WSL2 (g++ 13.3, glibc 2.39, same CPU), the tight loop gave p99 ratios of 1.12, 1.02 and 1.26 in three runs, and the writer added 834 to 866 documents in the 0.3 s of the measurement.

### Tests

`tests/test_threads.cpp` holds the thread tests. `make test T=threads` builds and runs them. They are part of `make test`.

- Concurrent reads on an unchanged `Index` and on an unchanged `Searcher` equal the single-threaded results.
- The stress test starts 4 reader threads and one writer. The writer adds 3,000 documents, replaces some and removes some. The readers loop over `search`, `suggest`, `stored` and `size`. They assert that the 40 stable anchor documents are always found with the same hits and the same suggestions, that every returned key was added at some point, that scores are finite, and that `size()` stays within bounds. Worker threads only count failures in atomics. The main thread checks them. The loops have fixed bounds, so the test ends the same way every time and takes well under a second in a debug build.
- Several writers lose no document, and `save` during writes produces a file that `load` accepts.
- `tests/test_fair_mutex.cpp` tests the lock alone. Writers exclude each other and the readers. Readers share the lock. A reader that arrives after a waiting writer enters after it. A reader that arrives while a writer loops without a pause is overtaken by at most a few writes, and a writer that arrives while readers loop without a pause is overtaken by at most a few reads. The last two count operations and do not use a clock. `make test T=fair_mutex` runs them. The same tests against `std::shared_mutex` fail, on Windows by the overtaking of the reader and on Linux by the overtaking of the writer.

Without the lock these tests fail. On this machine the stress test aborted every time in debug and crashed in release, with a reader inside `FuzzyIndex::collect` reading a node index of `-1` while the writer rebuilt the trie.

To run the tests under ThreadSanitizer on Linux, use `setarch x86_64 -R make test T=threads MODE=release EXTRA_CXXFLAGS="-fsanitize=thread -g" EXTRA_CFLAGS=-fsanitize=thread EXTRA_LDFLAGS=-fsanitize=thread`. The `setarch` part turns off address randomization, which some newer kernels need for ThreadSanitizer. With the lock, ThreadSanitizer reported nothing for the full suite. Without it, the stress test produced dozens of data race reports.

## Document keys

Applications identify a document by a string key. The index identifies it by an internal `int` id, as Lucene separates the application key from the internal doc id.

- `Indexer::add_document(key, document)` assigns the next internal id the first time a key is seen. Adding under a key that already exists updates that document in place and keeps its internal id.
- Internal ids are never reused. Removing a document leaves a gap in the document store, and adding the same key again gets a fresh id.
- `add_document(document)` without a key still works and returns the internal id. Such a document has an empty key and can be removed or updated by id only.
- `remove_document` and `update_document` accept the internal id, and `remove_document` also accepts the key.
- `SearchHit` carries both the internal `doc_id` and the `key`. The key is filled when the `Searcher` is built from an `Indexer`. A `Searcher` built from a bare `InvertedIndex` and `Analyzer` leaves it empty.


## Scoring

The scorer is chosen per `Searcher` with `SearchOptions::scoring`. `Scoring::tf_idf()` is the default and uses weighted tf times the Lucene-classic idf. `Scoring::bm25_with(k1, b)` uses BM25 with k1 = 1.2 and b = 0.75 by default.

BM25 is combined with the field boosts in the BM25F style. For each field a posting contributes `boost * tf / (1 - b + b * field_length / average_field_length)`. These values are summed over fields into one weighted tf, and the term scores `idf * tf / (k1 + tf)` with `idf = ln(1 + (N - n + 0.5) / (n + 0.5))`. Field length is the number of indexed tokens in that field after analysis, and the average is over documents that have the field. Prefix completions for the word being typed keep their own saturating formula and only take the idf from the chosen scorer.

Results are collected with a bounded priority queue that keeps the best `top_k` hits, ordered by score and then by doc id.

## Queries

`Searcher` is the one way to run a search. Build it once from an `Indexer`, so hits carry keys, or from an `InvertedIndex` and an `Analyzer`, plus optional `SearchOptions`. The options are `boosts` (`FieldBoosts::standard()`), `scoring` (`Scoring::tf_idf()`) and `fuzziness` (`Fuzziness::Off`), and any of them can be set alone: `Searcher searcher(indexer, {.scoring = Scoring::bm25_with(), .fuzziness = Fuzziness::On});`.

`search(text)` and `search_in(field, text)` run typed text. `search(query)` runs a parsed or hand-built `Query` tree and returns `SearchHit`s ranked the same way, with the same field boosts and the same tf-idf or BM25 scoring. `matching_ids(query)` returns the matching doc ids in ascending order.

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
