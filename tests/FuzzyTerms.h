#pragma once

#include "TermDictionary.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct FuzzyTerms {
    TermDictionary terms;

    FuzzyTerms() = default;
    explicit FuzzyTerms(const std::vector<std::string>& source_terms) {
        for (const std::string& term : source_terms) {
            add(term);
        }
    }

    void add(std::string_view term) { terms.intern(term); }

    void remove(std::string_view term) {
        const TermId id = terms.find(term);
        if (id != TermDictionary::none) {
            terms.erase(id);
        }
    }

    std::vector<FuzzyMatch> candidates(std::string_view term, std::size_t max_distance) const {
        return terms.fuzzy(term, max_distance);
    }

    std::size_t size() const { return terms.size(); }
};
