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
    } catch (const luxcore::QueryError&) {
        return LUX_INVALID_QUERY;
    } catch (const std::invalid_argument&) {
        return LUX_INVALID_ARGUMENT;
    } catch (const luxcore::IoError&) {
        return LUX_IO_ERROR;
    } catch (const luxcore::FormatError&) {
        return LUX_INVALID_FORMAT;
    } catch (const std::bad_alloc&) {
        return LUX_OUT_OF_MEMORY;
    } catch (...) {
        return LUX_INTERNAL_ERROR;
    }
}

luxcore::IndexConfig index_config(const lux_config* config) {
    luxcore::IndexConfig result;
    if (config != nullptr) {
        result.positions = config->positions != 0;
    }
    return result;
}

void override_text(std::string& target, const char* data, std::size_t length) {
    if (data != nullptr) {
        target.assign(data, length);
    }
}

luxcore::SnippetOptions snippet_options(const lux_snippet_options* options) {
    luxcore::SnippetOptions result;
    if (options != nullptr) {
        if (options->length != 0) {
            result.length = options->length;
        }
        override_text(result.start_marker, options->start_marker, options->start_marker_length);
        override_text(result.end_marker, options->end_marker, options->end_marker_length);
        override_text(result.ellipsis, options->ellipsis, options->ellipsis_length);
    }
    return result;
}

template <typename Snippet>
lux_status snippet_into(const lux_index* index, const char* key, std::size_t key_length, const char* field,
                        std::size_t field_length, const char* query, std::size_t query_length,
                        const lux_snippet_options* options, lux_text** out, Snippet snippet) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (index == nullptr || key == nullptr || field == nullptr || query == nullptr || out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        std::optional<std::string> value =
            snippet(index->index, std::string(key, key_length), std::string(field, field_length),
                    std::string(query, query_length), snippet_options(options));
        if (!value) {
            return LUX_NOT_FOUND;
        }
        *out = new lux_text{std::move(*value)};
        return LUX_OK;
    });
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
        case LUX_IO_ERROR:
            return "i/o error";
        case LUX_INVALID_FORMAT:
            return "not a valid LuxCore index file";
        case LUX_INVALID_QUERY:
            return "invalid query";
    }
    return "unknown status";
}

lux_status lux_index_create(lux_index** out) {
    return lux_index_create_with(nullptr, out);
}

lux_status lux_index_create_with(const lux_config* config, lux_index** out) {
    if (out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    *out = nullptr;
    return guarded([&] {
        *out = new lux_index{luxcore::Index(index_config(config))};
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
        index->index.add(std::string(key, key_length), std::string(title, title_length), std::string(body, body_length));
        return LUX_OK;
    });
}

lux_status lux_index_remove(lux_index* index, const char* key, std::size_t key_length, int* removed) {
    if (index == nullptr || key == nullptr || removed == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *removed = index->index.remove(std::string(key, key_length)) ? 1 : 0;
        return LUX_OK;
    });
}

lux_status lux_index_size(const lux_index* index, std::size_t* size) {
    if (index == nullptr || size == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    *size = index->index.size();
    return LUX_OK;
}

lux_status lux_index_save(const lux_index* index, const char* path, std::size_t path_length) {
    if (index == nullptr || path == nullptr || path_length == 0) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        index->index.save(std::string(path, path_length));
        return LUX_OK;
    });
}

lux_status lux_index_load(const char* path, std::size_t path_length, lux_index** out) {
    return lux_index_load_with(path, path_length, nullptr, out);
}

lux_status lux_index_load_with(const char* path, std::size_t path_length, const lux_config* config, lux_index** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (path == nullptr || path_length == 0 || out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *out = new lux_index{luxcore::Index::load(std::string(path, path_length), index_config(config))};
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
        std::optional<std::string> value = index->index.stored(std::string(key, key_length), std::string(field, field_length));
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
        *out = new lux_hits{index->index.search(std::string(query, query_length), limit)};
        return LUX_OK;
    });
}

lux_status lux_index_search_query(const lux_index* index, const char* query, std::size_t query_length,
                                  std::size_t limit, lux_hits** out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    if (index == nullptr || query == nullptr || out == nullptr) {
        return LUX_INVALID_ARGUMENT;
    }
    return guarded([&] {
        *out = new lux_hits{index->index.search_query(std::string(query, query_length), limit)};
        return LUX_OK;
    });
}

lux_status lux_index_snippet(const lux_index* index, const char* key, std::size_t key_length, const char* field,
                             std::size_t field_length, const char* query, std::size_t query_length,
                             const lux_snippet_options* options, lux_text** out) {
    return snippet_into(index, key, key_length, field, field_length, query, query_length, options, out,
                        [](const luxcore::Index& target, const std::string& key_text, const std::string& field_text,
                           const std::string& query_text, const luxcore::SnippetOptions& settings) {
                            return target.snippet(key_text, field_text, query_text, settings);
                        });
}

lux_status lux_index_snippet_query(const lux_index* index, const char* key, std::size_t key_length, const char* field,
                                   std::size_t field_length, const char* query, std::size_t query_length,
                                   const lux_snippet_options* options, lux_text** out) {
    return snippet_into(index, key, key_length, field, field_length, query, query_length, options, out,
                        [](const luxcore::Index& target, const std::string& key_text, const std::string& field_text,
                           const std::string& query_text, const luxcore::SnippetOptions& settings) {
                            return target.snippet_query(key_text, field_text, query_text, settings);
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
        *out = new lux_strings{index->index.suggest(std::string(query, query_length), limit)};
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
