#include "Searcher.h"
#include "EditDistance.h"
#include "Indexer.h"
#include "MatchPlanner.h"
#include "TermScorer.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include <queue>
#include <utility>

namespace {

constexpr double completion_weight = 0.5;

struct PrefixRun {
    const std::vector<Posting>* postings;
    double weight;
    bool exact;
};

struct PrefixExpansion {
    std::vector<PrefixRun> runs;
};

struct PrefixTally {
    int doc_id;
    double exact_tf = 0;
    double completion_tf = 0;
};

PrefixExpansion expand(const Scope& scope, const TextPlan::Prefix& prefix) {
    PrefixExpansion expansion;
    auto add_runs = [&](const std::string& term, bool exact) {
        scope.for_each_field_postings(term, [&](std::string_view, const std::vector<Posting>& postings, double weight) {
            expansion.runs.push_back({&postings, weight, exact});
        });
    };
    add_runs(prefix.term, true);
    for (const std::string& completion : prefix.completions) {
        add_runs(completion, false);
    }
    return expansion;
}

PrefixTally* merge_runs(const PrefixTally* a, const PrefixTally* a_end,
                        const PrefixTally* b, const PrefixTally* b_end, PrefixTally* out) {
    while (a != a_end && b != b_end) {
        if (a->doc_id < b->doc_id) {
            *out++ = *a++;
        } else if (b->doc_id < a->doc_id) {
            *out++ = *b++;
        } else {
            *out++ = {a->doc_id, a->exact_tf + b->exact_tf, a->completion_tf + b->completion_tf};
            ++a;
            ++b;
        }
    }
    out = std::copy(a, a_end, out);
    return std::copy(b, b_end, out);
}

std::vector<PrefixTally> merge_postings(const PrefixExpansion& expansion) {
    std::size_t total_postings = 0;
    for (const PrefixRun& run : expansion.runs) {
        total_postings += run.postings->size();
    }

    std::vector<PrefixTally> tallies(total_postings);
    std::vector<std::size_t> bounds{0};
    for (const PrefixRun& run : expansion.runs) {
        PrefixTally* out = tallies.data() + bounds.back();
        for (const Posting& posting : *run.postings) {
            const double tf = posting.term_frequency * run.weight;
            *out++ = run.exact ? PrefixTally{posting.doc_id, tf, 0} : PrefixTally{posting.doc_id, 0, tf};
        }
        bounds.push_back(bounds.back() + run.postings->size());
    }

    std::vector<PrefixTally> scratch(bounds.size() > 2 ? total_postings : 0);
    std::vector<std::size_t> next_bounds;
    while (bounds.size() > 2) {
        next_bounds.assign(1, 0);
        PrefixTally* out = scratch.data();
        for (std::size_t run = 0; run + 1 < bounds.size(); run += 2) {
            const PrefixTally* a = tallies.data() + bounds[run];
            const PrefixTally* a_end = tallies.data() + bounds[run + 1];
            if (run + 2 < bounds.size()) {
                out = merge_runs(a, a_end, a_end, tallies.data() + bounds[run + 2], out);
            } else {
                out = std::copy(a, a_end, out);
            }
            next_bounds.push_back(out - scratch.data());
        }
        tallies.swap(scratch);
        bounds.swap(next_bounds);
    }
    tallies.resize(bounds.back());
    return tallies;
}

std::vector<SearchHit> prefix_scores(const PrefixExpansion& expansion, int total_docs, const Scoring& scoring) {
    const std::vector<PrefixTally> tallies = merge_postings(expansion);
    const double prefix_idf = idf_for(scoring, total_docs, tallies.size());

    std::vector<SearchHit> hits;
    hits.reserve(tallies.size());
    for (const PrefixTally& tally : tallies) {
        if (tally.exact_tf > 0) {
            hits.push_back({tally.doc_id, tally.exact_tf * prefix_idf});
        } else {
            const double tf = tally.completion_tf;
            hits.push_back({tally.doc_id, completion_weight * tf / (tf + 1.0) * prefix_idf});
        }
    }
    return hits;
}

DocScores fuzzy_scores(const TermScorer& scorer, const std::vector<FuzzyMatch>& candidates,
                       const DocScores& exact) {
    double exact_floor = std::numeric_limits<double>::infinity();
    for (const auto& [doc_id, score] : exact) {
        exact_floor = std::min(exact_floor, score);
    }

    DocScores best;
    for (const FuzzyMatch& match : candidates) {
        const double factor = fuzzy_score_factor(match.distance);
        for (const auto& [doc_id, score] : scorer.score(match.term)) {
            if (exact.contains(doc_id)) {
                continue;
            }
            double& slot = best[doc_id];
            slot = std::max(slot, factor * std::min(score, exact_floor));
        }
    }
    return best;
}

ScoredDocs by_doc(ScoredDocs docs) {
    std::ranges::sort(docs, {}, &ScoredDoc::doc_id);
    return docs;
}

class WordQuery : public Query {
private:
    const TextPlan::Word& word;
    const std::optional<std::string>& field;

public:
    WordQuery(const TextPlan::Word& word, const std::optional<std::string>& field) : word(word), field(field) {}

    ScoredDocs execute(const QueryContext& context) const override {
        const Scope scope = context.scope(field);
        const TermScorer scorer(scope, context.index, context.scoring);
        const DocScores exact = scorer.score(word.term);

        ScoredDocs docs;
        docs.reserve(exact.size());
        for (const auto& [doc_id, score] : exact) {
            docs.push_back({doc_id, score});
        }
        if (!word.fuzzy.empty()) {
            for (const auto& [doc_id, score] : fuzzy_scores(scorer, word.fuzzy, exact)) {
                docs.push_back({doc_id, score});
            }
        }
        return by_doc(std::move(docs));
    }

    void add_leaves(QueryLeaves& leaves) const override { leaves.terms.push_back({word.term, field}); }
};

class PrefixQuery : public Query {
private:
    const TextPlan::Prefix& prefix;
    const std::optional<std::string>& field;

public:
    PrefixQuery(const TextPlan::Prefix& prefix, const std::optional<std::string>& field)
        : prefix(prefix), field(field) {}

    ScoredDocs execute(const QueryContext& context) const override {
        const Scope scope = context.scope(field);
        const std::vector<SearchHit> hits = prefix_scores(expand(scope, prefix), scope.documents(), context.scoring);

        ScoredDocs docs;
        docs.reserve(hits.size());
        for (const SearchHit& hit : hits) {
            docs.push_back({hit.doc_id, hit.score});
        }
        return docs;
    }

    void add_leaves(QueryLeaves& leaves) const override { leaves.terms.push_back({prefix.term, field}); }
};

std::unique_ptr<Query> compile(const TextPlan& plan) {
    std::vector<std::unique_ptr<Query>> leaves;
    for (const TextPlan::Word& word : plan.words()) {
        leaves.push_back(std::make_unique<WordQuery>(word, plan.field()));
    }
    if (plan.prefix()) {
        leaves.push_back(std::make_unique<PrefixQuery>(*plan.prefix(), plan.field()));
    }
    if (leaves.size() == 1) {
        return std::move(leaves.front());
    }

    auto any_of = std::make_unique<BooleanQuery>();
    for (std::unique_ptr<Query>& leaf : leaves) {
        any_of->add(Occur::Should, std::move(leaf));
    }
    return any_of;
}

}

Searcher::Searcher(const Indexer& indexer, SearchOptions options)
    : inverted_index(indexer.get_index()), analyzer(indexer.get_analyzer()), keys(&indexer.get_keys()),
      options(std::move(options)) {}

Searcher::Searcher(const InvertedIndex& index, const Analyzer& analyzer, SearchOptions options)
    : inverted_index(index), analyzer(analyzer), keys(nullptr), options(std::move(options)) {}

QueryContext Searcher::context() const { return {inverted_index, options.boosts, options.scoring}; }

std::vector<SearchHit> Searcher::finish(TopKCollector& collector) const {
    std::vector<SearchHit> results = collector.take();
    if (keys) {
        for (SearchHit& hit : results) {
            hit.key = keys->key_of(hit.doc_id);
        }
    }
    return results;
}

std::vector<Suggestion> Searcher::suggest(const std::string& query_string, std::size_t limit) const {
    return suggest_scoped(query_string, std::nullopt, limit);
}

std::vector<Suggestion> Searcher::suggest_in(std::string_view field, const std::string& query_string,
                                             std::size_t limit) const {
    return suggest_scoped(query_string, field, limit);
}

TextPlan Searcher::plan(const std::string& query_string, std::optional<std::string_view> field) const {
    return MatchPlanner::for_text(Scope{inverted_index, options.boosts, field}, analyzer, options.fuzziness,
                                  query_string);
}

std::vector<SearchHit> Searcher::search(const std::string& query_string, int top_k) const {
    return search(plan(query_string), top_k);
}

std::vector<SearchHit> Searcher::search_in(std::string_view field, const std::string& query_string, int top_k) const {
    return search(plan(query_string, field), top_k);
}

std::vector<Suggestion> Searcher::suggest_scoped(const std::string& query_string,
                                                 std::optional<std::string_view> field, std::size_t limit) const {
    const AnalyzedQuery query = analyzer.analyze_query(query_string);
    if (!query.prefix) {
        return {};
    }

    const Scope scope{inverted_index, options.boosts, field};
    std::vector<TermPostings> matches = scope.terms_with_prefix(*query.prefix);
    const auto kept = matches.begin() + std::min(limit, matches.size());
    std::partial_sort(matches.begin(), kept, matches.end(), more_common);

    std::vector<Suggestion> suggestions;
    suggestions.reserve(kept - matches.begin());
    for (auto it = matches.begin(); it != kept; ++it) {
        suggestions.push_back({std::string(it->term), it->postings->size()});
    }
    return suggestions;
}

std::vector<SearchHit> Searcher::search(const TextPlan& plan, int top_k) const {
    return search(*compile(plan), top_k);
}

std::vector<SearchHit> Searcher::search(const Query& query, int top_k) const {
    TopKCollector collector(top_k > 0 ? static_cast<std::size_t>(top_k) : 0);
    for (const ScoredDoc& doc : query.execute(context())) {
        collector.offer({doc.doc_id, doc.score});
    }
    return finish(collector);
}

std::vector<int> Searcher::matching_ids(const Query& query) const {
    std::vector<int> ids;
    for (const ScoredDoc& doc : query.execute(context())) {
        ids.push_back(doc.doc_id);
    }
    return ids;
}
