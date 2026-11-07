#include "InvertedIndex.h"

#include "ByteIO.h"
#include "DocumentStore.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace {

struct Tallies {
    std::vector<long long> lengths;
    std::vector<int> term_counts;
};

PostingList read_posting_list(ByteReader& reader, const DocumentStore& store, Tallies& tallies) {
    PostingList list;
    const std::size_t count = reader.count("posting count", 2);
    if (count == 0) {
        fail_format("term without postings");
    }
    list.postings.reserve(count);
    list.offsets.reserve(count);

    const std::uint64_t id_limit = store.id_limit();
    std::uint64_t doc_id = 0;
    for (std::size_t i = 0; i < count; i++) {
        const std::uint64_t gap = reader.varint();
        if (i > 0 && gap == 0) {
            fail_format("posting doc ids are not ascending");
        }
        if (gap >= id_limit) {
            fail_format("posting refers to a missing document");
        }
        doc_id += gap;
        if (doc_id >= id_limit || !store.contains(static_cast<int>(doc_id))) {
            fail_format("posting refers to a missing document");
        }

        const std::uint64_t packed = reader.varint();
        const std::uint64_t frequency = packed >> 1;
        if (frequency == 0 || frequency > INT_MAX) {
            fail_format("invalid term frequency");
        }

        if ((packed & 1) != 0) {
            if (frequency > reader.remaining()) {
                fail_format("term frequency " + std::to_string(frequency) + " does not fit in the remaining " +
                            std::to_string(reader.remaining()) + " bytes");
            }
            if (list.positions.size() + frequency >= PostingList::no_positions) {
                fail_format("too many positions in one posting list");
            }
            list.offsets.push_back(static_cast<std::uint32_t>(list.positions.size()));
            std::uint64_t position = 0;
            for (std::uint64_t k = 0; k < frequency; k++) {
                position += reader.varint_at_most("position gap", UINT32_MAX);
                if (position > UINT32_MAX) {
                    fail_format("position is out of range");
                }
                list.positions.push_back(static_cast<std::uint32_t>(position));
            }
        } else {
            list.offsets.push_back(PostingList::no_positions);
        }

        list.postings.push_back({static_cast<int>(doc_id), static_cast<int>(frequency)});
        tallies.lengths[static_cast<std::size_t>(doc_id)] += static_cast<long long>(frequency);
        tallies.term_counts[static_cast<std::size_t>(doc_id)]++;
    }
    return list;
}

void merge_postings(std::vector<Posting>& target, const std::vector<Posting>& source) {
    std::vector<Posting> merged;
    merged.reserve(target.size() + source.size());
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < target.size() || b < source.size()) {
        if (b == source.size() || (a < target.size() && target[a].doc_id < source[b].doc_id)) {
            merged.push_back(target[a++]);
        } else if (a == target.size() || source[b].doc_id < target[a].doc_id) {
            merged.push_back(source[b++]);
        } else {
            const long long total = static_cast<long long>(target[a].term_frequency) + source[b].term_frequency;
            if (total > INT_MAX) {
                fail_format("term frequency is out of range");
            }
            merged.push_back({target[a].doc_id, static_cast<int>(total)});
            a++;
            b++;
        }
    }
    target = std::move(merged);
}

}

void InvertedIndex::write(ByteWriter& writer) const {
    writer.varint(fields.size());
    for (const auto& [name, field_index] : fields) {
        writer.string(name);
        writer.varint(field_index.terms.size());
        for (const auto& [term, list] : field_index.terms) {
            writer.string(term);
            writer.varint(list.postings.size());
            int previous = 0;
            for (std::size_t i = 0; i < list.postings.size(); i++) {
                const Posting& posting = list.postings[i];
                writer.varint(static_cast<std::uint64_t>(posting.doc_id - previous));
                previous = posting.doc_id;

                const bool has_positions = list.offsets[i] != PostingList::no_positions;
                writer.varint((static_cast<std::uint64_t>(posting.term_frequency) << 1) | (has_positions ? 1 : 0));
                std::uint32_t last = 0;
                for (const std::uint32_t position : list.positions_of(i)) {
                    writer.varint(position - last);
                    last = position;
                }
            }
        }
    }
}

InvertedIndex InvertedIndex::read(ByteReader& reader, const DocumentStore& store) {
    InvertedIndex result;
    const std::size_t id_limit = store.id_limit();
    Tallies tallies{std::vector<long long>(id_limit), std::vector<int>(id_limit)};
    std::vector<IndexedField*> entries(id_limit);
    result.doc_fields.reserve(store.size());

    const std::size_t field_count = reader.count("field count", 3);
    for (std::size_t f = 0; f < field_count; f++) {
        const std::string name(reader.string("field name length"));
        const auto [field_it, inserted] = result.fields.try_emplace(name);
        if (!inserted) {
            fail_format("duplicate field " + name);
        }
        FieldIndex& field_index = field_it->second;

        const std::size_t term_count = reader.count("term count", 4);
        if (term_count == 0) {
            fail_format("field without terms");
        }
        for (std::size_t t = 0; t < term_count; t++) {
            const std::string term(reader.string("term length"));
            if (!field_index.terms.empty() && !(field_index.terms.rbegin()->first < term)) {
                fail_format("terms are not in ascending order");
            }
            field_index.terms.emplace_hint(field_index.terms.end(), term, read_posting_list(reader, store, tallies));
        }

        for (std::size_t doc = 0; doc < id_limit; doc++) {
            if (tallies.term_counts[doc] == 0) {
                continue;
            }
            if (tallies.lengths[doc] > INT_MAX) {
                fail_format("field length is out of range");
            }
            const int length = static_cast<int>(tallies.lengths[doc]);
            field_index.lengths.emplace(static_cast<int>(doc), length);
            field_index.total_length += length;
            field_index.documents++;

            IndexedField& entry = result.doc_fields[static_cast<int>(doc)].emplace_back(IndexedField{name, {}});
            entry.terms.reserve(static_cast<std::size_t>(tallies.term_counts[doc]));
            entries[doc] = &entry;
        }
        for (const auto& [term, list] : field_index.terms) {
            for (const Posting& posting : list.postings) {
                entries[static_cast<std::size_t>(posting.doc_id)]->terms.push_back(term);
            }
        }

        std::ranges::fill(tallies.lengths, 0);
        std::ranges::fill(tallies.term_counts, 0);

        for (const auto& [term, list] : field_index.terms) {
            auto it = result.index.lower_bound(term);
            if (it == result.index.end() || it->first != term) {
                result.index.emplace_hint(it, term, list.postings);
            } else {
                merge_postings(it->second, list.postings);
            }
        }
    }

    std::vector<std::string> terms;
    terms.reserve(result.index.size());
    for (const auto& entry : result.index) {
        terms.push_back(entry.first);
    }
    result.fuzzy = FuzzyIndex(terms);

    for (std::size_t doc = 0; doc < id_limit; doc++) {
        if (store.contains(static_cast<int>(doc))) {
            result.doc_fields[static_cast<int>(doc)];
        }
    }
    result.total_documents = static_cast<int>(result.doc_fields.size());
    return result;
}
