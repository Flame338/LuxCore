#include "luxcore_c.h"

#include <stdio.h>
#include <stdlib.h>
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

static void temp_path(char* buffer, size_t capacity, const char* name) {
    const char* directory = getenv("TMPDIR");
    if (directory == NULL || directory[0] == '\0') {
        directory = getenv("TEMP");
    }
    if (directory == NULL || directory[0] == '\0') {
        directory = getenv("TMP");
    }
    if (directory == NULL || directory[0] == '\0') {
        directory = ".";
    }
    snprintf(buffer, capacity, "%s/%s", directory, name);
}

static void write_file(const char* path, const char* data, size_t length) {
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(data, 1, length, file) == length);
        fclose(file);
    }
}

static size_t read_file(const char* path, char* data, size_t capacity) {
    size_t length = 0;
    FILE* file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        length = fread(data, 1, capacity, file);
        fclose(file);
    }
    return length;
}

static lux_status load_from(const char* path, lux_index** out) {
    return lux_index_load(path, strlen(path), out);
}

static int same_hits(const lux_index* a, const lux_index* b, const char* query) {
    lux_hits* left = NULL;
    lux_hits* right = NULL;
    size_t i;
    int same;

    CHECK_STATUS(lux_index_search(a, query, strlen(query), 10, &left), LUX_OK);
    CHECK_STATUS(lux_index_search(b, query, strlen(query), 10, &right), LUX_OK);
    same = lux_hits_count(left) == lux_hits_count(right) && lux_hits_count(left) > 0;
    for (i = 0; same && i < lux_hits_count(left); i++) {
        size_t left_length = 0;
        size_t right_length = 0;
        const char* left_key = lux_hits_key(left, i, &left_length);
        const char* right_key = lux_hits_key(right, i, &right_length);
        same = left_length == right_length && memcmp(left_key, right_key, left_length) == 0 &&
               lux_hits_score(left, i) == lux_hits_score(right, i);
    }
    lux_hits_free(left);
    lux_hits_free(right);
    return same;
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

static void a_saved_index_loads_back_with_the_same_results(void) {
    char path[512];
    lux_index* original = notes();
    lux_index* loaded = NULL;
    lux_strings* before = NULL;
    lux_strings* after = NULL;
    lux_text* text = NULL;
    const char* first;
    size_t size = 0;
    size_t length = 0;

    temp_path(path, sizeof path, "luxcore_c_round_trip.lux");
    remove(path);

    CHECK_STATUS(lux_index_save(original, path, strlen(path)), LUX_OK);
    CHECK_STATUS(load_from(path, &loaded), LUX_OK);
    CHECK(loaded != NULL);

    CHECK_STATUS(lux_index_size(loaded, &size), LUX_OK);
    CHECK(size == 3);
    CHECK(same_hits(original, loaded, "raincoat"));
    CHECK(same_hits(original, loaded, "shoping lis"));
    CHECK(same_hits(original, loaded, "brea"));

    CHECK_STATUS(lux_index_stored(loaded, STR("trip"), STR("body"), &text), LUX_OK);
    CHECK(equals(lux_text_data(text), lux_text_length(text), "book the train to Edinburgh and pack a raincoat"));
    lux_text_free(text);

    CHECK_STATUS(lux_index_suggest(original, STR("rea"), 5, &before), LUX_OK);
    CHECK_STATUS(lux_index_suggest(loaded, STR("rea"), 5, &after), LUX_OK);
    CHECK(lux_strings_count(after) == lux_strings_count(before));
    CHECK(lux_strings_count(after) == 1);
    first = lux_strings_get(after, 0, &length);
    CHECK(equals(first, length, "reading"));

    lux_strings_free(before);
    lux_strings_free(after);
    lux_index_free(original);
    lux_index_free(loaded);
    remove(path);
}

static void a_loaded_index_can_be_changed_and_saved_again(void) {
    char first[512];
    char second[512];
    lux_index* original = notes();
    lux_index* loaded = NULL;
    lux_index* again = NULL;
    lux_hits* hits = NULL;
    size_t size = 0;
    int removed = 0;

    temp_path(first, sizeof first, "luxcore_c_resave_first.lux");
    temp_path(second, sizeof second, "luxcore_c_resave_second.lux");
    remove(first);
    remove(second);

    CHECK_STATUS(lux_index_save(original, first, strlen(first)), LUX_OK);
    CHECK_STATUS(load_from(first, &loaded), LUX_OK);
    CHECK_STATUS(lux_index_add(loaded, STR("garden"), STR("Garden"), STR("tomatoes need water")), LUX_OK);
    CHECK_STATUS(lux_index_remove(loaded, STR("shopping"), &removed), LUX_OK);
    CHECK(removed == 1);
    CHECK_STATUS(lux_index_save(loaded, second, strlen(second)), LUX_OK);
    CHECK_STATUS(load_from(second, &again), LUX_OK);

    CHECK_STATUS(lux_index_size(again, &size), LUX_OK);
    CHECK(size == 3);
    CHECK_STATUS(lux_index_search(again, STR("tomatoes"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    CHECK(hit_is(hits, 0, "garden"));
    lux_hits_free(hits);
    CHECK_STATUS(lux_index_search(again, STR("bread"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 0);
    lux_hits_free(hits);

    lux_index_size(original, &size);
    CHECK(size == 3);

    lux_index_free(original);
    lux_index_free(loaded);
    lux_index_free(again);
    remove(first);
    remove(second);
}

static void load_and_save_report_io_and_format_failures(void) {
    char missing[512];
    char garbage[512];
    char truncated[512];
    char nowhere[512];
    char bytes[4096];
    size_t length;
    lux_index* original = notes();
    lux_index* loaded = original;

    temp_path(missing, sizeof missing, "luxcore_c_missing.lux");
    temp_path(garbage, sizeof garbage, "luxcore_c_garbage.lux");
    temp_path(truncated, sizeof truncated, "luxcore_c_truncated.lux");
    temp_path(nowhere, sizeof nowhere, "luxcore_c_no_such_directory/index.lux");
    remove(missing);
    remove(garbage);
    remove(truncated);

    CHECK_STATUS(load_from(missing, &loaded), LUX_IO_ERROR);
    CHECK(loaded == NULL);

    loaded = original;
    write_file(garbage, STR("hello, this is not an index"));
    CHECK_STATUS(load_from(garbage, &loaded), LUX_INVALID_FORMAT);
    CHECK(loaded == NULL);

    loaded = original;
    CHECK_STATUS(lux_index_save(original, truncated, strlen(truncated)), LUX_OK);
    length = read_file(truncated, bytes, sizeof bytes);
    CHECK(length > 2 && length < sizeof bytes);
    write_file(truncated, bytes, length / 2);
    CHECK_STATUS(load_from(truncated, &loaded), LUX_INVALID_FORMAT);
    CHECK(loaded == NULL);

    CHECK_STATUS(lux_index_save(original, nowhere, strlen(nowhere)), LUX_IO_ERROR);

    lux_index_free(original);
    remove(garbage);
    remove(truncated);
}

static void save_and_load_reject_null_and_empty_paths(void) {
    lux_index* index = notes();
    lux_index* loaded = index;

    CHECK_STATUS(lux_index_save(NULL, STR("x.lux")), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_save(index, NULL, 0), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_save(index, NULL, 5), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_save(index, STR("")), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_load(NULL, 0, &loaded), LUX_INVALID_ARGUMENT);
    CHECK(loaded == NULL);
    loaded = index;
    CHECK_STATUS(lux_index_load(STR(""), &loaded), LUX_INVALID_ARGUMENT);
    CHECK(loaded == NULL);
    CHECK_STATUS(lux_index_load(STR("x.lux"), NULL), LUX_INVALID_ARGUMENT);

    lux_index_free(index);
}

static int text_is(const lux_text* text, const char* expected) {
    return equals(lux_text_data(text), lux_text_length(text), expected);
}

static lux_index* phrase_notes(int positions) {
    lux_index* index = NULL;
    lux_config config;
    config.positions = positions;
    CHECK_STATUS(lux_index_create_with(&config, &index), LUX_OK);
    CHECK_STATUS(lux_index_add(index, STR("fox"), STR("Animals"), STR("the quick brown fox jumps over the lazy dog")),
                 LUX_OK);
    CHECK_STATUS(lux_index_add(index, STR("dog"), STR("Pets"), STR("a lazy brown dog sleeps")), LUX_OK);
    return index;
}

static void search_query_understands_fields_and_boolean_operators(void) {
    lux_index* index = notes();
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search_query(index, STR("title:list"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 2);
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search_query(index, STR("title:book"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 0);
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search_query(index, STR("+book -train"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    CHECK(hit_is(hits, 0, "reading"));
    CHECK(lux_hits_score(hits, 0) > 0.0);
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search_query(index, STR("milk OR raincoat"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 2);
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search_query(index, STR("book"), 1, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search_query(index, STR("  "), 10, &hits), LUX_OK);
    CHECK(hits != NULL);
    CHECK(lux_hits_count(hits) == 0);
    lux_hits_free(hits);

    lux_index_free(index);
}

static void search_query_reports_a_parse_failure_as_invalid_query(void) {
    lux_index* index = notes();
    lux_hits* hits = (lux_hits*)index;
    lux_status status = lux_index_search_query(index, STR("book (train"), 10, &hits);

    CHECK(status == LUX_INVALID_QUERY);
    CHECK(status == 7);
    CHECK(hits == NULL);
    CHECK(strcmp(lux_status_message(status), "invalid query") == 0);

    CHECK_STATUS(lux_index_search_query(index, STR("\"unterminated"), 10, &hits), LUX_INVALID_QUERY);
    CHECK_STATUS(lux_index_search_query(index, STR("title:"), 10, &hits), LUX_INVALID_QUERY);

    lux_index_free(index);
}

static void phrase_queries_need_positions(void) {
    lux_index* plain = phrase_notes(0);
    lux_index* positional = phrase_notes(1);
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_search_query(plain, STR("\"quick brown\""), 10, &hits), LUX_INVALID_QUERY);
    CHECK(hits == NULL);
    CHECK_STATUS(lux_index_search_query(plain, STR("wi-fi"), 10, &hits), LUX_INVALID_QUERY);

    CHECK_STATUS(lux_index_search_query(positional, STR("\"quick brown\""), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    CHECK(hit_is(hits, 0, "fox"));
    lux_hits_free(hits);

    CHECK_STATUS(lux_index_search_query(positional, STR("\"brown quick\""), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 0);
    lux_hits_free(hits);

    lux_index_free(plain);
    lux_index_free(positional);
}

static void create_with_a_null_config_uses_the_defaults(void) {
    lux_index* index = NULL;
    lux_hits* hits = NULL;

    CHECK_STATUS(lux_index_create_with(NULL, &index), LUX_OK);
    CHECK(index != NULL);
    CHECK_STATUS(lux_index_add(index, STR("fox"), STR("Animals"), STR("the quick brown fox")), LUX_OK);
    CHECK_STATUS(lux_index_search_query(index, STR("\"quick brown\""), 10, &hits), LUX_INVALID_QUERY);
    CHECK_STATUS(lux_index_search_query(index, STR("quick"), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);

    lux_hits_free(hits);
    lux_index_free(index);
}

static void positions_survive_save_and_load_with_a_config(void) {
    char path[512];
    lux_index* original = phrase_notes(1);
    lux_index* with_positions = NULL;
    lux_index* without_positions = NULL;
    lux_index* defaulted = NULL;
    lux_config config;
    lux_hits* hits = NULL;

    temp_path(path, sizeof path, "luxcore_c_positions.lux");
    remove(path);
    CHECK_STATUS(lux_index_save(original, path, strlen(path)), LUX_OK);

    config.positions = 1;
    CHECK_STATUS(lux_index_load_with(path, strlen(path), &config, &with_positions), LUX_OK);
    CHECK_STATUS(lux_index_search_query(with_positions, STR("\"quick brown\""), 10, &hits), LUX_OK);
    CHECK(lux_hits_count(hits) == 1);
    lux_hits_free(hits);

    config.positions = 0;
    CHECK_STATUS(lux_index_load_with(path, strlen(path), &config, &without_positions), LUX_OK);
    CHECK_STATUS(lux_index_search_query(without_positions, STR("\"quick brown\""), 10, &hits), LUX_INVALID_QUERY);

    CHECK_STATUS(lux_index_load_with(path, strlen(path), NULL, &defaulted), LUX_OK);
    CHECK_STATUS(lux_index_search_query(defaulted, STR("\"quick brown\""), 10, &hits), LUX_INVALID_QUERY);

    lux_index_free(original);
    lux_index_free(with_positions);
    lux_index_free(without_positions);
    lux_index_free(defaulted);
    remove(path);
}

static void snippet_marks_what_search_matched(void) {
    lux_index* index = notes();
    lux_text* text = (lux_text*)index;

    CHECK_STATUS(lux_index_snippet(index, STR("reading"), STR("body"), STR("lucne ranks"), NULL, &text), LUX_OK);
    CHECK(text_is(text, "a book about how <em>Lucene</em> <em>ranks</em> search results"));
    lux_text_free(text);

    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("lucne"), NULL, &text), LUX_OK);
    CHECK(text_is(text, "a book about how Lucene ranks search results"));
    lux_text_free(text);

    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("+book OR ranks"), NULL, &text),
                 LUX_OK);
    CHECK(text_is(text, "a <em>book</em> about how Lucene <em>ranks</em> search results"));
    lux_text_free(text);

    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("title:book"), NULL, &text), LUX_OK);
    CHECK(text_is(text, "a book about how Lucene ranks search results"));
    lux_text_free(text);

    lux_index_free(index);
}

static void snippet_options_override_markers_and_length(void) {
    lux_index* index = notes();
    lux_text* text = NULL;
    lux_snippet_options options;

    memset(&options, 0, sizeof options);
    options.length = 160;
    options.start_marker = "[";
    options.start_marker_length = 1;
    options.end_marker = "]";
    options.end_marker_length = 1;
    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("lucene"), &options, &text), LUX_OK);
    CHECK(text_is(text, "a book about how [Lucene] ranks search results"));
    lux_text_free(text);

    options.start_marker = "<<partial>>";
    options.start_marker_length = 2;
    options.end_marker = NULL;
    options.end_marker_length = 0;
    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("lucene"), &options, &text), LUX_OK);
    CHECK(text_is(text, "a book about how <<Lucene</em> ranks search results"));
    lux_text_free(text);

    options.start_marker = "";
    options.start_marker_length = 0;
    options.end_marker = "";
    options.end_marker_length = 0;
    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("lucene"), &options, &text), LUX_OK);
    CHECK(text_is(text, "a book about how Lucene ranks search results"));
    lux_text_free(text);

    memset(&options, 0, sizeof options);
    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("body"), STR("lucene"), &options, &text), LUX_OK);
    CHECK(text_is(text, "a book about how <em>Lucene</em> ranks search results"));
    lux_text_free(text);

    lux_index_free(index);
}

static void snippet_options_set_the_ellipsis_and_the_length(void) {
    lux_index* index = NULL;
    lux_text* text = NULL;
    lux_snippet_options options;
    char body[2048];
    size_t used = 0;
    int i;

    for (i = 0; i < 30; i++) {
        used += (size_t)snprintf(body + used, sizeof body - used, "filler words fill space ");
    }
    used += (size_t)snprintf(body + used, sizeof body - used, "needle appears here ");
    for (i = 0; i < 30; i++) {
        used += (size_t)snprintf(body + used, sizeof body - used, "filler words fill space ");
    }

    CHECK_STATUS(lux_index_create(&index), LUX_OK);
    CHECK_STATUS(lux_index_add(index, STR("long"), STR("Long"), body, used), LUX_OK);

    memset(&options, 0, sizeof options);
    options.length = 60;
    options.ellipsis = "~";
    options.ellipsis_length = 1;
    CHECK_STATUS(lux_index_snippet_query(index, STR("long"), STR("body"), STR("needle"), &options, &text), LUX_OK);
    CHECK(lux_text_length(text) < 160);
    CHECK(lux_text_data(text)[0] == '~');
    CHECK(strstr(lux_text_data(text), "<em>needle</em>") != NULL);
    CHECK(strstr(lux_text_data(text), "...") == NULL);
    lux_text_free(text);

    options.ellipsis = "";
    options.ellipsis_length = 0;
    CHECK_STATUS(lux_index_snippet_query(index, STR("long"), STR("body"), STR("needle"), &options, &text), LUX_OK);
    CHECK(strstr(lux_text_data(text), "<em>needle</em>") != NULL);
    CHECK(lux_text_data(text)[0] != '~');
    CHECK(lux_text_data(text)[0] != '.');
    lux_text_free(text);

    options.ellipsis = NULL;
    CHECK_STATUS(lux_index_snippet_query(index, STR("long"), STR("body"), STR("needle"), &options, &text), LUX_OK);
    CHECK(strncmp(lux_text_data(text), "...", 3) == 0);
    lux_text_free(text);

    lux_index_free(index);
}

static void snippet_reports_unknown_keys_and_fields_as_not_found(void) {
    lux_index* index = notes();
    lux_text* text = (lux_text*)index;

    CHECK_STATUS(lux_index_snippet(index, STR("missing"), STR("body"), STR("book"), NULL, &text), LUX_NOT_FOUND);
    CHECK(text == NULL);
    CHECK_STATUS(lux_index_snippet(index, STR("reading"), STR("author"), STR("book"), NULL, &text), LUX_NOT_FOUND);
    CHECK_STATUS(lux_index_snippet_query(index, STR("missing"), STR("body"), STR("book"), NULL, &text), LUX_NOT_FOUND);
    CHECK(text == NULL);
    CHECK_STATUS(lux_index_snippet_query(index, STR("reading"), STR("author"), STR("book"), NULL, &text), LUX_NOT_FOUND);

    lux_index_free(index);
}

static void snippet_query_reports_a_bad_query_as_invalid_query(void) {
    lux_index* plain = phrase_notes(0);
    lux_text* text = (lux_text*)plain;

    CHECK_STATUS(lux_index_snippet_query(plain, STR("fox"), STR("body"), STR("(quick"), NULL, &text), LUX_INVALID_QUERY);
    CHECK(text == NULL);
    CHECK_STATUS(lux_index_snippet_query(plain, STR("fox"), STR("body"), STR("\"quick brown\""), NULL, &text),
                 LUX_INVALID_QUERY);

    lux_index_free(plain);
}

static void new_functions_reject_null_arguments(void) {
    lux_index* index = notes();
    lux_hits* hits = (lux_hits*)index;
    lux_text* text = (lux_text*)index;
    lux_index* created = (lux_index*)index;
    lux_config config;

    config.positions = 1;
    CHECK_STATUS(lux_index_create_with(&config, NULL), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_create_with(&config, &created), LUX_OK);
    lux_index_free(created);
    created = (lux_index*)index;
    CHECK_STATUS(lux_index_load_with(NULL, 1, &config, &created), LUX_INVALID_ARGUMENT);
    CHECK(created == NULL);
    CHECK_STATUS(lux_index_load_with("x", 0, &config, &created), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_load_with("x", 1, &config, NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_search_query(NULL, STR("q"), 10, &hits), LUX_INVALID_ARGUMENT);
    CHECK(hits == NULL);
    hits = (lux_hits*)index;
    CHECK_STATUS(lux_index_search_query(index, NULL, 1, 10, &hits), LUX_INVALID_ARGUMENT);
    CHECK(hits == NULL);
    CHECK_STATUS(lux_index_search_query(index, STR("q"), 10, NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_snippet(NULL, STR("k"), STR("body"), STR("q"), NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK(text == NULL);
    text = (lux_text*)index;
    CHECK_STATUS(lux_index_snippet(index, NULL, 1, STR("body"), STR("q"), NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet(index, STR("k"), NULL, 1, STR("q"), NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet(index, STR("k"), STR("body"), NULL, 1, NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet(index, STR("k"), STR("body"), STR("q"), NULL, NULL), LUX_INVALID_ARGUMENT);

    CHECK_STATUS(lux_index_snippet_query(NULL, STR("k"), STR("body"), STR("q"), NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet_query(index, NULL, 1, STR("body"), STR("q"), NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet_query(index, STR("k"), NULL, 1, STR("q"), NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet_query(index, STR("k"), STR("body"), NULL, 1, NULL, &text), LUX_INVALID_ARGUMENT);
    CHECK_STATUS(lux_index_snippet_query(index, STR("k"), STR("body"), STR("q"), NULL, NULL), LUX_INVALID_ARGUMENT);

    lux_index_free(index);
}

static void every_status_has_a_message(void) {
    CHECK(strcmp(lux_status_message(LUX_OK), "ok") == 0);
    CHECK(strlen(lux_status_message(LUX_INVALID_ARGUMENT)) > 0);
    CHECK(strlen(lux_status_message(LUX_NOT_FOUND)) > 0);
    CHECK(strlen(lux_status_message(LUX_OUT_OF_MEMORY)) > 0);
    CHECK(strlen(lux_status_message(LUX_INTERNAL_ERROR)) > 0);
    CHECK(strlen(lux_status_message(LUX_IO_ERROR)) > 0);
    CHECK(strlen(lux_status_message(LUX_INVALID_FORMAT)) > 0);
    CHECK(strlen(lux_status_message(LUX_INVALID_QUERY)) > 0);
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
        {"a_saved_index_loads_back_with_the_same_results", a_saved_index_loads_back_with_the_same_results},
        {"a_loaded_index_can_be_changed_and_saved_again", a_loaded_index_can_be_changed_and_saved_again},
        {"load_and_save_report_io_and_format_failures", load_and_save_report_io_and_format_failures},
        {"save_and_load_reject_null_and_empty_paths", save_and_load_reject_null_and_empty_paths},
        {"search_query_understands_fields_and_boolean_operators", search_query_understands_fields_and_boolean_operators},
        {"search_query_reports_a_parse_failure_as_invalid_query", search_query_reports_a_parse_failure_as_invalid_query},
        {"phrase_queries_need_positions", phrase_queries_need_positions},
        {"create_with_a_null_config_uses_the_defaults", create_with_a_null_config_uses_the_defaults},
        {"positions_survive_save_and_load_with_a_config", positions_survive_save_and_load_with_a_config},
        {"snippet_marks_what_search_matched", snippet_marks_what_search_matched},
        {"snippet_options_override_markers_and_length", snippet_options_override_markers_and_length},
        {"snippet_options_set_the_ellipsis_and_the_length", snippet_options_set_the_ellipsis_and_the_length},
        {"snippet_reports_unknown_keys_and_fields_as_not_found", snippet_reports_unknown_keys_and_fields_as_not_found},
        {"snippet_query_reports_a_bad_query_as_invalid_query", snippet_query_reports_a_bad_query_as_invalid_query},
        {"new_functions_reject_null_arguments", new_functions_reject_null_arguments},
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
