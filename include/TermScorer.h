#pragma once

#include "FuzzyIndex.h"
#include "InvertedIndex.h"
#include "Scoring.h"

#include <cstddef>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ScoredDoc {
    int doc_id;
    double score;
};

using ScoredDocs = std::vector<ScoredDoc>;

struct Scope {
    const InvertedIndex& index;
    const FieldBoosts& boosts;
    std::optional<std::string_view> field;

    const std::vector<Posting>* postings(const std::string& term) const {
        return field ? index.get_postings(*field, term) : index.get_postings(term);
    }

    std::vector<TermPostings> terms_with_prefix(std::string_view prefix) const {
        return field ? index.terms_with_prefix(*field, prefix) : index.terms_with_prefix(prefix);
    }

    int documents() const {
        return field ? index.get_field_documents(*field) : index.get_total_documents();
    }

    template <typename Visit>
    void for_each_field_postings(const std::string& term, Visit&& visit) const {
        if (field) {
            if (const std::vector<Posting>* found = index.get_postings(*field, term)) {
                visit(*field, *found, boosts.get(*field));
            }
            return;
        }
        for (const FieldPostings& entry : index.get_field_postings(term)) {
            visit(entry.field, *entry.postings, boosts.get(entry.field));
        }
    }
};

bool ranks_before(const SearchHit& a, const SearchHit& b);

class TopKCollector {
private:
    std::size_t capacity;
    std::priority_queue<SearchHit, std::vector<SearchHit>, decltype(&ranks_before)> worst_on_top{&ranks_before};

public:
    explicit TopKCollector(std::size_t capacity) : capacity(capacity) {}

    void offer(SearchHit hit);
    std::vector<SearchHit> take();
};

class TermScorer {
private:
    using DocScores = std::unordered_map<int, double>;

    const Scope& scope;
    const Scoring& scoring;
    int total_docs;

    double idf(std::size_t doc_freq) const;
    double term_idf(const std::string& term) const;
    double weighted_tf(std::string_view field, int doc_id, double frequency) const;
    double weighted_tf(std::string_view field, int doc_id, double frequency, double boost, double average_length) const;
    double average_length(std::string_view field) const;
    double combine(double term_idf, double tf) const;
    DocScores score_by_doc(const std::string& term) const;

public:
    class PhraseTally {
    private:
        friend class TermScorer;

        const TermScorer& scorer;
        double idf;
        DocScores weighted_tfs;

        PhraseTally(const TermScorer& scorer, double idf) : scorer(scorer), idf(idf) {}

    public:
        void add(std::string_view field, int doc_id, int frequency);
        ScoredDocs scores() const;
    };

    TermScorer(const Scope& scope, const Scoring& scoring)
        : scope(scope), scoring(scoring), total_docs(scope.documents()) {}

    ScoredDocs score(const std::string& term) const;
    ScoredDocs score_prefix(const std::string& term, const std::vector<std::string>& completions) const;
    ScoredDocs score_fuzzy(const std::vector<FuzzyMatch>& candidates, const ScoredDocs& exact) const;
    PhraseTally phrase(const std::vector<std::string>& terms) const;
};
