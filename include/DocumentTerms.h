#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The distinct terms of one document, per field, ready to be added to an
// InvertedIndex. Field names are distinct, and so are the terms of a field.
struct DocumentTerms {
    struct Term {
        std::uint64_t hash;
        std::uint32_t text;
        std::uint32_t length;
        std::uint32_t frequency;
        std::uint32_t first_position;
    };

    struct Field {
        std::string name;
        std::vector<Term> terms;
    };

    std::vector<Field> fields;
    std::string text;
    // When positioned, each term owns frequency ascending positions from first_position.
    std::vector<std::uint32_t> positions;
    bool positioned = false;

    std::string_view text_of(const Term& term) const { return {text.data() + term.text, term.length}; }
};

class DocumentTermsBuilder {
    private:
        struct Occurrence {
            std::uint32_t term;
            std::uint32_t position;
        };

        struct Table {
            std::vector<std::uint32_t> slots;
            std::vector<Occurrence> occurrences;
        };

        DocumentTerms result;
        std::vector<Table> tables;

        std::uint32_t intern(std::size_t field, std::string_view term);

    public:
        explicit DocumentTermsBuilder(bool positioned) { result.positioned = positioned; }

        std::size_t field(std::string_view name);
        void add(std::size_t field, std::string_view term, std::uint32_t position);
        void add_count(std::size_t field, std::string_view term, std::uint32_t frequency);
        DocumentTerms finish();
};
