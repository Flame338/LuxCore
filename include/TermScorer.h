#pragma once

#include "InvertedIndex.h"
#include "Scoring.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct ScoredDoc {
    int doc_id;
    double score;
};

// Elements added by resize are left uninitialized: the scorer overwrites every one, and
// zeroing a result of millions of postings first costs about as much as computing it.
template <typename T>
struct UninitializedAllocator : std::allocator<T> {
    template <typename Other>
    struct rebind {
        using other = UninitializedAllocator<Other>;
    };

    using std::allocator<T>::allocator;

    template <typename Other>
    void construct(Other* place) noexcept {
        ::new (static_cast<void*>(place)) Other;
    }

    template <typename Other, typename... Arguments>
    void construct(Other* place, Arguments&&... arguments) {
        ::new (static_cast<void*>(place)) Other(std::forward<Arguments>(arguments)...);
    }
};

using ScoredDocs = std::vector<ScoredDoc, UninitializedAllocator<ScoredDoc>>;

struct Scope {
    const InvertedIndex& index;
    const FieldBoosts& boosts;
    std::optional<std::string_view> field;

    std::size_t document_frequency(const std::string& term) const {
        if (!field) {
            return index.get_document_frequency(term);
        }
        const std::optional<Postings> found = index.get_postings(*field, term);
        return found ? found->size() : 0;
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
            if (const std::optional<Postings> found = index.get_postings(*field, term)) {
                visit(*field, *found, boosts.get(*field));
            }
            return;
        }
        for (const FieldPostings& entry : index.get_field_postings(term)) {
            visit(entry.field, entry.postings, boosts.get(entry.field));
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
    double weighted_tf(std::span<const int> lengths, int doc_id, double frequency, double boost,
                       double average_length) const;
    double average_length(std::string_view field) const;
    double combine(double term_idf, double tf) const;
    ScoredDocs score_by_doc(const std::string& term) const;

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
