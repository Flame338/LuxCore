#include "luxcore_c.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)\
    do {\
        if (!(condition)) {\
            printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);\
            failures++;\
        }\
    } while (0)

#define CHECK_STATUS(call, expected) CHECK((call) == (expected))

#define STR(literal) (literal), (sizeof(literal) - 1)

static int equals(const char* data, size_t length, const char* expected) {
    return data != NULL && length == strlen(expected) && memcmp(data, expected, length) == 0;
}

static int hit_is(const lux_hits* hits, size_t position, const char* expected) {
    size_t length = 0;
    const char* key = lux_hits_key(hits, position, &length);
    return equals(key, length, expected);
}

static lux_index* notes(void) {
    lux_index* index = NULL;
    CHECK_STATUS(lux_index_create(&index), LUX_OK);
    CHECK_STATUS(lux_index_add(index, STR("shopping"), STR("Shopping list"), STR("milk, eggs, bread and butter")), LUX_OK);
    CHECK_STATUS(lux_index_add(index, STR("reading"), STR("Reading list"),
                               STR("a book about how Lucene ranks search results")),
                 LUX_OK);
    CHECK_STATUS(lux_index_add(index, STR("trip"), STR("Trip planning"),
                               STR("book the train to Edinburgh and pack a raincoat")),
                 LUX_OK);
    return index;
}

static void search_finds_a_document_by_its_key(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search(index, STR("raincoat"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    CHECK(hit_is(hits, 0, "trip"));
    CHECK(lux_hits_score(hits, 0) > 0.0);

    lux_hits_free(hits);
    lux_index_free(index);
}

static void search_tolerates_a_typo_in_a_finished_word(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search(index, STR("shoping lis"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 2);
    CHECK(hit_is(hits, 0, "shopping"));
    CHECK(hit_is(hits, 1, "reading"));

    lux_hits_free(hits);
    lux_index_free(index);
}

static void search_limits_the_number_of_hits(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search(index, STR("book"), 1, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search(index, STR("book"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 2);
    lux_hits_free(hits);

    lux_index_free(index);
}

static void search_with_no_match_returns_an_empty_result(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search(index, STR(""), 10, &hits), LUX_OK);
    CHECK(hits != NULL);
    CHECK(lux_hits_count(hits) == 0);
    CHECK(lux_hits_key(hits, 0, NULL) == NULL);
    lux_hits_free(hits);

    lux_index_free(index);
}

static void search_reads_the_query_by_length_not_by_terminator(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search(index, "raincoat and more", 8, 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    CHECK(hit_is(hits, 0, "trip"));

    lux_hits_free(hits);
    lux_index_free(index);
}

static void suggest_completes_the_last_word(void) {
    lux_index* index = notes();
    lux_strings* suggestions = NULL;
    const char* first;
    size_t length = 0;

    CHECK_STATUS(lux_index_suggest(index, STR("rea"), 5, &suggestions), LUX_OK);
    CHECK(lux_strings_count(suggestions) == 1);
    first = lux_strings_get(suggestions, 0, &length);
    CHECK(equals(first, length, "reading"));
    CHECK(lux_strings_get(suggestions, 1, &length) == NULL);
    CHECK(length == 0);
    lux_strings_free(suggestions);

    CHECK_STATUS(lux_index_suggest(index, STR("zzz"), 5, &suggestions), LUX_OK);
    CHECK(lux_strings_count(suggestions) == 0);
    lux_strings_free(suggestions);

    lux_index_free(index);
}

static void remove_drops_the_document(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;
    size_t size = 0;
    int removed = -1;

    CHECK_STATUS(lux_index_size(index, &size), LUX_OK);
    CHECK(size == 3);

    CHECK_STATUS(lux_index_remove(index, STR("trip"), &removed), LUX_OK);
    CHECK(removed == 1);
    CHECK_STATUS(lux_index_remove(index, STR("trip"), &removed), LUX_OK);
    CHECK(removed == 0);

    CHECK_STATUS(lux_index_size(index, &size), LUX_OK);
    CHECK(size == 2);
    CHECK_STATUS(lux_index_search(index, STR("raincoat"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 0);

    lux_hits_free(hits);
    lux_index_free(index);
}

static void adding_an_existing_key_replaces_the_document(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;
    size_t size = 0;

    CHECK_STATUS(lux_index_add(index, STR("trip"), STR("Trip planning"), STR("pack a umbrella")), LUX_OK);
    CHECK_STATUS(lux_index_size(index, &size), LUX_OK);
    CHECK(size == 3);
    CHECK_STATUS(lux_index_search(index, STR("raincoat"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 0);
    lux_hits_free(hits);
    CHECK_STATUS(lux_index_search(index, STR("umbrella"), 10, &hits), LUX_OK);
    CHECK(hit_is(hits, 0, "trip"));
    lux_hits_free(hits);

    lux_index_free(index);
}

static void stored_returns_fields_by_key(void) {
    lux_index* index = notes();
    lux_text* text = NULL;

    CHECK_STATUS(lux_index_stored(index, STR("trip"), STR("title"), &text), LUX_OK);
    CHECK(equals(lux_text_data(text), lux_text_length(text), "Trip planning"));
    lux_text_free(text);

    CHECK_STATUS(lux_index_stored(index, STR("trip"), STR("body"), &text), LUX_OK);
    CHECK(equals(lux_text_data(text), lux_text_length(text), "book the train to Edinburgh and pack a raincoat"));
    lux_text_free(text);

    CHECK_STATUS(lux_index_stored(index, STR("trip"), STR("author"), &text), LUX_NOT_FOUND);
    CHECK(text == NULL);
    CHECK_STATUS(lux_index_stored(index, STR("missing"), STR("title"), &text), LUX_NOT_FOUND);
    CHECK(text == NULL);

    lux_index_free(index);
}

static void utf8_keys_and_text_round_trip(void) {
    static const char key[] = "caf\xC3\xA9-\xE6\x9D\xB1\xE4\xBA\xAC";
    static const char title[] = "Caf\xC3\xA9 \xE6\x9D\xB1\xE4\xBA\xAC";
    lux_index* index = NULL;
    lux_hits* hits = NULL;
    lux_text* text = NULL;

    CHECK_STATUS(lux_index_create(&index), LUX_OK);
    CHECK_STATUS(lux_index_add(index, key, sizeof(key) - 1, title, sizeof(title) - 1, STR("espresso and tea")), LUX_OK);

    CHECK_STATUS(lux_index_search(index, STR("espresso"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    CHECK(hit_is(hits, 0, key));
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_stored(index, key, sizeof(key) - 1, STR("title"), &text), LUX_OK);
    CHECK(equals(lux_text_data(text), lux_text_length(text), title));
    lux_text_free(text);

    lux_index_free(index);
}

static void an_empty_key_is_an_invalid_argument(void) {
    lux_index* index = NULL;
    size_t size = 99;
    lux_status status;

    CHECK_STATUS(lux_index_create(&index), LUX_OK);
    status = lux_index_add(index, STR(""), STR("title"), STR("body"));
    CHECK(status == LUX_INVALID_ARGUMENT);
    CHECK(strcmp(lux_status_message(status), "invalid argument") == 0);
    CHECK_STATUS(lux_index_size(index, &size), LUX_OK);
    CHECK(size == 0);

    lux_index_free(index);
}

static void null_arguments_are_invalid(void) {
    lux_index* index = notes();
    lux_hits* hits = (lux_hits*)index;
    lux_strings* suggestions = (lux_strings*)index;
    lux_text* text = (lux_text*)index;
    size_t size = 0;
    int removed = 0;

    CHECK_STATUS(lux_index_create(NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_add(NULL, STR("k"), STR("t"), STR("b")), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_add(index, NULL, 1, STR("t"), STR("b")), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_add(index, STR("k"), NULL, 1, STR("b")), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_add(index, STR("k"), STR("t"), NULL, 1), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_remove(NULL, STR("k"), &removed), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_remove(index, NULL, 1, &removed), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_remove(index, STR("k"), NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_size(NULL, &size), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_size(index, NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_search(NULL, STR("q"), 10, &hits), LUX_INVALID_ARGUMENT);
    CHECK(hits == NULL);
    hits = (lux_hits*)index;
    CHECK_STATUS(lux_index_search(index, NULL, 1, 10, &hits), LUX_INVALID_ARGUMENT);
    CHECK(hits == NULL);
    CHECK_STATUS(lux_index_search(index, STR("q"), 10, NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_suggest(NULL, STR("q"), 5, &suggestions), LUX_INVALID_ARGUMENT);
    CHECK(suggestions == NULL);
    CHECK_STATUS(lux_index_suggest(index, NULL, 1, 5, &suggestions), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_suggest(index, STR("q"), 5, NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_stored(NULL, STR("k"), STR("title"), &text), LUX_INVALID_ARGUMENT);
    CHECK(text == NULL);
    CHECK_STATUS(lux_index_stored(index, NULL, 1, STR("title"), &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_stored(index, STR("k"), NULL, 1, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_stored(index, STR("k"), STR("title"), NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_size(index, &size), LUX_OK);
    CHECK(size == 3);

    lux_index_free(index);
}

static void accessors_and_free_functions_accept_null(void) {
    size_t length = 7;

    CHECK(lux_hits_count(NULL) == 0);
    CHECK(lux_hits_key(NULL, 0, &length) == NULL);
    CHECK(length == 0);
    CHECK(lux_hits_score(NULL, 0) == 0.0);
    CHECK(lux_strings_count(NULL) == 0);
    CHECK(lux_strings_get(NULL, 0, &length) == NULL);
    CHECK(lux_text_data(NULL) == NULL);
    CHECK(lux_text_length(NULL) == 0);

    lux_index_free(NULL);
    lux_hits_free(NULL);
    lux_strings_free(NULL);
    lux_text_free(NULL);
}

static void every_status_has_a_message(void) {
    CHECK(strcmp(lux_status_message(LUX_OK), "ok") == 0);
    CHECK(strlen(lux_status_message(LUX_INVALID_ARGUMENT)) > 0);
    CHECK(strlen(lux_status_message(LUX_NOT_FOUND)) > 0);
    CHECK(strlen(lux_status_message(LUX_OUT_OF_MEMORY)) > 0);
    CHECK(strlen(lux_status_message(LUX_INTERNAL_ERROR)) > 0);
    CHECK(strlen(lux_status_message((lux_status)99)) > 0);
}

typedef struct {
    const char* name;
    void (*run)(void);
} test_case;

int main(void) {
    static const test_case tests[] = {
        {"search_finds_a_document_by_its_key", search_finds_a_document_by_its_key},
        {"search_tolerates_a_typo_in_a_finished_word", search_tolerates_a_typo_in_a_finished_word},
        {"search_limits_the_number_of_hits", search_limits_the_number_of_hits},
        {"search_with_no_match_returns_an_empty_result", search_with_no_match_returns_an_empty_result},
        {"search_reads_the_query_by_length_not_by_terminator", search_reads_the_query_by_length_not_by_terminator},
        {"suggest_completes_the_last_word", suggest_completes_the_last_word},
        {"remove_drops_the_document", remove_drops_the_document},
        {"adding_an_existing_key_replaces_the_document", adding_an_existing_key_replaces_the_document},
        {"stored_returns_fields_by_key", stored_returns_fields_by_key},
        {"utf8_keys_and_text_round_trip", utf8_keys_and_text_round_trip},
        {"an_empty_key_is_an_invalid_argument", an_empty_key_is_an_invalid_argument},
        {"null_arguments_are_invalid", null_arguments_are_invalid},
        {"accessors_and_free_functions_accept_null", accessors_and_free_functions_accept_null},
        {"every_status_has_a_message", every_status_has_a_message},
    };
    const size_t count = sizeof(tests) / sizeof(tests[0]);
    int failed = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        const int failures_before = failures;
        tests[i].run();
        if (failures > failures_before) {
            failed++;
            printf("FAIL  %s\n", tests[i].name);
        } else {
            printf("PASS  %s\n", tests[i].name);
        }
    }

    printf("\n%d C tests, %d failed\n", (int)count, failed);
    return failed == 0 ? 0 : 1;
}
