---
title: LuxCore
line: A search library you can embed, with BM25 ranking and typo tolerance
language: C++20
status: finished
featured: true
keywords: [search, library, lucene, bm25, index, memory, fuzzy, typo, cpp, c++, engine]
source: https://github.com/Flame338/LuxCore
numbers:
  - value: 1.1 ms
    caption: for the slowest 1% of keystrokes on 100,000 documents. It was 2.0 ms.
  - value: 3.4×
    caption: less index memory, from 387 MB to 114 MB. The saved index is byte-for-byte the same.
  - value: 733 KB
    caption: for the release library, with no dependencies beyond the C++ standard library.
summary: A full-text search library you can embed, modeled on Apache Lucene. It has an inverted index, BM25 ranking, search-as-you-type that forgives typos, and a query parser with booleans and phrases.
---

## What it is

LuxCore indexes small local collections in memory: notes, app content, file names. As you type, it completes the last word, tolerates a typo, and highlights the words that matched. The index saves to one binary file, and loading checks that file before allocating anything.

Other languages reach it through a C API. There is a Python example in the repo.

## The hard part

The slowest keystrokes were the shortest ones. A two-letter prefix matches thousands of words, and the search was allocating and merging tens of thousands of postings to answer it.

I changed it to merge the sorted postings pairwise and to pick the top results without sorting everything. That took the slowest 1% of keystrokes from 2.0 ms to 1.1 ms.

bench/RESULTS.md · 100,000 documents

|     | before   | after    |
| --- | -------- | -------- |
| p50 | 143 µs   | 115 µs   |
| p95 | 1,163 µs | 742 µs   |
| p99 | 2,019 µs | 1,111 µs |

## Where I left it

LuxCore is finished. It does what I set out to build, and I'm not adding to it.

One limit stays. Searching while another thread indexes is slower than searching alone. With one writer adding a document every 200 microseconds, eight readers drop from 33,161 keystrokes a second to 14,431. A cheaper lock made no difference, because a queued write blocks the reads behind it.
