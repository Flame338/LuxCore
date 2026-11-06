#include "Document.h"

#include <algorithm>
#include <utility>

Document& Document::add(Field field) {
    fields.push_back(std::move(field));
    return *this;
}

const Field* Document::find(std::string_view name) const {
    const auto it = std::ranges::find(fields, name, &Field::name);
    return it == fields.end() ? nullptr : &*it;
}

Document make_text_document(const std::string& title, const std::string& body) {
    Document document;
    document.add({"title", title});
    document.add({"body", body});
    return document;
}
