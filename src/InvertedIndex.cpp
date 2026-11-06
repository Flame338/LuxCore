#include "InvertedIndex.h"

#include <algorithm>

namespace {

const std::vector<Posting>& postings_of(const std::vector<Posting>& postings) {
    return postings;
}

const std::vector<Posting>& postings_of(const PostingList& list) {
    return list.postings;
}

int frequency_of(int frequency) {
    return frequency;
}

int frequency_of(const std::vector<std::uint32_t>& positions) {
    return static_cast<int>(positions.size());
}

std::size_t insert_posting(std::vector<Posting>& postings, int doc_id, int freq) {
    auto pos = std::ranges::lower_bound(postings, doc_id, {}, &Posting::doc_id);
    const std::size_t index = pos - postings.begin();
    postings.insert(pos, Posting{doc_id, freq});
    return index;
}

void insert_posting(PostingList& list, int doc_id, int freq, int) {
    const std::size_t index = insert_posting(list.postings, doc_id, freq);
    list.offsets.insert(list.offsets.begin() + index, PostingList::no_positions);
}

void insert_posting(PostingList& list, int doc_id, int, const std::vector<std::uint32_t>& positions) {
    const std::size_t index = insert_posting(list.postings, doc_id, static_cast<int>(positions.size()));
    list.offsets.insert(list.offsets.begin() + index, static_cast<std::uint32_t>(list.positions.size()));
    const std::size_t first = list.positions.size();
    list.positions.insert(list.positions.end(), positions.begin(), positions.end());
    if (!std::is_sorted(list.positions.begin() + first, list.positions.end())) {
        std::sort(list.positions.begin() + first, list.positions.end());
    }
}

void erase_posting(std::vector<Posting>& postings, int doc_id) {
    auto it = std::ranges::lower_bound(postings, doc_id, {}, &Posting::doc_id);
    if (it != postings.end() && it->doc_id == doc_id) {
        postings.erase(it);
    }
}

void erase_posting(PostingList& list, int doc_id) {
    auto it = std::ranges::lower_bound(list.postings, doc_id, {}, &Posting::doc_id);
    if (it == list.postings.end() || it->doc_id != doc_id) {
        return;
    }
    const std::size_t index = it - list.postings.begin();
    const std::uint32_t offset = list.offsets[index];
    const std::uint32_t length = static_cast<std::uint32_t>(it->term_frequency);
    list.postings.erase(it);
    list.offsets.erase(list.offsets.begin() + index);
    if (offset == PostingList::no_positions) {
        return;
    }
    list.positions.erase(list.positions.begin() + offset, list.positions.begin() + offset + length);
    for (std::uint32_t& other : list.offsets) {
        if (other != PostingList::no_positions && other > offset) {
            other -= length;
        }
    }
}

template <typename Map>
bool erase_term_posting(Map& map, const std::string& term, int doc_id) {
    auto it = map.find(term);
    if (it == map.end()) {
        return false;
    }
    erase_posting(it->second, doc_id);
    if (postings_of(it->second).empty()) {
        map.erase(it);
        return true;
    }
    return false;
}

const std::vector<Posting>* posting_pointer(const std::vector<Posting>& postings) {
    return &postings;
}

const std::vector<Posting>* posting_pointer(const PostingList& list) {
    return &list.postings;
}

template <typename Map>
std::vector<TermPostings> prefix_matches(const Map& map, std::string_view prefix) {
    std::vector<TermPostings> matches;
    for (auto it = map.lower_bound(prefix); it != map.end() && it->first.starts_with(prefix); ++it) {
        matches.push_back({it->first, posting_pointer(it->second)});
    }
    return matches;
}

}

template <typename TermMap>
void InvertedIndex::add_fields(int doc_id, const std::unordered_map<std::string, TermMap>& fields_to_add) {
    remove_document(doc_id);

    TermFreqs merged;
    std::vector<IndexedField>& indexed = doc_fields[doc_id];
    for (const auto& [field, term_entries] : fields_to_add) {
        if (term_entries.empty()) {
            continue;
        }

        FieldIndex& field_index = fields[field];
        field_index.documents++;

        IndexedField& entry = indexed.emplace_back(IndexedField{field, {}});
        int length = 0;
        for (const auto& [term, occurrences] : term_entries) {
            const int freq = frequency_of(occurrences);
            length += freq;
            insert_posting(field_index.terms[term], doc_id, freq, occurrences);
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

void InvertedIndex::add_document(int doc_id, const FieldTermFreqs& field_term_freqs) {
    add_fields(doc_id, field_term_freqs);
}

void InvertedIndex::add_document(int doc_id, const FieldTermPositions& field_term_positions) {
    add_fields(doc_id, field_term_positions);
}

void InvertedIndex::remove_document(int doc_id) {
    auto it = doc_fields.find(doc_id);
    if (it == doc_fields.end()) {
        return;
    }

    for (const IndexedField& entry : it->second) {
        auto field_it = fields.find(entry.field);
        for (const std::string& term : entry.terms) {
            erase_term_posting(field_it->second.terms, term, doc_id);
            if (erase_term_posting(index, term, doc_id)) {
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
    const PostingList* list = get_posting_list(field, term);
    return list ? &list->postings : nullptr;
}

const PostingList* InvertedIndex::get_posting_list(std::string_view field, const std::string& term) const {
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

std::vector<std::string_view> InvertedIndex::field_names() const {
    std::vector<std::string_view> names;
    names.reserve(fields.size());
    for (const auto& entry : fields) {
        names.push_back(entry.first);
    }
    return names;
}

IndexMemory InvertedIndex::memory_usage() const {
    IndexMemory usage{0, 0};
    for (const auto& [term, postings] : index) {
        usage.posting_bytes += postings.capacity() * sizeof(Posting);
    }
    for (const auto& [field, field_index] : fields) {
        for (const auto& [term, list] : field_index.terms) {
            usage.posting_bytes += list.postings.capacity() * sizeof(Posting);
            usage.position_bytes += (list.offsets.capacity() + list.positions.capacity()) * sizeof(std::uint32_t);
        }
    }
    return usage;
}

std::vector<FieldPostings> InvertedIndex::get_field_postings(const std::string& term) const {
    std::vector<FieldPostings> result;
    for (const auto& [field, field_index] : fields) {
        auto it = field_index.terms.find(term);
        if (it != field_index.terms.end()) {
            result.push_back({field, &it->second.postings});
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
