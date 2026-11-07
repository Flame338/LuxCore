#include "luxcore_c.h"

#include "LuxCore.h"

#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct lux_index {
    luxcore::Index index;
};

struct lux_hits {
    std::vector<luxcore::Hit> items;
};

struct lux_strings {
    std::vector<std::string> items;
};

struct lux_text {
    std::string value;
};

namespace {

template <typename Body>
lux_status guarded(Body body) {
    try {
        return body();
    } catch (const std::invalid_argument&) {
        return LUX_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return LUX_OUT_OF_MEMORY;
    } catch (...) {
        return LUX_INTERNAL_ERROR;
    }
}

std::string text_of(const char* data, std::size_t length) {
    return std::string(data, length);
}

}

const char* lux_status_message(lux_status status) {
    switch (status) {
        case LUX_OK:
            return "ok";
        case LUX_INVALID_ARGUMENT:
            return "invalid argument";
        case LUX_NOT_FOUND:
            return "not found";
        case LUX_OUT_OF_MEMORY:
            return "out of memory";
        case LUX_INTERNAL_ERROR:
            return "internal error";
    }
    return "unknown status";
}

lux_status lux_index_create(lux_index** out) {
    if (out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    *out = nullptr;
    return guarded([&] {
        *out = new lux_index();
        return LUX_OK;
    });
}

void lux_index_free(lux_index* index) {
    delete index;
}

lux_status lux_index_add(lux_index* index, const char* key, std::size_t key_length, const char* title,
                         std::size_t title_length, const char* body, std::size_t body_length) {
    if (index == nullptr || key == nullptr || title == nullptr || body == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        index->index.add(text_of(key, key_length), text_of(title, title_length), text_of(body, body_length));
        return LUX_OK;
    });
}

lux_status lux_index_remove(lux_index* index, const char* key, std::size_t key_length, int* removed) {
    if (index == nullptr || key == nullptr || removed == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *removed = index->index.remove(text_of(key, key_length)) ? 1 : 0;
        return LUX_OK;
    });
}

lux_status lux_index_size(const lux_index* index, std::size_t* size) {
    if (index == nullptr || size == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *size = index->index.size();
        return LUX_OK;
    });
}

lux_status lux_index_stored(const lux_index* index, const char* key, std::size_t key_length, const char* field,
                            std::size_t field_length, lux_text** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (index == nullptr || key == nullptr || field == nullptr || out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        std::optional<std::string> value = index->index.stored(text_of(key, key_length), text_of(field, field_length));
        if (!value) {
            return LUX_NOT_FOUND;
        }
        *out = new lux_text{std::move(*value)};
        return LUX_OK;
    });
}

lux_status lux_index_search(const lux_index* index, const char* query, std::size_t query_length, std::size_t limit,
                            lux_hits** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (index == nullptr || query == nullptr || out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *out = new lux_hits{index->index.search(text_of(query, query_length), limit)};
        return LUX_OK;
    });
}

lux_status lux_index_suggest(const lux_index* index, const char* query, std::size_t query_length, std::size_t limit,
                             lux_strings** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (index == nullptr || query == nullptr || out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *out = new lux_strings{index->index.suggest(text_of(query, query_length), limit)};
        return LUX_OK;
    });
}

std::size_t lux_hits_count(const lux_hits* hits) {
    return hits == nullptr ? 0 : hits->items.size();
}

const char* lux_hits_key(const lux_hits* hits, std::size_t position, std::size_t* length) {
    if (hits == nullptr || position >= hits->items.size()) {
        if (length != nullptr) {
            *length = 0;
        }
        return nullptr;
    }
    const std::string& key = hits->items[position].key;
    if (length != nullptr) {
        *length = key.size();
    }
    return key.data();
}

double lux_hits_score(const lux_hits* hits, std::size_t position) {
    if (hits == nullptr || position >= hits->items.size()) {
        return 0.0;
    }
    return hits->items[position].score;
}

void lux_hits_free(lux_hits* hits) {
    delete hits;
}

std::size_t lux_strings_count(const lux_strings* strings) {
    return strings == nullptr ? 0 : strings->items.size();
}

const char* lux_strings_get(const lux_strings* strings, std::size_t position, std::size_t* length) {
    if (strings == nullptr || position >= strings->items.size()) {
        if (length != nullptr) {
            *length = 0;
        }
        return nullptr;
    }
    const std::string& item = strings->items[position];
    if (length != nullptr) {
        *length = item.size();
    }
    return item.data();
}

void lux_strings_free(lux_strings* strings) {
    delete strings;
}

const char* lux_text_data(const lux_text* text) {
    return text == nullptr ? nullptr : text->value.data();
}

std::size_t lux_text_length(const lux_text* text) {
    return text == nullptr ? 0 : text->value.size();
}

void lux_text_free(lux_text* text) {
    delete text;
}
