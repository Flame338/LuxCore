#include "InvertedIndex.h"

#include <algorithm>

namespace {

void insert_posting(std::vector<Posting>& postings, int doc_id, int freq) {
    auto pos = std::ranges::lower_bound(postings, doc_id, {}, &Posting::doc_id);
    postings.insert(pos, Posting{doc_id, freq});
}

template <typename Map>
bool erase_posting(Map& map, const std::string& term, int doc_id) {
    auto it = map.find(term);
    if (it == map.end()) {
        return false;
    }
    std::erase_if(it->second, [doc_id](const Posting& p) { return p.doc_id == doc_id; });
    if (it->second.empty()) {
        map.erase(it);
        return true;
    }
    return false;
}

template <typename Map>
std::vector<TermPostings> prefix_matches(const Map& map, std::string_view prefix) {
    std::vector<TermPostings> matches;
    for (auto it = map.lower_bound(prefix); it != map.end() && it->first.starts_with(prefix); ++it) {
        matches.push_back({it->first, &it->second});
    }
    return matches;
}

}

void InvertedIndex::add_document(int doc_id, const FieldTermFreqs& field_term_freqs) {
    remove_document(doc_id);

    TermFreqs merged;
    std::vector<IndexedField>& indexed = doc_fields[doc_id];
    for (const auto& [field, term_freqs] : field_term_freqs) {
        if (term_freqs.empty()) {
            continue;
        }

        FieldIndex& field_index = fields[field];
        field_index.documents++;

        IndexedField& entry = indexed.emplace_back(IndexedField{field, {}});
        int length = 0;
        for (const auto& [term, freq] : term_freqs) {
            length += freq;
            insert_posting(field_index.terms[term], doc_id, freq);
            entry.terms.push_back(term);
            merged[term] += freq;
        }
        field_index.lengths[doc_id] = length;
        field_index.total_length += length;
    }

    for (const auto& [term, freq] : merged) {
        std::vector<Posting>& postings = index[term];
        if (postings.empty()) {
            fuzzy.add(term);
        }
        insert_posting(postings, doc_id, freq);
    }
    total_documents++;
}

void InvertedIndex::remove_document(int doc_id) {
    auto it = doc_fields.find(doc_id);
    if (it == doc_fields.end()) {
        return;
    }

    for (const IndexedField& entry : it->second) {
        auto field_it = fields.find(entry.field);
        for (const std::string& term : entry.terms) {
            erase_posting(field_it->second.terms, term, doc_id);
            if (erase_posting(index, term, doc_id)) {
                fuzzy.remove(term);
            }
        }
        if (auto length_it = field_it->second.lengths.find(doc_id); length_it != field_it->second.lengths.end()) {
            field_it->second.total_length -= length_it->second;
            field_it->second.lengths.erase(length_it);
        }
        if (--field_it->second.documents == 0) {
            fields.erase(field_it);
        }
    }
    doc_fields.erase(it);
    total_documents--;
}

const std::vector<Posting>* InvertedIndex::get_postings(const std::string& term) const {
    auto it = index.find(term);
    if (it != index.end()) {
        return &(it->second);
    }
    return nullptr;
}

const std::vector<Posting>* InvertedIndex::get_postings(std::string_view field, const std::string& term) const {
    auto field_it = fields.find(field);
    if (field_it == fields.end()) {
        return nullptr;
    }
    auto it = field_it->second.terms.find(term);
    if (it != field_it->second.terms.end()) {
        return &(it->second);
    }
    return nullptr;
}

std::vector<FieldPostings> InvertedIndex::get_field_postings(const std::string& term) const {
    std::vector<FieldPostings> result;
    for (const auto& [field, field_index] : fields) {
        auto it = field_index.terms.find(term);
        if (it != field_index.terms.end()) {
            result.push_back({field, &it->second});
        }
    }
    return result;
}

std::vector<TermPostings> InvertedIndex::terms_with_prefix(std::string_view prefix) const {
    return prefix_matches(index, prefix);
}

std::vector<TermPostings> InvertedIndex::terms_with_prefix(std::string_view field, std::string_view prefix) const {
    auto field_it = fields.find(field);
    if (field_it == fields.end()) {
        return {};
    }
    return prefix_matches(field_it->second.terms, prefix);
}

int InvertedIndex::get_field_documents(std::string_view field) const {
    auto it = fields.find(field);
    return it == fields.end() ? 0 : it->second.documents;
}

int InvertedIndex::get_field_length(std::string_view field, int doc_id) const {
    auto field_it = fields.find(field);
    if (field_it == fields.end()) {
        return 0;
    }
    auto it = field_it->second.lengths.find(doc_id);
    return it == field_it->second.lengths.end() ? 0 : it->second;
}

double InvertedIndex::get_average_field_length(std::string_view field) const {
    auto it = fields.find(field);
    if (it == fields.end() || it->second.documents == 0) {
        return 0.0;
    }
    return static_cast<double>(it->second.total_length) / it->second.documents;
}
