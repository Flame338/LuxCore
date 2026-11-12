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

struct ReadList {
    std::vector<std::uint32_t> docs;
    std::vector<std::uint32_t> frequencies;
    PositionList positions;
};

void read_posting_list(ByteReader& reader, const DocumentStore& store, Tallies& tallies, bool keep_positions,
                       ReadList& list) {
    const std::size_t count = reader.count("posting count", 2);
    if (count == 0) {
        fail_format("term without postings");
    }
    list.docs.clear();
    list.frequencies.clear();
    list.positions = PositionList{};
    list.docs.reserve(count);
    list.frequencies.reserve(count);
    bool any_positions = false;
    std::size_t position_count = 0;

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
            if (position_count + frequency >= PositionList::no_positions) {
                fail_format("too many positions in one posting list");
            }
            if (keep_positions) {
                if (!any_positions) {
                    list.positions.offsets.assign(i, PositionList::no_positions);
                    any_positions = true;
                }
                list.positions.offsets.push_back(static_cast<std::uint32_t>(list.positions.positions.size()));
            }
            position_count += frequency;
            std::uint64_t position = 0;
            for (std::uint64_t k = 0; k < frequency; k++) {
                position += reader.varint_at_most("position gap", UINT32_MAX);
                if (position > UINT32_MAX) {
                    fail_format("position is out of range");
                }
                if (keep_positions) {
                    list.positions.positions.push_back(static_cast<std::uint32_t>(position));
                }
            }
        } else if (any_positions) {
            list.positions.offsets.push_back(PositionList::no_positions);
        }

        list.docs.push_back(static_cast<std::uint32_t>(doc_id));
        list.frequencies.push_back(static_cast<std::uint32_t>(frequency));
        tallies.lengths[static_cast<std::size_t>(doc_id)] += static_cast<long long>(frequency);
        tallies.term_counts[static_cast<std::size_t>(doc_id)]++;
    }
    list.positions.offsets.shrink_to_fit();
    list.positions.positions.shrink_to_fit();
}

std::size_t varint_size(std::uint32_t value) {
    std::size_t size = 1;
    for (; value >= 0x80; value >>= 7) {
        size++;
    }
    return size;
}

}

void InvertedIndex::write(ByteWriter& writer) const {
    std::vector<TermId> ordered;
    ordered.reserve(terms.size());
    terms.with_prefix({}, ordered);

    writer.varint(field_ids.size());
    for (const auto& [name, field_number] : field_ids) {
        writer.string(name);
        writer.varint(fields[field_number].terms);
        for (const TermId term : ordered) {
            const ListId id = list_of(term, field_number);
            if (id == PostingStore::none) {
                continue;
            }
            const PostingList list = lists.posting_list(id);
            writer.string(terms.text_of(term));
            writer.varint(list.postings.size());
            int previous = 0;
            for (std::size_t i = 0; i < list.postings.size(); i++) {
                const Posting posting = list.postings[i];
                writer.varint(static_cast<std::uint64_t>(posting.doc_id - previous));
                previous = posting.doc_id;

                writer.varint((static_cast<std::uint64_t>(posting.term_frequency) << 1) |
                              (list.has_positions(i) ? 1 : 0));
                std::uint32_t last = 0;
                for (const std::uint32_t position : list.positions_of(i)) {
                    writer.varint(position - last);
                    last = position;
                }
            }
        }
    }
}

InvertedIndex InvertedIndex::read(ByteReader& reader, const DocumentStore& store, bool keep_positions) {
    InvertedIndex result;
    const std::size_t id_limit = store.id_limit();
    Tallies tallies{std::vector<long long>(id_limit), std::vector<int>(id_limit)};
    ReadList read;

    const std::size_t field_count = reader.count("field count", 3);
    for (std::size_t f = 0; f < field_count; f++) {
        const std::string name(reader.string("field name length"));
        if (result.field_id(name)) {
            fail_format("duplicate field " + name);
        }
        const std::uint32_t field_number = result.field_for(name);
        result.fields[field_number].lengths.assign(id_limit, 0);

        const std::size_t term_count = reader.count("term count", 4);
        if (term_count == 0) {
            fail_format("field without terms");
        }
        result.terms.reserve(result.terms.size() + term_count, 0);
        result.lists.reserve(result.lists.size() + term_count);
        std::string_view previous;
        for (std::size_t t = 0; t < term_count; t++) {
            const std::string_view term = reader.string("term length");
            if (t > 0 && !(previous < term)) {
                fail_format("terms are not in ascending order");
            }
            previous = term;
            read_posting_list(reader, store, tallies, keep_positions, read);

            const TermId id = result.terms.intern(term);
            const ListId list = result.list_for(id, field_number);
            std::uint32_t added = static_cast<std::uint32_t>(read.docs.size());
            if (result.lists.next_of(list) != PostingStore::none) {
                for (const std::uint32_t doc : read.docs) {
                    added -= result.in_another_list(id, list, static_cast<int>(doc)) ? 1 : 0;
                }
            }
            result.terms.document_frequency(id) += added;
            result.lists.assign(list, read.docs, read.frequencies, std::move(read.positions));
        }

        FieldIndex& field_index = result.fields[field_number];
        for (std::size_t doc = 0; doc < id_limit; doc++) {
            if (tallies.term_counts[doc] == 0) {
                continue;
            }
            if (tallies.lengths[doc] > INT_MAX) {
                fail_format("field length is out of range");
            }
            const int length = static_cast<int>(tallies.lengths[doc]);
            field_index.lengths[doc] = length;
            field_index.total_length += length;
            field_index.documents++;
        }
        std::ranges::fill(tallies.lengths, 0);
        std::ranges::fill(tallies.term_counts, 0);
    }

    // Lists were created in ascending id order, so walking them in that order
    // gives every document its list ids ascending.
    const ListId list_count = static_cast<ListId>(result.lists.size());
    std::vector<std::uint32_t> encoded_size(id_limit);
    std::vector<ListId> last_list(id_limit);
    for (ListId list = 0; list < list_count; list++) {
        for (const std::uint32_t doc : result.lists.postings(list).doc_ids()) {
            encoded_size[doc] += static_cast<std::uint32_t>(varint_size(list - last_list[doc]));
            last_list[doc] = list;
        }
    }
    result.doc_lists.resize(id_limit);
    for (std::size_t doc = 0; doc < id_limit; doc++) {
        if (store.contains(static_cast<int>(doc))) {
            result.doc_lists[doc].emplace(encoded_size[doc], '\0');
            result.total_documents++;
        }
    }
    std::ranges::fill(encoded_size, 0);
    std::ranges::fill(last_list, 0);
    for (ListId list = 0; list < list_count; list++) {
        for (const std::uint32_t doc : result.lists.postings(list).doc_ids()) {
            char* out = result.doc_lists[doc]->data() + encoded_size[doc];
            std::uint32_t gap = list - last_list[doc];
            for (; gap >= 0x80; gap >>= 7) {
                *out++ = static_cast<char>(gap | 0x80);
            }
            *out++ = static_cast<char>(gap);
            encoded_size[doc] = static_cast<std::uint32_t>(out - result.doc_lists[doc]->data());
            last_list[doc] = list;
        }
    }
    return result;
}
