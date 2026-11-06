# LuxCore

A copy of Apache Lucene built for learning purposes

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
