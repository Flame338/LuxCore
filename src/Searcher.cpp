#include "Searcher.h"
#include "Indexer.h"
#include "MatchPlanner.h"
#include "TermScorer.h"
#include <algorithm>
#include <memory>
#include <queue>
#include <utility>

namespace {

class WordQuery : public Query {
private:
    const TextPlan::Word& word;
    const std::optional<std::string>& field;

public:
    WordQuery(const TextPlan::Word& word, const std::optional<std::string>& field) : word(word), field(field) {}

    ScoredDocs execute(const QueryContext& context) const override {
        const Scope scope = context.scope(field);
        const TermScorer scorer(scope, context.scoring);
        ScoredDocs docs = scorer.score(word.term);
        if (!word.fuzzy.empty()) {
            const std::size_t exact_count = docs.size();
            const ScoredDocs fuzzy = scorer.score_fuzzy(word.fuzzy, docs);
            docs.insert(docs.end(), fuzzy.begin(), fuzzy.end());
            std::ranges::inplace_merge(docs, docs.begin() + exact_count, {}, &ScoredDoc::doc_id);
        }
        return docs;
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
        return TermScorer(scope, context.scoring).score_prefix(prefix.term, prefix.completions);
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
        suggestions.push_back({std::string(it->term), it->document_frequency});
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
