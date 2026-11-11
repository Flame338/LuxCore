#include "DocumentTerms.h"

#include "TermDictionary.h"

#include <algorithm>
#include <stdexcept>

std::size_t DocumentTermsBuilder::field(std::string_view name) {
    for (std::size_t i = 0; i < result.fields.size(); i++) {
        if (result.fields[i].name == name) {
            return i;
        }
    }
    result.fields.push_back({std::string(name), {}});
    tables.emplace_back();
    return result.fields.size() - 1;
}

std::uint32_t DocumentTermsBuilder::intern(std::size_t field, std::string_view term) {
    std::vector<DocumentTerms::Term>& terms = result.fields[field].terms;
    Table& table = tables[field];
    if ((terms.size() + 1) * 2 > table.slots.size()) {
        table.slots.assign(std::max<std::size_t>(64, table.slots.size() * 2), 0);
        for (std::uint32_t i = 0; i < terms.size(); i++) {
            std::size_t at = terms[i].hash & (table.slots.size() - 1);
            while (table.slots[at] != 0) {
                at = (at + 1) & (table.slots.size() - 1);
            }
            table.slots[at] = i + 1;
        }
    }

    const std::uint64_t hash = hash_term(term);
    const std::size_t mask = table.slots.size() - 1;
    std::size_t at = hash & mask;
    for (; table.slots[at] != 0; at = (at + 1) & mask) {
        const DocumentTerms::Term& known = terms[table.slots[at] - 1];
        if (known.hash == hash && result.text_of(known) == term) {
            return table.slots[at] - 1;
        }
    }
    if (result.text.size() + term.size() > UINT32_MAX) {
        throw std::length_error("LuxCore: the distinct terms of one document hold more than 4 GB of text");
    }
    terms.push_back({hash, static_cast<std::uint32_t>(result.text.size()), static_cast<std::uint32_t>(term.size()), 0,
                     0});
    result.text.append(term);
    table.slots[at] = static_cast<std::uint32_t>(terms.size());
    return static_cast<std::uint32_t>(terms.size() - 1);
}

void DocumentTermsBuilder::add(std::size_t field, std::string_view term, std::uint32_t position) {
    const std::uint32_t index = intern(field, term);
    result.fields[field].terms[index].frequency++;
    if (result.positioned) {
        tables[field].occurrences.push_back({index, position});
    }
}

void DocumentTermsBuilder::add_count(std::size_t field, std::string_view term, std::uint32_t frequency) {
    result.fields[field].terms[intern(field, term)].frequency += frequency;
}

DocumentTerms DocumentTermsBuilder::finish() {
    if (result.positioned) {
        std::size_t total = 0;
        for (const Table& table : tables) {
            total += table.occurrences.size();
        }
        if (total > UINT32_MAX) {
            throw std::length_error("LuxCore: one document holds at most 2^32 term positions");
        }
        result.positions.resize(total);
        std::uint32_t next = 0;
        std::vector<std::uint32_t> cursor;
        for (std::size_t f = 0; f < tables.size(); f++) {
            std::vector<DocumentTerms::Term>& terms = result.fields[f].terms;
            cursor.resize(terms.size());
            for (std::size_t i = 0; i < terms.size(); i++) {
                terms[i].first_position = next;
                cursor[i] = next;
                next += terms[i].frequency;
            }
            for (const Occurrence& occurrence : tables[f].occurrences) {
                result.positions[cursor[occurrence.term]++] = occurrence.position;
            }
            for (const DocumentTerms::Term& term : terms) {
                const auto first = result.positions.begin() + term.first_position;
                if (!std::is_sorted(first, first + term.frequency)) {
                    std::sort(first, first + term.frequency);
                }
            }
        }
    }
    tables.clear();
    return std::move(result);
}
