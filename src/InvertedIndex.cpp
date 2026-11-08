#include "InvertedIndex.h"

#include <algorithm>

namespace {

int frequency_of(int frequency) {
    return frequency;
}

int frequency_of(const std::vector<std::uint32_t>& positions) {
    return static_cast<int>(positions.size());
}

template <typename T>
void make_room_for(std::vector<T>& values, std::size_t extra) {
    if (values.size() + extra > values.capacity()) {
        values.reserve(values.size() + std::max(extra, std::max<std::size_t>(1, values.size() / 4)));
    }
}

std::size_t insert_posting(std::vector<Posting>& postings, int doc_id, int freq) {
    make_room_for(postings, 1);
    auto pos = std::ranges::lower_bound(postings, doc_id, {}, &Posting::doc_id);
    const std::size_t index = pos - postings.begin();
    postings.insert(pos, Posting{doc_id, freq});
    return index;
}

void insert_posting(PostingList& list, int doc_id, int freq, int) {
    const std::size_t index = insert_posting(list.postings, doc_id, freq);
    if (!list.offsets.empty()) {
        make_room_for(list.offsets, 1);
        list.offsets.insert(list.offsets.begin() + index, PostingList::no_positions);
    }
}

void insert_posting(PostingList& list, int doc_id, int, const std::vector<std::uint32_t>& positions) {
    const std::size_t index = insert_posting(list.postings, doc_id, static_cast<int>(positions.size()));
    if (list.offsets.empty()) {
        list.offsets.assign(list.postings.size(), PostingList::no_positions);
    } else {
        make_room_for(list.offsets, 1);
        list.offsets.insert(list.offsets.begin() + index, PostingList::no_positions);
    }
    list.offsets[index] = static_cast<std::uint32_t>(list.positions.size());
    const std::size_t first = list.positions.size();
    make_room_for(list.positions, positions.size());
    list.positions.insert(list.positions.end(), positions.begin(), positions.end());
    if (!std::is_sorted(list.positions.begin() + first, list.positions.end())) {
        std::sort(list.positions.begin() + first, list.positions.end());
    }
}

void erase_posting(PostingList& list, int doc_id) {
    auto it = std::ranges::lower_bound(list.postings, doc_id, {}, &Posting::doc_id);
    if (it == list.postings.end() || it->doc_id != doc_id) {
        return;
    }
    const std::size_t index = it - list.postings.begin();
    const std::uint32_t offset = list.offsets.empty() ? PostingList::no_positions : list.offsets[index];
    const std::uint32_t length = static_cast<std::uint32_t>(it->term_frequency);
    list.postings.erase(it);
    if (!list.offsets.empty()) {
        list.offsets.erase(list.offsets.begin() + index);
    }
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
void erase_term_posting(Map& map, const std::string& term, int doc_id) {
    auto it = map.find(term);
    if (it == map.end()) {
        return;
    }
    erase_posting(it->second, doc_id);
    if (it->second.postings.empty()) {
        map.erase(it);
    }
}

}

std::vector<const std::string*> InvertedIndex::distinct_terms(const std::vector<IndexedField>& indexed) {
    std::vector<const std::string*> terms;
    for (const IndexedField& entry : indexed) {
        terms.insert(terms.end(), entry.terms.begin(), entry.terms.end());
    }
    if (indexed.size() > 1) {
        std::ranges::sort(terms, [](const std::string* a, const std::string* b) { return *a < *b; });
        terms.erase(std::ranges::unique(terms, [](const std::string* a, const std::string* b) { return *a == *b; })
                        .begin(),
                    terms.end());
    }
    return terms;
}

template <typename TermMap>
void InvertedIndex::add_fields(int doc_id, const std::unordered_map<std::string, TermMap>& fields_to_add) {
    remove_document(doc_id);

    std::vector<IndexedField>& indexed = doc_fields[doc_id];
    for (const auto& [field, term_entries] : fields_to_add) {
        if (term_entries.empty()) {
            continue;
        }

        const auto field_it = fields.try_emplace(field).first;
        FieldIndex& field_index = field_it->second;
        field_index.documents++;

        IndexedField& entry = indexed.emplace_back(IndexedField{field_it, {}});
        entry.terms.reserve(term_entries.size());
        int length = 0;
        for (const auto& [term, occurrences] : term_entries) {
            const int freq = frequency_of(occurrences);
            length += freq;
            const auto term_it = field_index.terms.try_emplace(term).first;
            insert_posting(term_it->second, doc_id, freq, occurrences);
            entry.terms.push_back(&term_it->first);
        }
        if (static_cast<std::size_t>(doc_id) >= field_index.lengths.size()) {
            field_index.lengths.resize(static_cast<std::size_t>(doc_id) + 1);
        }
        field_index.lengths[static_cast<std::size_t>(doc_id)] = length;
        field_index.total_length += length;
    }

    for (const std::string* term : distinct_terms(indexed)) {
        auto it = doc_freqs.lower_bound(*term);
        if (it == doc_freqs.end() || it->first != *term) {
            it = doc_freqs.emplace_hint(it, *term, 0);
            fuzzy.add(*term);
        }
        it->second++;
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

    for (const std::string* term : distinct_terms(it->second)) {
        const auto frequency = doc_freqs.find(*term);
        if (--frequency->second == 0) {
            fuzzy.remove(*term);
            doc_freqs.erase(frequency);
        }
    }

    for (const IndexedField& entry : it->second) {
        FieldIndex& field_index = entry.field->second;
        for (const std::string* term : entry.terms) {
            erase_term_posting(field_index.terms, *term, doc_id);
        }
        const std::size_t slot = static_cast<std::size_t>(doc_id);
        if (slot < field_index.lengths.size()) {
            field_index.total_length -= field_index.lengths[slot];
            field_index.lengths[slot] = 0;
        }
        if (--field_index.documents == 0) {
            fields.erase(entry.field);
        }
    }
    doc_fields.erase(it);
    total_documents--;
}

std::size_t InvertedIndex::get_document_frequency(const std::string& term) const {
    const auto it = doc_freqs.find(term);
    return it == doc_freqs.end() ? 0 : it->second;
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
    std::vector<TermPostings> matches;
    for (auto it = doc_freqs.lower_bound(prefix); it != doc_freqs.end() && it->first.starts_with(prefix); ++it) {
        matches.push_back({it->first, it->second});
    }
    return matches;
}

std::vector<TermPostings> InvertedIndex::terms_with_prefix(std::string_view field, std::string_view prefix) const {
    auto field_it = fields.find(field);
    if (field_it == fields.end()) {
        return {};
    }
    const PostingListMap& terms = field_it->second.terms;
    std::vector<TermPostings> matches;
    for (auto it = terms.lower_bound(prefix); it != terms.end() && it->first.starts_with(prefix); ++it) {
        matches.push_back({it->first, it->second.postings.size()});
    }
    return matches;
}

int InvertedIndex::get_field_documents(std::string_view field) const {
    auto it = fields.find(field);
    return it == fields.end() ? 0 : it->second.documents;
}

int InvertedIndex::get_field_length(std::string_view field, int doc_id) const {
    const std::span<const int> lengths = get_field_lengths(field);
    const std::size_t slot = static_cast<std::size_t>(doc_id);
    return doc_id >= 0 && slot < lengths.size() ? lengths[slot] : 0;
}

std::span<const int> InvertedIndex::get_field_lengths(std::string_view field) const {
    auto field_it = fields.find(field);
    return field_it == fields.end() ? std::span<const int>() : std::span<const int>(field_it->second.lengths);
}

double InvertedIndex::get_average_field_length(std::string_view field) const {
    auto it = fields.find(field);
    if (it == fields.end() || it->second.documents == 0) {
        return 0.0;
    }
    return static_cast<double>(it->second.total_length) / it->second.documents;
}
