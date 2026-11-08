# Latency budget results (PSE-297)

Run with `make MODE=release build/release/luxbench.exe`, then `./build/release/luxbench.exe --indexing 1 --fuzzy 1 --passes 5 --removes 0` for keystroke latency and `./build/release/luxbench.exe --scaling 1 --passes 3 --removes 0` for threads. Both are deterministic for seed 12345. Every number below is a median of 7 (latency) or 5 (scaling) runs, with the old and new binaries alternated.

## Setup

| Item | Value |
| --- | --- |
| Machine | Intel Core i7-13700K, 16 cores, 24 threads, 31.8 GB RAM, Windows 11 Pro |
| Compiler | g++ 16.1.0 (w64devkit), `-std=c++20 -O2 -DNDEBUG`, `make MODE=release` |
| Corpus | 100,000 documents, 40,000 vocabulary words, 39,898 indexed terms, Zipf word frequencies, titles of 2 to 4 words and bodies of 20 to 40 words |
| Workload | 150 typed queries of 1 to 3 words, 2,174 keystrokes, each keystroke runs `search(limit 8)` and `suggest(limit 5)` on the public `luxcore::Index` (BM25, fuzzy matching on, no stop words) |
| Index build | 2.4 s from scratch. Save 68 ms, load 201 ms (37.3 MB file), results identical after load |

## Keystroke latency, one thread, idle index

| | Before | After |
| --- | --- | --- |
| p50 | 143 us | 115 us |
| p95 | 1163 us | 742 us |
| p99 | 2019 us | 1111 us |

The target was p99 under 5 ms. The slowest keystrokes were 2 to 3 letter prefixes such as "bo" and "sto", whose cost was allocating and merging tens of thousands of postings. Fuzzy matching of finished words set the median: with fuzzy off the median was 25 us.

## Threads

| Readers | Keystrokes per second before | after |
| --- | --- | --- |
| 1 | 3,032 | 4,342 |
| 4 | 11,295 | 17,552 |
| 8 | 20,780 | 33,161 |
| 16 | 29,928 | 48,454 |

With one background writer adding a document every 200 us, 8 readers drop from 33,161 to 14,431 keystrokes per second and their p50 rises from 122 us to 281 us. The reader and writer lock is not the limit: a cheaper lock and analysing documents before taking the write lock were both measured and made no difference. Readers wait for the writer because a queued write holds back every later read.
