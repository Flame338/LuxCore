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
    LUX_INTERNAL_ERROR = 4
} lux_status;

typedef struct lux_index lux_index;
typedef struct lux_hits lux_hits;
typedef struct lux_strings lux_strings;
typedef struct lux_text lux_text;

LUX_API const char* lux_status_message(lux_status status);

LUX_API lux_status lux_index_create(lux_index** out);
LUX_API void lux_index_free(lux_index* index);

LUX_API lux_status lux_index_add(lux_index* index, const char* key, size_t key_length, const char* title,
                                 size_t title_length, const char* body, size_t body_length);
LUX_API lux_status lux_index_remove(lux_index* index, const char* key, size_t key_length, int* removed);
LUX_API lux_status lux_index_size(const lux_index* index, size_t* size);

LUX_API lux_status lux_index_stored(const lux_index* index, const char* key, size_t key_length, const char* field,
                                    size_t field_length, lux_text** out);
LUX_API lux_status lux_index_search(const lux_index* index, const char* query, size_t query_length, size_t limit,
                                    lux_hits** out);
LUX_API lux_status lux_index_suggest(const lux_index* index, const char* query, size_t query_length, size_t limit,
                                     lux_strings** out);

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
