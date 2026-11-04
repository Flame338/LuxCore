#include "InvertedIndex.h"

#include <algorithm>

void InvertedIndex::add_document(int doc_id, const std::unordered_map<std::string, int>& term_freqs) {
    remove_document(doc_id);

    std::vector<std::string>& terms = doc_terms[doc_id];
    for (const auto& [term, freq] : term_freqs) {
        std::vector<Posting>& postings = index[term];
        auto pos = std::ranges::lower_bound(postings, doc_id, {}, &Posting::doc_id);
        postings.insert(pos, Posting{doc_id, freq});
        terms.push_back(term);
    }
    total_documents++;
}

void InvertedIndex::remove_document(int doc_id) {
    auto it = doc_terms.find(doc_id);
    if (it == doc_terms.end()) {
        return;
    }

    for (const std::string& term : it->second) {
        std::vector<Posting>& postings = index[term];
        std::erase_if(postings, [doc_id](const Posting& p) { return p.doc_id == doc_id; });
        if (postings.empty()) {
            index.erase(term);
        }
    }
    doc_terms.erase(it);
    total_documents--;
}

const std::vector<Posting>* InvertedIndex::get_postings(const std::string& term) const {
    auto it = index.find(term);
    if (it != index.end()) {
        return &(it->second);
    }
    return nullptr;
}
