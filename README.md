# LuxCore

A copy of Apache Lucene built for learning purposes

## Document keys

Applications identify a document by a string key. The index identifies it by an internal `int` id, as Lucene separates the application key from the internal doc id.

- `Indexer::add_document(key, document)` assigns the next internal id the first time a key is seen. Adding under a key that already exists updates that document in place and keeps its internal id.
- Internal ids are never reused. Removing a document leaves a gap in the document store, and adding the same key again gets a fresh id.
- `add_document(document)` without a key still works and returns the internal id. Such a document has an empty key and can be removed or updated by id only.
- `remove_document` and `update_document` accept the internal id, and `remove_document` also accepts the key.
- `SearchHit` carries both the internal `doc_id` and the `key`. The key is filled when the `Searcher` is given `Indexer::get_keys()`.
