#include "InvertedIndex.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace {

void append_varint(std::string& out, std::uint32_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<char>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

void decode_list_ids(std::string_view encoded, std::vector<ListId>& out) {
    out.clear();
    ListId id = 0;
    for (std::size_t at = 0; at < encoded.size();) {
        std::uint32_t gap = 0;
        for (int shift = 0;; shift += 7) {
            const std::uint8_t byte = static_cast<std::uint8_t>(encoded[at++]);
            gap |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                break;
            }
        }
        id += gap;
        out.push_back(id);
    }
}

}

std::uint64_t InvertedIndex::StateStamp::next() {
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

std::optional<std::uint32_t> InvertedIndex::field_id(std::string_view name) const {
    const auto found = field_ids.find(name);
    if (found == field_ids.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::uint32_t InvertedIndex::field_for(std::string_view name) {
    if (const std::optional<std::uint32_t> known = field_id(name)) {
        return *known;
    }
    std::uint32_t id;
    if (free_fields.empty()) {
        id = static_cast<std::uint32_t>(fields.size());
        fields.emplace_back();
    } else {
        id = free_fields.back();
        free_fields.pop_back();
    }
    fields[id].name = std::string(name);
    field_ids.emplace(fields[id].name, id);
    return id;
}

void InvertedIndex::retire_field(std::uint32_t field) {
    field_ids.erase(fields[field].name);
    fields[field] = FieldIndex{};
    free_fields.push_back(field);
}

ListId InvertedIndex::list_of(TermId term, std::uint32_t field) const {
    for (ListId list = terms.first_list(term); list != PostingStore::none; list = lists.next_of(list)) {
        if (lists.field_of(list) == field) {
            return list;
        }
    }
    return PostingStore::none;
}

ListId InvertedIndex::list_for(TermId term, std::uint32_t field) {
    const ListId known = list_of(term, field);
    if (known != PostingStore::none) {
        return known;
    }
    const ListId created = lists.create(term, field);
    lists.set_next(created, terms.first_list(term));
    terms.first_list(term) = created;
    fields[field].terms++;
    return created;
}

void InvertedIndex::drop_list(ListId list) {
    const TermId term = lists.term_of(list);
    if (terms.first_list(term) == list) {
        terms.first_list(term) = lists.next_of(list);
    } else {
        ListId previous = terms.first_list(term);
        while (lists.next_of(previous) != list) {
            previous = lists.next_of(previous);
        }
        lists.set_next(previous, lists.next_of(list));
    }
    fields[lists.field_of(list)].terms--;
    lists.destroy(list);
}

ListId InvertedIndex::find_list(std::string_view field, std::string_view term) const {
    const std::optional<std::uint32_t> id = field_id(field);
    if (!id) {
        return PostingStore::none;
    }
    const TermId found = terms.find(term);
    return found == TermDictionary::none ? PostingStore::none : list_of(found, *id);
}

// A term counts a document once, however many of its fields hold the term.
bool InvertedIndex::in_another_list(TermId term, ListId list, int doc_id) const {
    for (ListId other = terms.first_list(term); other != PostingStore::none; other = lists.next_of(other)) {
        if (other != list && lists.postings(other).contains(doc_id)) {
            return true;
        }
    }
    return false;
}

void InvertedIndex::remember_lists(int doc_id) {
    std::ranges::sort(scratch_lists);
    std::string encoded;
    encoded.reserve(scratch_lists.size() * 2);
    ListId previous = 0;
    for (const ListId list : scratch_lists) {
        append_varint(encoded, list - previous);
        previous = list;
    }
    const std::size_t slot = static_cast<std::size_t>(doc_id);
    if (slot >= doc_lists.size()) {
        if (slot >= doc_lists.capacity()) {
            doc_lists.reserve(std::max(slot + 1, doc_lists.capacity() + doc_lists.capacity() / 4));
        }
        doc_lists.resize(slot + 1);
    }
    doc_lists[slot].emplace(encoded.data(), encoded.size());
}

void InvertedIndex::add_document(int doc_id, const DocumentTerms& document) {
    if (doc_id < 0) {
        throw std::invalid_argument("InvertedIndex::add_document: negative document id");
    }
    IndexClocks& clocks = index_clocks();
    {
        auto timed = clocks.invert_remove.time();
        remove_document(doc_id);
    }

    stamp.touch();
    {
        auto timed = clocks.invert_terms.time();
        scratch_lists.clear();
        for (const DocumentTerms::Field& field : document.fields) {
            if (field.terms.empty()) {
                continue;
            }
            const std::uint32_t field_number = field_for(field.name);
            fields[field_number].documents++;

            for (const DocumentTerms::Term& term : field.terms) {
                terms.prefetch_slot(term.hash);
            }
            for (const DocumentTerms::Term& term : field.terms) {
                terms.prefetch_entry(term.hash);
            }
            for (const DocumentTerms::Term& term : field.terms) {
                terms.prefetch_text(term.hash);
            }
            scratch_terms.clear();
            for (const DocumentTerms::Term& term : field.terms) {
                const TermId id = terms.intern(document.text_of(term), term.hash);
                scratch_terms.push_back(id);
                if (terms.first_list(id) != PostingStore::none) {
                    lists.prefetch(terms.first_list(id));
                }
            }
            const std::size_t first_list = scratch_lists.size();
            for (const TermId id : scratch_terms) {
                const ListId list = list_for(id, field_number);
                scratch_lists.push_back(list);
                lists.prefetch_tail(list);
            }

            long long length = 0;
            for (std::size_t i = 0; i < field.terms.size(); i++) {
                const DocumentTerms::Term& term = field.terms[i];
                const TermId id = scratch_terms[i];
                const ListId list = scratch_lists[first_list + i];
                if (!in_another_list(id, list, doc_id)) {
                    terms.document_frequency(id)++;
                }
                if (document.positioned) {
                    lists.insert(list, doc_id,
                                 std::span(document.positions).subspan(term.first_position, term.frequency));
                } else {
                    lists.insert(list, doc_id, term.frequency);
                }
                length += term.frequency;
            }

            FieldIndex& field_index = fields[field_number];
            if (static_cast<std::size_t>(doc_id) >= field_index.lengths.size()) {
                field_index.lengths.resize(static_cast<std::size_t>(doc_id) + 1);
            }
            field_index.lengths[static_cast<std::size_t>(doc_id)] = static_cast<int>(length);
            field_index.total_length += static_cast<int>(length);
        }
    }

    auto timed = clocks.invert_doc_lists.time();
    remember_lists(doc_id);
    total_documents++;
}

void InvertedIndex::add_document(int doc_id, const FieldTermFreqs& field_term_freqs) {
    DocumentTermsBuilder builder(false);
    for (const auto& [field, term_freqs] : field_term_freqs) {
        const std::size_t slot = builder.field(field);
        for (const auto& [term, frequency] : term_freqs) {
            builder.add_count(slot, term, static_cast<std::uint32_t>(frequency));
        }
    }
    add_document(doc_id, builder.finish());
}

void InvertedIndex::add_document(int doc_id, const FieldTermPositions& field_term_positions) {
    DocumentTermsBuilder builder(true);
    for (const auto& [field, term_positions] : field_term_positions) {
        const std::size_t slot = builder.field(field);
        for (const auto& [term, positions] : term_positions) {
            builder.add_count(slot, term, 0);
            for (const std::uint32_t position : positions) {
                builder.add(slot, term, position);
            }
        }
    }
    add_document(doc_id, builder.finish());
}

void InvertedIndex::remove_document(int doc_id) {
    const std::size_t slot = static_cast<std::size_t>(doc_id);
    if (doc_id < 0 || slot >= doc_lists.size() || !doc_lists[slot]) {
        return;
    }
    stamp.touch();

    decode_list_ids(*doc_lists[slot], scratch_lists);
    std::vector<std::uint32_t> touched;
    for (const ListId list : scratch_lists) {
        const TermId term = lists.term_of(list);
        const std::uint32_t field = lists.field_of(list);
        if (std::ranges::find(touched, field) == touched.end()) {
            touched.push_back(field);
        }

        lists.erase(list, doc_id);
        const bool last_of_document = !in_another_list(term, list, doc_id);
        if (lists.size_of(list) == 0) {
            drop_list(list);
        }
        if (last_of_document && --terms.document_frequency(term) == 0) {
            terms.erase(term);
        }
    }

    for (const std::uint32_t field : touched) {
        FieldIndex& field_index = fields[field];
        if (slot < field_index.lengths.size()) {
            field_index.total_length -= field_index.lengths[slot];
            field_index.lengths[slot] = 0;
        }
        if (--field_index.documents == 0) {
            retire_field(field);
        }
    }
    doc_lists[slot].reset();
    total_documents--;
}

std::size_t InvertedIndex::get_document_frequency(std::string_view term) const {
    const TermId id = terms.find(term);
    return id == TermDictionary::none ? 0 : terms.document_frequency(id);
}

std::optional<Postings> InvertedIndex::get_postings(std::string_view field, std::string_view term) const {
    const ListId list = find_list(field, term);
    if (list == PostingStore::none) {
        return std::nullopt;
    }
    return lists.postings(list);
}

std::optional<PostingList> InvertedIndex::get_posting_list(std::string_view field, std::string_view term) const {
    const ListId list = find_list(field, term);
    if (list == PostingStore::none) {
        return std::nullopt;
    }
    return lists.posting_list(list);
}

std::vector<std::string_view> InvertedIndex::field_names() const {
    std::vector<std::string_view> names;
    names.reserve(field_ids.size());
    for (const auto& entry : field_ids) {
        names.push_back(entry.first);
    }
    return names;
}

IndexMemory InvertedIndex::memory_usage() const {
    return {lists.posting_bytes(), lists.position_bytes()};
}

std::vector<FieldPostings> InvertedIndex::get_field_postings(std::string_view term) const {
    std::vector<FieldPostings> result;
    const TermId id = terms.find(term);
    if (id == TermDictionary::none) {
        return result;
    }
    for (ListId list = terms.first_list(id); list != PostingStore::none; list = lists.next_of(list)) {
        result.push_back({fields[lists.field_of(list)].name, lists.postings(list)});
    }
    if (result.size() > 1) {
        std::ranges::sort(result, {}, &FieldPostings::field);
    }
    return result;
}

std::vector<TermPostings> InvertedIndex::terms_with_prefix(std::string_view prefix) const {
    // Collecting the ids first lets the result be allocated once at its final size.
    std::vector<TermId> ids;
    terms.for_each_with_prefix(prefix, [&ids](TermId id) { ids.push_back(id); });
    std::vector<TermPostings> matches;
    matches.reserve(ids.size());
    for (const TermId id : ids) {
        matches.push_back({terms.text_of(id), terms.document_frequency(id)});
    }
    return matches;
}

std::vector<TermPostings> InvertedIndex::terms_with_prefix(std::string_view field, std::string_view prefix) const {
    const std::optional<std::uint32_t> field_number = field_id(field);
    if (!field_number) {
        return {};
    }
    // Collecting the ids first lets the result be allocated once at its final size.
    std::vector<TermId> ids;
    terms.for_each_with_prefix(prefix, [&](TermId id) {
        if (list_of(id, *field_number) != PostingStore::none) {
            ids.push_back(id);
        }
    });
    std::vector<TermPostings> matches;
    matches.reserve(ids.size());
    for (const TermId id : ids) {
        matches.push_back({terms.text_of(id), lists.size_of(list_of(id, *field_number))});
    }
    return matches;
}

int InvertedIndex::get_field_documents(std::string_view field) const {
    const std::optional<std::uint32_t> id = field_id(field);
    return id ? fields[*id].documents : 0;
}

int InvertedIndex::get_field_length(std::string_view field, int doc_id) const {
    const std::span<const int> lengths = get_field_lengths(field);
    const std::size_t slot = static_cast<std::size_t>(doc_id);
    return doc_id >= 0 && slot < lengths.size() ? lengths[slot] : 0;
}

std::span<const int> InvertedIndex::get_field_lengths(std::string_view field) const {
    const std::optional<std::uint32_t> id = field_id(field);
    return id ? std::span<const int>(fields[*id].lengths) : std::span<const int>();
}

double InvertedIndex::get_average_field_length(std::string_view field) const {
    const std::optional<std::uint32_t> id = field_id(field);
    if (!id || fields[*id].documents == 0) {
        return 0.0;
    }
    return static_cast<double>(fields[*id].total_length) / fields[*id].documents;
}

void InvertedIndex::memory_stats(MemoryStats& out) const {
    terms.memory_stats(out);
    lists.memory_stats(out);

    MemoryRow length_row{"index.field_lengths"};
    for (const FieldIndex& field : fields) {
        length_row.items += field.lengths.size();
        length_row.vector(field.lengths);
    }
    out.push_back(length_row);

    MemoryRow doc_row{"index.doc_lists"};
    doc_row.items = static_cast<std::size_t>(total_documents);
    doc_row.vector(doc_lists);
    for (const std::optional<std::string>& encoded : doc_lists) {
        if (encoded) {
            doc_row.string(*encoded);
        }
    }
    out.push_back(doc_row);
}
