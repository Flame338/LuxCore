# Index memory and build speed

This page records what changed in the index core on the `perf/index-memory` branch and the measurements behind it. The work started from a real report: Recall, a terminal search tool built on LuxCore, held 4.8 GB of RAM after it indexed a whole `C:\` drive, and indexing used one of 24 CPU threads.

Every number is measured unless it is marked inferred.

## Setup

| Item | Value |
| --- | --- |
| Machine | Intel Core i7-13700K, 16 cores, 24 threads, 31.8 GB RAM, Windows 11 Pro |
| Compiler | g++ 16.1.0 (w64devkit), `-std=c++20 -O2 -DNDEBUG`, `make shared MODE=release` |
| Driver | Recall's headless `recall bench` command, which calls the C API in `api/luxcore_c.h`, and a small C++ latency harness that loads either DLL |
| Frozen corpus | 30,662 source and text files, 296 MB, copied once so that it cannot change between runs |
| Large corpus | A full `C:\` drive: 156,255 files and 41.9M postings before, 288,255 files and 67.2M postings after |
| Index config | Positions off, BM25, fuzzy matching on |

"Old" is the core before this branch. "New" is this branch. For the frozen corpus both were built from the same checkout, with only the index core differing, and runs alternated old, new, old, new.

## Results

### Same corpus, old core against new

Frozen corpus, bodies indexed but not stored, one thread.

| | Old | New | Change |
| --- | --- | --- | --- |
| Private memory after indexing | 456 MB | 183 MB | 2.5x less |
| Memory held by the index itself | 387 MB | 114 MB | 3.4x less |
| Commit time, tokenizing done beforehand | 6.55 s | 1.19 s | 5.5x faster |
| Add time including tokenizing | 9.3 to 10.5 s | 4.1 s | 2.4x faster |
| Load a saved index | 0.71 s | 0.41 s | 1.7x faster |
| Saved file | 266,122,563 bytes | 266,122,563 bytes | byte-identical |

### Search latency, same corpus

148 queries, 50 repeats each, 5 alternating runs. Values are milliseconds. "Repeated" is the steady state while a user keeps typing.

| Kind | Old p50 | New p50 | Old p95 | New p95 |
| --- | --- | --- | --- | --- |
| Fuzzy search, first run | 0.140 | 0.126 | 2.51 | 2.30 |
| Fuzzy search, repeated | 0.0640 | 0.0573 | 1.010 | 0.977 |
| Query-syntax search, repeated | 0.0149 | 0.0118 | 0.122 | 0.105 |
| Suggest, repeated | 0.0019 | 0.0013 | 0.0097 | 0.0070 |
| Snippet, repeated | 1.43 | 1.32 | 6.02 | 5.00 |

One cell is not better: suggest p95 on the first run is 0.0198 ms new against 0.0179 ms old, and the ranges across runs overlap.

### Results are identical

- Top-20 keys and exact scores match for all 561 query and mode pairs on the frozen corpus, in each of 5 runs.
- The same holds on the Rust standard library sources (2,255 files) with positions off and with positions on, including phrase queries.
- The old and new cores write byte-identical index files, so the format version did not change and a file saved by the old core still loads.

### The full drive

These two runs are not the same corpus, so compare the per-posting figures.

| | Before | After |
| --- | --- | --- |
| Files, postings | 156,255 files, 41.9M | 288,255 files, 67.2M |
| Index structures in memory, stored text excluded | 2,049 MB | 929 MB |
| Bytes per posting, all index structures | 48.9 | 13.8 |
| Stored file bodies in memory | 1,832 MB | 4 MB (titles only) |
| Extra memory at the peak of a save | 5,664 MB | 134 MB |
| Extra memory at the peak of a load | 2,277 MB | 61 MB |
| Save time | 3.4 s | 1.05 s |
| LuxCore work per file while indexing | 0.64 ms | 0.21 ms |

Where the memory was before, on 156,255 files:

| Structure | Heap |
| --- | --- |
| Stored text pool (every file body) | 1,832 MB |
| Fuzzy trie nodes | 512 MB |
| Posting vectors | 379 MB |
| Per-field term `std::map` nodes | 352 MB |
| Per-document term pointers, kept for removal | 324 MB |
| Document-frequency `std::map` | 226 MB |
| Fuzzy trie term strings | 161 MB |

Where it is now, on 288,255 files:

| Structure | Heap |
| --- | --- |
| Posting blocks | 423 MB |
| Per-document list ids, kept for removal | 101 MB |
| Posting list headers | 91 MB |
| Trie nodes | 68 MB |
| Term entries | 61 MB |
| Term text, stored once | 58 MB |
| Document keys | 57 MB |
| Term hash slots | 44 MB |

## What changed

1. **One term dictionary.** Each term used to be stored three times: as a key in a document-frequency `std::map`, as a key in a per-field `std::map`, and as a string in the fuzzy trie. Now each distinct term has a dense 32-bit id. Its text is stored once in a single pool. A hash table gives exact lookup, and one trie gives ordered prefix listing and fuzzy candidates. See `include/TermDictionary.h` and `include/TermTrie.h`.
2. **Compact postings.** A list with a single posting lives inline in its header, which covers about two thirds of all lists. Longer lists grow in blocks. Removal keeps 32-bit list ids per document in place of 8-byte string pointers. See `include/PostingStore.h`.
3. **Bodies are optional.** A caller can index a body without storing it (`lux_index_prepare` without `LUX_PREPARE_STORE_BODY`) and highlight text it supplies itself (`lux_index_snippet_text`). The output is byte-identical to `lux_index_snippet` on a stored body with the same text.
4. **Prepare and commit.** `lux_index_add` did tokenizing inside the write lock. It is now split into `lux_index_prepare`, which takes no lock and is safe from many threads at once, and `lux_index_commit`, a short step under the write lock. Committing in the same order as serial adds gives a byte-identical index.
5. **Streaming save and load.** Save used to build the whole file in one buffer and then copy it into a second one. It now writes through a bounded buffer. Load used to read the whole file into a string beside the index it was building. It now maps the file.

## What it took to keep search latency level

The first like-for-like run showed the new core up to 15% slower on query-syntax search and on suggest. The fixes:

- The per-posting callback did not inline. Replaced by a visitor that hands the scorer plain typed runs.
- Merge buffers were zero-filled before being overwritten. Replaced by an allocator that leaves elements uninitialized.
- Prefix listing pushed every trie node onto a vector. Replaced by a walk that stays off the stack along single-child chains and sizes its result once.
- The trie root had a long sibling chain. A 256-entry table indexed by first byte replaced about 60 dependent loads per lookup.
- Tokenizing a 100 KB body reallocated its token vector repeatedly. One `reserve` fixed it.

## A bug worth remembering

One optimization kept a scratch vector in a `thread_local`. The test suite passed. Recall then hung every time it unloaded the DLL. A `thread_local` with a destructor in a DLL that links libstdc++ statically registers a per-thread cleanup key, and deleting that key while the library unloads deadlocked inside winpthreads. A debugger backtrace showed the stuck thread in `pthread_key_delete` under `DllMainCRTStartup`. The vector is now an ordinary local. The lesson: avoid adding a `thread_local` with a destructor to this library, and test unloading from the host process, not only from the library's own tests.

## Tests

534 C++ tests and 38 C tests pass. New in this branch:

- A randomized add, replace and remove sequence checked against a plain reference model (`tests/test_index_model.cpp`).
- Dictionary churn checked against a sorted set and brute-force edit distance (`tests/test_term_dictionary.cpp`).
- Load fuzzing against a guard page: every truncation offset, byte flips at every offset, and the memory-mapped path (`tests/test_load_fuzz.cpp`).
- Golden files written by the old core that must load and re-serialize to identical bytes (`tests/test_format_compat.cpp`).
- Prepare from 8 threads while another thread searches and commits (`tests/test_threads.cpp`, `tests/test_prepare.cpp`).

## Known limits

- An exception in the middle of an add, such as an allocation failure, leaves that one document half-applied. The old core behaved the same way.
- A saved file that another process truncates in place while it is mapped can fault. Replacing the file by rename is safe.
- Posting data is about 5 bytes per posting. Delta-encoding document ids would shrink it further at the cost of a different access pattern.

## Reproduce

```
make -j16 shared test MODE=release BUILD=build/perf
```

Then, from a Recall checkout on its `perf/index-memory` branch:

```
cargo build --release
target\release\recall.exe bench <ROOT> --cache <empty dir> --lib <path to libluxcore.dll> --queries bench\queries.txt --out run.json
python tools\bench_summary.py run.json --memory
python tools\diff_results.py before.json after.json
```

`lux_index_debug_stats` returns the per-structure memory rows and phase clocks that the tables above come from.
