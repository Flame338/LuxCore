#include "Query.h"

#include <algorithm>
#include <unordered_map>

namespace {

ScoredDocs intersect(const ScoredDocs& a, const ScoredDocs& b) {
    ScoredDocs result;
    auto left = a.begin();
    auto right = b.begin();
    while (left != a.end() && right != b.end()) {
        if (left->doc_id < right->doc_id) {
            ++left;
        } else if (right->doc_id < left->doc_id) {
            ++right;
        } else {
            result.push_back({left->doc_id, left->score + right->score});
            ++left;
            ++right;
        }
    }
    return result;
}

constexpr std::size_t skew_ratio = 8;

ScoredDocs unite_skewed(const ScoredDocs& small, const ScoredDocs& large) {
    ScoredDocs result;
    result.reserve(small.size() + large.size());
    auto rest = large.begin();
    for (const ScoredDoc& doc : small) {
        const auto found = std::ranges::lower_bound(rest, large.end(), doc.doc_id, {}, &ScoredDoc::doc_id);
        result.insert(result.end(), rest, found);
        if (found != large.end() && found->doc_id == doc.doc_id) {
            result.push_back({doc.doc_id, found->score + doc.score});
            rest = found + 1;
        } else {
            result.push_back(doc);
            rest = found;
        }
    }
    result.insert(result.end(), rest, large.end());
    return result;
}

ScoredDocs unite(const ScoredDocs& a, const ScoredDocs& b) {
    if (a.size() * skew_ratio <= b.size()) {
        return unite_skewed(a, b);
    }
    if (b.size() * skew_ratio <= a.size()) {
        return unite_skewed(b, a);
    }
    ScoredDocs result;
    result.reserve(a.size() + b.size());
    auto left = a.begin();
    auto right = b.begin();
    while (left != a.end() && right != b.end()) {
        if (left->doc_id < right->doc_id) {
            result.push_back(*left++);
        } else if (right->doc_id < left->doc_id) {
            result.push_back(*right++);
        } else {
            result.push_back({left->doc_id, left->score + right->score});
            ++left;
            ++right;
        }
    }
    result.insert(result.end(), left, a.end());
    result.insert(result.end(), right, b.end());
    return result;
}

ScoredDocs subtract(const ScoredDocs& a, const ScoredDocs& b) {
    ScoredDocs result;
    auto right = b.begin();
    for (const ScoredDoc& doc : a) {
        while (right != b.end() && right->doc_id < doc.doc_id) {
            ++right;
        }
        if (right == b.end() || right->doc_id != doc.doc_id) {
            result.push_back(doc);
        }
    }
    return result;
}

ScoredDocs add_scores_of_matching(const ScoredDocs& base, const ScoredDocs& optional) {
    ScoredDocs result = base;
    auto right = optional.begin();
    for (ScoredDoc& doc : result) {
        while (right != optional.end() && right->doc_id < doc.doc_id) {
            ++right;
        }
        if (right != optional.end() && right->doc_id == doc.doc_id) {
            doc.score += right->score;
        }
    }
    return result;
}

bool contains_position(std::span<const std::uint32_t> positions, std::size_t position) {
    return std::ranges::binary_search(positions, static_cast<std::uint32_t>(position));
}

int phrase_frequency(const std::vector<PhraseTerm>& terms, const std::vector<std::span<const std::uint32_t>>& positions) {
    int frequency = 0;
    for (const std::uint32_t start : positions.front()) {
        bool matches = true;
        for (std::size_t i = 1; i < terms.size() && matches; i++) {
            matches = contains_position(positions[i], start + terms[i].position);
        }
        frequency += matches ? 1 : 0;
    }
    return frequency;
}

std::vector<std::string_view> fields_of(const QueryContext& context, const std::optional<std::string>& field) {
    if (field) {
        return {*field};
    }
    return context.index.field_names();
}

}

ScoredDocs TermQuery::execute(const QueryContext& context) const {
    const Scope scope = context.scope(field);
    return TermScorer(scope, context.scoring).score(term);
}

void TermQuery::add_leaves(QueryLeaves& leaves) const {
    leaves.terms.push_back({term, field});
}

BooleanQuery& BooleanQuery::add(Occur occur, std::unique_ptr<Query> query) {
    clauses.push_back({occur, std::move(query)});
    return *this;
}

ScoredDocs BooleanQuery::execute(const QueryContext& context) const {
    std::vector<ScoredDocs> must;
    std::vector<ScoredDocs> should;
    std::vector<ScoredDocs> must_not;
    for (const BooleanClause& clause : clauses) {
        ScoredDocs matches = clause.query->execute(context);
        switch (clause.occur) {
            case Occur::Must: must.push_back(std::move(matches)); break;
            case Occur::Should: should.push_back(std::move(matches)); break;
            case Occur::MustNot: must_not.push_back(std::move(matches)); break;
        }
    }

    ScoredDocs result;
    if (must.empty()) {
        for (ScoredDocs& matches : should) {
            result = result.empty() ? std::move(matches) : unite(result, matches);
        }
    } else {
        std::ranges::sort(must, {}, [](const ScoredDocs& matches) { return matches.size(); });
        result = std::move(must.front());
        for (std::size_t i = 1; i < must.size() && !result.empty(); i++) {
            result = intersect(result, must[i]);
        }
        for (const ScoredDocs& matches : should) {
            result = add_scores_of_matching(result, matches);
        }
    }
    for (const ScoredDocs& matches : must_not) {
        result = subtract(result, matches);
    }
    return result;
}

void BooleanQuery::add_leaves(QueryLeaves& leaves) const {
    for (const BooleanClause& clause : clauses) {
        if (clause.occur != Occur::MustNot) {
            clause.query->add_leaves(leaves);
        }
    }
}

PhraseQuery::PhraseQuery(std::vector<PhraseTerm> terms, std::optional<std::string> field)
    : terms(std::move(terms)), field(std::move(field)) {
    std::ranges::stable_sort(this->terms, {}, &PhraseTerm::position);
    if (!this->terms.empty()) {
        const std::size_t first = this->terms.front().position;
        for (PhraseTerm& term : this->terms) {
            term.position -= first;
        }
    }
}

PhraseQuery::PhraseQuery(const std::vector<std::string>& terms, std::optional<std::string> field)
    : field(std::move(field)) {
    for (std::size_t i = 0; i < terms.size(); i++) {
        this->terms.push_back({terms[i], i});
    }
}

void PhraseQuery::add_leaves(QueryLeaves& leaves) const {
    leaves.phrases.push_back({terms, field});
}

ScoredDocs PhraseQuery::execute(const QueryContext& context) const {
    if (terms.empty()) {
        return {};
    }

    const Scope scope = context.scope(field);
    const TermScorer scorer(scope, context.scoring);

    std::vector<std::string> words;
    for (const PhraseTerm& phrase_term : terms) {
        words.push_back(phrase_term.term);
    }
    TermScorer::PhraseTally tally = scorer.phrase(words);
    for (const std::string_view field_name : fields_of(context, field)) {
        std::vector<PostingList> lists;
        for (const PhraseTerm& phrase_term : terms) {
            const std::optional<PostingList> list = context.index.get_posting_list(field_name, phrase_term.term);
            if (!list) {
                break;
            }
            lists.push_back(*list);
        }
        if (lists.size() != terms.size()) {
            continue;
        }

        const std::size_t lead = std::ranges::min_element(lists, {}, [](const PostingList& list) {
                                     return list.postings.size();
                                 }) - lists.begin();
        std::vector<std::span<const std::uint32_t>> positions(lists.size());

        for (const Posting candidate : lists[lead].postings) {
            bool present = true;
            for (std::size_t i = 0; i < lists.size() && present; i++) {
                const Postings& postings = lists[i].postings;
                const std::size_t found = postings.lower_bound(candidate.doc_id);
                present = found != postings.size() && postings.doc_id(found) == candidate.doc_id;
                if (present) {
                    positions[i] = lists[i].positions_of(found);
                }
            }
            if (!present) {
                continue;
            }
            const int frequency = phrase_frequency(terms, positions);
            if (frequency > 0) {
                tally.add(field_name, candidate.doc_id, frequency);
            }
        }
    }

    return tally.scores();
}
