#pragma once

#include <stddef.h>

#if defined(_WIN32)
#if defined(LUX_BUILD_SHARED)
#define LUX_API __declspec(dllexport)
#elif defined(LUX_SHARED)
#define LUX_API __declspec(dllimport)
#else
#define LUX_API
#endif
#elif defined(__GNUC__)
#define LUX_API __attribute__((visibility("default")))
#else
#define LUX_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum lux_status {
    LUX_OK = 0,
    LUX_INVALID_ARGUMENT = 1,
    LUX_NOT_FOUND = 2,
    LUX_OUT_OF_MEMORY = 3,
    LUX_INTERNAL_ERROR = 4,
    LUX_IO_ERROR = 5,
    LUX_INVALID_FORMAT = 6,
    LUX_INVALID_QUERY = 7
} lux_status;

typedef struct lux_index lux_index;
typedef struct lux_hits lux_hits;
typedef struct lux_strings lux_strings;
typedef struct lux_text lux_text;
typedef struct lux_prepared lux_prepared;

#define LUX_PREPARE_STORE_BODY 1u

typedef struct lux_config {
    int positions;
} lux_config;

typedef struct lux_snippet_options {
    size_t length;
    const char* start_marker;
    size_t start_marker_length;
    const char* end_marker;
    size_t end_marker_length;
    const char* ellipsis;
    size_t ellipsis_length;
} lux_snippet_options;

LUX_API const char* lux_status_message(lux_status status);

LUX_API lux_status lux_index_create(lux_index** out);
LUX_API lux_status lux_index_create_with(const lux_config* config, lux_index** out);
LUX_API void lux_index_free(lux_index* index);

LUX_API lux_status lux_index_add(lux_index* index, const char* key, size_t key_length, const char* title,
                                 size_t title_length, const char* body, size_t body_length);
/* Safe to call from many threads at once, alongside searches and commits on the same index.
 * Without LUX_PREPARE_STORE_BODY the body is indexed but not stored: lux_index_stored and
 * lux_index_snippet report it as not found, and lux_index_snippet_text highlights a copy the caller keeps. */
LUX_API lux_status lux_index_prepare(const lux_index* index, const char* title, size_t title_length, const char* body,
                                     size_t body_length, unsigned flags, lux_prepared** out);
/* Takes ownership of prepared whatever the result. */
LUX_API lux_status lux_index_commit(lux_index* index, const char* key, size_t key_length, lux_prepared* prepared);
LUX_API void lux_prepared_free(lux_prepared* prepared);
LUX_API lux_status lux_index_remove(lux_index* index, const char* key, size_t key_length, int* removed);
LUX_API lux_status lux_index_size(const lux_index* index, size_t* size);

LUX_API lux_status lux_index_save(const lux_index* index, const char* path, size_t path_length);
LUX_API lux_status lux_index_load(const char* path, size_t path_length, lux_index** out);
LUX_API lux_status lux_index_load_with(const char* path, size_t path_length, const lux_config* config,
                                       lux_index** out);

LUX_API lux_status lux_index_stored(const lux_index* index, const char* key, size_t key_length, const char* field,
                                    size_t field_length, lux_text** out);
LUX_API lux_status lux_index_search(const lux_index* index, const char* query, size_t query_length, size_t limit,
                                    lux_hits** out);
LUX_API lux_status lux_index_search_query(const lux_index* index, const char* query, size_t query_length,
                                          size_t limit, lux_hits** out, size_t* error_offset);
LUX_API lux_status lux_index_snippet(const lux_index* index, const char* key, size_t key_length, const char* field,
                                     size_t field_length, const char* query, size_t query_length,
                                     const lux_snippet_options* options, lux_text** out);
LUX_API lux_status lux_index_snippet_query(const lux_index* index, const char* key, size_t key_length,
                                           const char* field, size_t field_length, const char* query,
                                           size_t query_length, const lux_snippet_options* options, lux_text** out,
                                           size_t* error_offset);
LUX_API lux_status lux_index_snippet_text(const lux_index* index, const char* text, size_t text_length,
                                          const char* query, size_t query_length, const lux_snippet_options* options,
                                          lux_text** out);
LUX_API lux_status lux_index_snippet_text_query(const lux_index* index, const char* text, size_t text_length,
                                                const char* query, size_t query_length,
                                                const lux_snippet_options* options, lux_text** out,
                                                size_t* error_offset);
LUX_API lux_status lux_index_suggest(const lux_index* index, const char* query, size_t query_length, size_t limit,
                                     lux_strings** out);

LUX_API lux_status lux_index_debug_stats(const lux_index* index, lux_text** out);

LUX_API size_t lux_hits_count(const lux_hits* hits);
LUX_API const char* lux_hits_key(const lux_hits* hits, size_t position, size_t* length);
LUX_API double lux_hits_score(const lux_hits* hits, size_t position);
LUX_API void lux_hits_free(lux_hits* hits);

LUX_API size_t lux_strings_count(const lux_strings* strings);
LUX_API const char* lux_strings_get(const lux_strings* strings, size_t position, size_t* length);
LUX_API void lux_strings_free(lux_strings* strings);

LUX_API const char* lux_text_data(const lux_text* text);
LUX_API size_t lux_text_length(const lux_text* text);
LUX_API void lux_text_free(lux_text* text);

#ifdef __cplusplus
}
#endif
