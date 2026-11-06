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

`Highlighter(analyzer, options).snippet(stored_text, query_string)` returns the part of a stored text that best shows why it matched, with the matching words wrapped in markers. `snippet(document, field, query_string)` does the same for a stored field and returns an empty string when the document has no such field.

- `HighlightOptions::length` is the snippet length in Unicode code points and defaults to 160. Markers and ellipsis are not counted, and a cut never falls inside a UTF-8 sequence.
- `start_marker` and `end_marker` default to `<em>` and `</em>`. `ellipsis` defaults to `...` and is added at each edge where the text was cut. An empty string turns it off.
- The query goes through `Analyzer::analyze_query_tokens` and the stored text through `Analyzer::analyze_tokens`. A text token is marked when its term equals a query term, or starts with the unfinished last word of the query, as `Searcher::search` expands it. Marks cover the original bytes through the token start and end offsets, so `cafe` marks `Café`.
- Tokens that overlap or touch are merged into one mark, so the CJK query `东京` marks both characters in one span and an identifier kept whole next to its parts is marked once.
- A text that fits in `length` comes back whole. Otherwise the window starts a quarter of `length` before a match, and the window covering the most distinct query terms wins, then the most matches, then the earliest. With no match the snippet is the beginning of the text, unmarked.
- A match cut by the window edge is marked up to the edge. An empty query or an empty text gives the text or an empty string, unmarked.

`snippet(stored_text, query, field)` and `snippet(document, field, query)` take a parsed `Query` tree instead of a query string, so structure from `QueryParser` is respected. `field` in the first overload is optional.

- A `TermQuery` marks text tokens whose term equals its term. The term is an index term and is compared as it is, without analysis, so `TermQuery("Fox")` marks nothing.
- A `BooleanQuery` contributes its `Must` and `Should` clauses. A `MustNot` clause is skipped together with its whole subtree, so `fox -(bear OR wolf)` marks only `fox`. A nested group such as `(a OR b) AND c` marks all three.
- A `PhraseQuery` follows the same position rules as search: relative positions, a gap where a stop word was removed, and no slop. Only complete occurrences are marked, so `"quick brown"` leaves a lone `quick` unmarked. Each marked word keeps its own span, and the words of one occurrence count as one term when the window is chosen.
- When `field` is given, a `TermQuery` or `PhraseQuery` on another field contributes nothing. A query without a field matches in any field, and when `field` is not given every term counts whatever its query field. The document overload passes the name of the field it reads.
- The unfinished-word prefix of the string overload has no counterpart here, so `searc` parsed into a query marks only the word `searc`.
- A `Query` subclass other than these three contributes nothing.

Choosing the window is one two-pointer pass over the matches, so a large text with many matches stays cheap. A text of 1.2 MB with 300,000 matches took about 35 ms in a release build. Known limits: the snippet is the text as stored, so it can start or end inside a word. Only one window is returned, never several fragments. Words that only a fuzzy search matched are not marked. A `length` of 0 returns only the ellipsis.
