#pragma once

#include "Document.h"

#include <cstddef>
#include <utility>
#include <vector>

class DocumentStore {
private:
    std::vector<Document> documents;

public:
    int add(Document document) {
        documents.push_back(std::move(document));
        return static_cast<int>(documents.size() - 1);
    }

    const Document& get(int id) const { return documents.at(static_cast<std::size_t>(id)); }

    std::size_t size() const { return documents.size(); }
};
