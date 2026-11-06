#pragma once

#include <string>
#include <string_view>
#include <vector>

struct FieldOptions {
    bool stored;
    bool indexed;
    bool analyzed;

    static constexpr FieldOptions text() { return {true, true, true}; }
    static constexpr FieldOptions keyword() { return {true, true, false}; }
    static constexpr FieldOptions stored_only() { return {true, false, false}; }

    bool operator==(const FieldOptions&) const = default;
};

struct Field {
    std::string name;
    std::string value;
    FieldOptions options = FieldOptions::text();
};

class Document {
private:
    std::vector<Field> fields;

public:
    Document& add(Field field);
    const Field* find(std::string_view name) const;
    const std::vector<Field>& get_fields() const { return fields; }
};

Document make_text_document(const std::string& title, const std::string& body);
