#pragma once

#include "Document.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

class DocumentStore {
private:
    std::vector<std::optional<Document>> documents;
    std::size_t live = 0;

public:
    int add(Document document) {
        documents.emplace_back(std::move(document));
        live++;
        return static_cast<int>(documents.size() - 1);
    }

    bool contains(int id) const {
        return id >= 0 && static_cast<std::size_t>(id) < documents.size() && documents[static_cast<std::size_t>(id)];
    }

    void replace(int id, Document document) {
        if (!contains(id)) {
            throw std::out_of_range("DocumentStore::replace");
        }
        documents[static_cast<std::size_t>(id)] = std::move(document);
    }

    bool remove(int id) {
        if (!contains(id)) {
            return false;
        }
        documents[static_cast<std::size_t>(id)].reset();
        live--;
        return true;
    }

    const Document& get(int id) const {
        if (!contains(id)) {
            throw std::out_of_range("DocumentStore::get");
        }
        return *documents[static_cast<std::size_t>(id)];
    }

    std::size_t size() const { return live; }
};
