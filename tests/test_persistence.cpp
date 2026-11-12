#include "TestFramework.h"

#include "CombinedPostings.h"
#include "ByteIO.h"
#include "DocumentStore.h"
#include "Indexer.h"
#include "LuxCore.h"
#include "QueryParser.h"
#include "Searcher.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string number(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.17g", value);
    return buffer;
}

std::string hits_text(const std::vector<SearchHit>& hits) {
    std::string text;
    for (const SearchHit& hit : hits) {
        text += "[" + std::to_string(hit.doc_id) + "," + hit.key + "," + number(hit.score) + "]";
    }
    return text;
}

std::string describe_index(const InvertedIndex& index, int ids) {
    std::string text = "docs=" + std::to_string(index.get_total_documents()) +
                       " terms=" + std::to_string(index.get_unique_terms_count()) + "\n";
    for (const std::string_view field : index.field_names()) {
        text += "field " + std::string(field) + " docs=" + std::to_string(index.get_field_documents(field)) +
                " average=" + number(index.get_average_field_length(field)) + "\n lengths";
        for (int id = 0; id < ids; id++) {
            text += " " + std::to_string(index.get_field_length(field, id));
        }
        for (const TermPostings& match : index.terms_with_prefix(field, "")) {
            const std::optional<PostingList> list = index.get_posting_list(field, match.term);
            text += "\n " + std::string(match.term) + ":";
            for (std::size_t i = 0; i < list->postings.size(); i++) {
                text += "(" + std::to_string(list->postings[i].doc_id) + "x" +
                        std::to_string(list->postings[i].term_frequency) + "@";
                for (const std::uint32_t position : list->positions_of(i)) {
                    text += std::to_string(position) + ",";
                }
                text += ")";
            }
        }
        text += "\n";
    }
    text += "combined\n";
    for (const TermPostings& match : index.terms_with_prefix("")) {
        text += " " + std::string(match.term) + " df=" + std::to_string(match.document_frequency) + ":";
        for (const Posting& posting : combined_postings(index, std::string(match.term))) {
            text += "(" + std::to_string(posting.doc_id) + "x" + std::to_string(posting.term_frequency) + ")";
        }
        text += "\n";
    }
    for (const char* probe : {"rocket", "launch", "cafe", "fox", "getuserbyid", "\xe4\xb8\x9c\xe4\xba\xac", "garden"}) {
        text += std::string("fuzzy ") + probe + ":";
        for (const FuzzyMatch& match : index.fuzzy_terms(probe, 2)) {
            text += match.term + "/" + std::to_string(match.distance) + ",";
        }
        text += "\n";
    }
    return text;
}

std::string describe(const Indexer& indexer, int ids) {
    std::string text = describe_index(indexer.get_index(), ids);
    for (int id = 0; id < ids; id++) {
        text += "doc " + std::to_string(id) + (indexer.contains(id) ? " live key=" : " gone key=") +
                indexer.get_key(id);
        const std::string& key = indexer.get_key(id);
        if (!key.empty()) {
            const std::optional<int> found = indexer.find_id(key);
            text += " found=" + (found ? std::to_string(*found) : std::string("-"));
        }
        if (indexer.contains(id)) {
            for (const Field& field : indexer.get_document(id).get_fields()) {
                text += " {" + field.name + "=" + field.value + "|" + std::to_string(field.options.stored) +
                        std::to_string(field.options.indexed) + std::to_string(field.options.analyzed) + "}";
            }
        }
        text += "\n";
    }
    return text;
}

const char* const queries[] = {"rocket",   "rock",       "rcket launch", "cafe",   "r\xc3\xa9sum\xc3\xa9",
                               "naive",    "\xe4\xb8\x9c\xe4\xba\xac", "\xe4\xb8\x9c\xe4\xba\xac\xe5\xa4\xa7",
                               "getuserbyid", "get user", "parse_http", "fox den", "urgent", "ano",
                               "launc",    "zzzz",       "swap",         "late",   "the",
                               "garden notes", "snake case", "http server"};

std::string run_query(const Indexer& indexer, const Query& query) {
    const Searcher searcher(indexer, {.scoring = Scoring::bm25_with()});
    std::string text = hits_text(searcher.search(query, 10)) + " ids=";
    for (const int id : searcher.matching_ids(query)) {
        text += std::to_string(id) + ",";
    }
    return text;
}

std::string results_of(const Indexer& indexer) {
    std::string text;
    for (const Scoring& scoring : {Scoring::tf_idf(), Scoring::bm25_with()}) {
        for (const Fuzziness fuzziness : {Fuzziness::Off, Fuzziness::On}) {
            const Searcher searcher(indexer, {.scoring = scoring, .fuzziness = fuzziness});
            for (const char* query : queries) {
                text += std::string(query) + " => " + hits_text(searcher.search(query, 10)) + " | ";
                for (const Suggestion& suggestion : searcher.suggest(query, 5)) {
                    text += suggestion.term + "#" + std::to_string(suggestion.doc_freq) + ",";
                }
                text += " | " + hits_text(searcher.search_in("title", query, 10)) + "\n";
            }
        }
    }

    text += run_query(indexer, PhraseQuery(std::vector<std::string>{"rocket", "launch"})) + "\n";
    text += run_query(indexer, PhraseQuery(std::vector<std::string>{"rocket", "launch"}, "body")) + "\n";
    text += run_query(indexer, PhraseQuery(std::vector<PhraseTerm>{{"rocket", 0}, {"pad", 2}})) + "\n";
    text += run_query(indexer, PhraseQuery(std::vector<std::string>{"fox", "den"}, "body")) + "\n";
    text += run_query(indexer, PhraseQuery(std::vector<std::string>{"den", "fox"}, "body")) + "\n";
    text += run_query(indexer, PhraseQuery(std::vector<std::string>{"red", "fox"}, "body")) + "\n";
    const QueryParser parser(indexer.get_analyzer());
    for (const char* parsed : {"\"fox den\"", "\"rocket launch\" OR cafe", "+rocket -pad", "title:garden body:(fox OR den)",
                               "\"snake case name\"", "tag:Urgent"}) {
        const ParseResult result = parser.parse(parsed);
        text += std::string(parsed) + " => " + (result.ok() ? run_query(indexer, *result.query) : "error") + "\n";
    }
    return text;
}

Indexer varied_corpus() {
    Indexer indexer("stopwords.txt");
    indexer.add_document("rocket", make_text_document("Rocket launch", "The rocket launch pad is ready and the launch is soon"));

    Document many;
    many.add({"title", "Garden notes"});
    many.add({"body", "red fox den"});
    many.add({"body", "fox den red"});
    many.add({"tag", "Urgent", FieldOptions::keyword()});
    many.add({"memo", "kept but never searched", FieldOptions::stored_only()});
    many.add({"hidden", "indexed but not stored", FieldOptions{false, true, true}});
    indexer.add_document("many", many);

    indexer.add_document("accents", make_text_document("Caf\xc3\xa9 r\xc3\xa9sum\xc3\xa9", "na\xc3\xafve Zo\xc3\xab at the caf\xc3\xa9"));
    indexer.add_document("cjk", make_text_document("\xe4\xb8\x9c\xe4\xba\xac\xe5\xa4\xa7\xe5\xad\xa6",
                                                  "\xe4\xba\xac\xe9\x83\xbd \xe5\xa4\xa7\xe9\x98\xaa \xe4\xb8\x9c\xe4\xba\xac\xe5\xa4\xa7\xe5\xad\xa6 \xe7\xa0\x94\xe7\xa9\xb6"));
    indexer.add_document("ids", make_text_document("getUserById", "parse_http_request HTTPServer2 snake_case_name getUserById"));
    indexer.add_document(make_text_document("Anonymous", "no key on this one"));
    indexer.add_document("stopwords-only", make_text_document("", "the and of"));

    indexer.add_document("gone-1", make_text_document("Doomed", "first doomed note"));
    indexer.add_document("gone-2", make_text_document("Doomed", "second doomed note about rocket pad"));
    const int keyless_gone = indexer.add_document(make_text_document("Doomed anonymous", "fox den"));
    indexer.add_document("swap", make_text_document("Before swap", "original text with fox"));
    indexer.remove_document("gone-1");
    indexer.remove_document("gone-2");
    indexer.remove_document(keyless_gone);
    indexer.add_document("swap", make_text_document("After swap", "replacement text about gardens"));
    indexer.add_document("late", make_text_document("Late arrival", "added after the removals"));
    return indexer;
}

constexpr int corpus_ids = 24;

Indexer small_corpus() {
    Indexer indexer;
    indexer.add_document("one", make_text_document("Alpha", "beta gamma alpha"));
    indexer.add_document("two", make_text_document("Gamma", "delta"));
    indexer.add_document(make_text_document("", "keyless epsilon"));
    indexer.add_document("three", make_text_document("Zeta", "eta theta"));
    indexer.remove_document("two");
    return indexer;
}

std::string mutate(Indexer& indexer) {
    std::string ids;
    ids += std::to_string(indexer.add_document("fresh", make_text_document("Fresh start", "brand new rocket document"))) + ",";
    ids += std::to_string(indexer.add_document("rocket", make_text_document("Rocket", "replaced text about gardens"))) + ",";
    ids += std::to_string(indexer.add_document(make_text_document("Anon", "another anonymous note"))) + ",";
    ids += indexer.remove_document("many") ? "removed," : "missing,";
    ids += indexer.remove_document(2) ? "removed," : "missing,";
    ids += indexer.remove_document(3) ? "removed," : "missing,";
    ids += std::to_string(indexer.add_document("stopwords-only", make_text_document("Now real", "words arrived"))) + ",";
    ids += std::to_string(indexer.add_document("again", make_text_document("Again", "fox den again"))) + ",";
    return ids;
}

template <typename Body>
std::string failure_of(Body body) {
    try {
        body();
    } catch (const std::runtime_error& error) {
        return error.what();
    } catch (...) {
        return "<other exception>";
    }
    return "<no error>";
}

bool rejected(const std::string& failure) {
    return failure != "<no error>" && failure != "<other exception>";
}

bool mentions(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

std::string with_body_length(std::string bytes, std::uint64_t length) {
    for (int i = 0; i < 8; i++) {
        bytes[8 + static_cast<std::size_t>(i)] = static_cast<char>(length >> (8 * i));
    }
    return bytes;
}

std::string framed(const std::string& body) {
    ByteWriter header;
    header.append(Indexer().serialize().substr(0, 8));
    header.u64(body.size());
    header.append(body);
    return header.take();
}

std::string load_failure(const std::string& bytes) {
    return failure_of([&] { Indexer::deserialize(bytes); });
}

void one_document_store(ByteWriter& writer) {
    writer.varint(1);
    writer.varint(1);
    writer.varint(0);
    writer.varint(0);
}

void no_keys(ByteWriter& writer) {
    writer.varint(0);
}

void one_posting_list(ByteWriter& writer, std::uint64_t gap, std::uint64_t packed) {
    writer.varint(1);
    writer.string("t");
    writer.varint(1);
    writer.string("a");
    writer.varint(1);
    writer.varint(gap);
    writer.varint(packed);
}

std::string with_postings(std::uint64_t gap, std::uint64_t packed) {
    ByteWriter body;
    one_document_store(body);
    no_keys(body);
    one_posting_list(body, gap, packed);
    return framed(body.bytes());
}

struct TempFile {
    std::filesystem::path path;

    explicit TempFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("luxcore_persistence_" + name + ".lux")) {
        std::filesystem::remove(path);
    }

    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    std::string str() const { return path.string(); }
};

std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_all(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string api_results(const luxcore::Index& index) {
    std::string text;
    for (const char* query : queries) {
        text += std::string(query) + " =>";
        for (const luxcore::Hit& hit : index.search(query, 10)) {
            text += " " + hit.key + "@" + number(hit.score);
        }
        text += " |";
        for (const std::string& term : index.suggest(query, 5)) {
            text += " " + term;
        }
        text += "\n";
    }
    return text;
}

std::string api_stored(const luxcore::Index& index) {
    std::string text;
    for (const char* key : {"rocket", "accents", "cjk", "ids", "swap", "late", "gone-1", "missing"}) {
        for (const char* field : {"title", "body", "other"}) {
            text += std::string(key) + "." + field + "=" + index.stored(key, field).value_or("<none>") + "\n";
        }
    }
    return text;
}

luxcore::Index api_corpus() {
    luxcore::Index index;
    index.add("rocket", "Rocket launch", "The rocket launch pad is ready and the launch is soon");
    index.add("accents", "Caf\xc3\xa9 r\xc3\xa9sum\xc3\xa9", "na\xc3\xafve Zo\xc3\xab at the caf\xc3\xa9");
    index.add("cjk", "\xe4\xb8\x9c\xe4\xba\xac\xe5\xa4\xa7\xe5\xad\xa6", "\xe4\xba\xac\xe9\x83\xbd \xe5\xa4\xa7\xe9\x98\xaa \xe7\xa0\x94\xe7\xa9\xb6");
    index.add("ids", "getUserById", "parse_http_request HTTPServer2 snake_case_name");
    index.add("gone-1", "Doomed", "first doomed note");
    index.add("swap", "Before swap", "original text with fox");
    index.remove("gone-1");
    index.add("swap", "After swap", "replacement text about gardens");
    index.add("late", "Late arrival", "added after the removals");
    return index;
}

}

TEST(round_trip_keeps_every_observable_result) {
    const Indexer original = varied_corpus();
    const Indexer loaded = Indexer::deserialize(original.serialize(), "stopwords.txt");

    const std::string expected = describe(original, corpus_ids);
    CHECK(mentions(expected, "doc 0 live key=rocket"));
    CHECK(mentions(expected, "gone key="));
    CHECK(mentions(expected, "|111}"));
    CHECK_EQ(describe(loaded, corpus_ids), expected);

    const std::string results = results_of(original);
    CHECK(mentions(results, "rocket => [0,rocket,"));
    CHECK(mentions(results, "[1,many,"));
    CHECK_EQ(results_of(loaded), results);
}

TEST(round_trip_of_postings_without_positions_and_mixed_postings) {
    DocumentStore store;
    for (int i = 0; i < 5; i++) {
        store.add(Document());
    }
    store.remove(3);

    InvertedIndex index;
    index.add_document(0, FieldTermFreqs{{"body", TermFreqs{{"cat", 3}, {"dog", 1}}}});
    index.add_document(1, FieldTermPositions{{"body", TermPositions{{"cat", {4, 9, 9, 20}}, {"fox", {0}}}}});
    index.add_document(2, FieldTermFreqs{{"body", TermFreqs{{"cat", 2}}}, {"title", TermFreqs{{"cat", 1}, {"owl", 7}}}});
    index.add_document(4, FieldTermFreqs{});

    ByteWriter writer;
    index.write(writer);
    ByteReader reader(writer.bytes());
    const InvertedIndex loaded = InvertedIndex::read(reader, store);

    CHECK(reader.at_end());
    CHECK_EQ(describe_index(loaded, 6), describe_index(index, 6));
    CHECK_EQ(loaded.get_total_documents(), 4);
    CHECK(loaded.get_posting_list("body", "cat")->positions_of(0).empty());
    CHECK_EQ(loaded.get_posting_list("body", "cat")->positions_of(1).size(), static_cast<std::size_t>(4));
    CHECK_EQ(loaded.get_field_length("body", 0), 4);
    CHECK_EQ(loaded.get_field_length("title", 2), 8);
}

TEST(a_loaded_index_survives_a_second_save_and_load_after_changes) {
    Indexer loaded = Indexer::deserialize(varied_corpus().serialize(), "stopwords.txt");
    mutate(loaded);
    const Indexer again = Indexer::deserialize(loaded.serialize(), "stopwords.txt");

    CHECK_EQ(describe(again, corpus_ids + 8), describe(loaded, corpus_ids + 8));
    CHECK_EQ(results_of(again), results_of(loaded));
}

TEST(every_truncation_of_a_saved_index_is_rejected) {
    const std::string bytes = small_corpus().serialize();

    CHECK(bytes.size() > 100);
    for (std::size_t length = 0; length < bytes.size(); length++) {
        const std::string failure = failure_of([&] { Indexer::deserialize(std::string_view(bytes).substr(0, length)); });
        CHECK(rejected(failure));
    }
}

TEST(every_truncation_of_the_content_is_rejected_even_with_a_matching_header) {
    const std::string bytes = small_corpus().serialize();

    for (std::size_t length = 16; length < bytes.size(); length++) {
        const std::string cut = with_body_length(bytes.substr(0, length), length - 16);
        const std::string failure = load_failure(cut);
        CHECK(rejected(failure));
    }
}

TEST(corrupting_any_single_byte_either_loads_or_is_rejected) {
    const std::string bytes = small_corpus().serialize();
    std::size_t rejections = 0;
    std::size_t loads = 0;

    for (std::size_t position = 0; position < bytes.size(); position++) {
        for (const unsigned char mask : {0x01, 0x10, 0x80, 0xFF}) {
            std::string corrupted = bytes;
            corrupted[position] = static_cast<char>(static_cast<unsigned char>(corrupted[position]) ^ mask);
            const std::string failure = failure_of([&] {
                Indexer loaded = Indexer::deserialize(corrupted);
                loaded.add_document("probe", make_text_document("alpha", "beta gamma"));
                Searcher(loaded.get_index(), loaded.get_analyzer()).search("alpha beta");
                Indexer::deserialize(loaded.serialize());
            });
            CHECK(failure != "<other exception>");
            if (failure == "<no error>") {
                loads++;
            } else {
                rejections++;
            }
        }
    }
    CHECK(rejections > bytes.size());
    CHECK(loads > 0);
}

TEST(random_damage_to_a_larger_index_either_loads_or_is_rejected) {
    const std::string bytes = varied_corpus().serialize();
    std::mt19937 random(20240607);
    std::size_t rejections = 0;

    for (int round = 0; round < 3000; round++) {
        std::string damaged = bytes;
        const int edits = 1 + static_cast<int>(random() % 4);
        for (int edit = 0; edit < edits; edit++) {
            damaged[16 + random() % (damaged.size() - 16)] = static_cast<char>(random() % 256);
        }
        const std::string failure = load_failure(damaged);
        CHECK(failure != "<other exception>");
        rejections += rejected(failure) ? 1 : 0;
    }
    CHECK(rejections > 1000);
}

TEST(random_garbage_behind_a_valid_header_is_rejected_or_loaded_safely) {
    std::mt19937 random(7);
    for (int round = 0; round < 3000; round++) {
        std::string body(random() % 48, '\0');
        for (char& byte : body) {
            byte = static_cast<char>(random() % 3 == 0 ? random() % 256 : random() % 4);
        }
        CHECK(load_failure(framed(body)) != "<other exception>");
    }
}

TEST(crafted_minimal_files_load_and_their_damaged_variants_are_rejected) {
    const Indexer valid = Indexer::deserialize(with_postings(0, 2));
    CHECK_EQ(valid.get_index().get_total_documents(), 1);
    CHECK_EQ(valid.get_index().get_postings("t", "a")->size(), static_cast<std::size_t>(1));

    CHECK(mentions(load_failure(with_postings(5, 2)), "missing document"));
    CHECK(mentions(load_failure(with_postings(0, 0)), "term frequency"));
    CHECK(mentions(load_failure(with_postings(0, 0x20000000ULL * 2 + 1)), "does not fit"));
    CHECK(mentions(load_failure(with_postings(0, (static_cast<std::uint64_t>(1) << 40) | 2)), "term frequency"));
}

TEST(crafted_inconsistent_references_are_rejected) {
    ByteWriter bad_option;
    bad_option.varint(1);
    bad_option.varint(1);
    bad_option.varint(0);
    bad_option.varint(1);
    bad_option.string("f");
    bad_option.string("v");
    bad_option.u8(0x80);
    CHECK(mentions(load_failure(framed(bad_option.bytes())), "option"));

    ByteWriter key_for_missing_document;
    one_document_store(key_for_missing_document);
    key_for_missing_document.varint(1);
    key_for_missing_document.varint(3);
    key_for_missing_document.string("k");
    CHECK(mentions(load_failure(framed(key_for_missing_document.bytes())), "missing document"));

    ByteWriter duplicate_key;
    duplicate_key.varint(2);
    duplicate_key.varint(2);
    duplicate_key.varint(0);
    duplicate_key.varint(0);
    duplicate_key.varint(1);
    duplicate_key.varint(0);
    duplicate_key.varint(2);
    duplicate_key.varint(0);
    duplicate_key.string("k");
    duplicate_key.varint(1);
    duplicate_key.string("k");
    CHECK(mentions(load_failure(framed(duplicate_key.bytes())), "duplicate key"));

    ByteWriter descending_documents;
    descending_documents.varint(2);
    descending_documents.varint(2);
    descending_documents.varint(1);
    descending_documents.varint(0);
    descending_documents.varint(0);
    descending_documents.varint(0);
    CHECK(mentions(load_failure(framed(descending_documents.bytes())), "not ascending"));

    ByteWriter unsorted_terms;
    one_document_store(unsorted_terms);
    no_keys(unsorted_terms);
    unsorted_terms.varint(1);
    unsorted_terms.string("t");
    unsorted_terms.varint(2);
    for (const char* term : {"b", "a"}) {
        unsorted_terms.string(term);
        unsorted_terms.varint(1);
        unsorted_terms.varint(0);
        unsorted_terms.varint(2);
    }
    CHECK(mentions(load_failure(framed(unsorted_terms.bytes())), "ascending order"));

    ByteWriter repeated_posting;
    one_document_store(repeated_posting);
    no_keys(repeated_posting);
    repeated_posting.varint(1);
    repeated_posting.string("t");
    repeated_posting.varint(1);
    repeated_posting.string("a");
    repeated_posting.varint(2);
    for (int i = 0; i < 2; i++) {
        repeated_posting.varint(0);
        repeated_posting.varint(2);
    }
    CHECK(mentions(load_failure(framed(repeated_posting.bytes())), "not ascending"));

    ByteWriter duplicate_field;
    one_document_store(duplicate_field);
    no_keys(duplicate_field);
    duplicate_field.varint(2);
    for (int i = 0; i < 2; i++) {
        duplicate_field.string("t");
        duplicate_field.varint(1);
        duplicate_field.string("a");
        duplicate_field.varint(1);
        duplicate_field.varint(0);
        duplicate_field.varint(2);
    }
    CHECK(mentions(load_failure(framed(duplicate_field.bytes())), "duplicate field"));
}

TEST(a_sparse_store_with_many_removed_ids_loads) {
    Indexer indexer;
    for (int i = 0; i < 200; i++) {
        indexer.add_document(make_text_document("t", "b"));
    }
    for (int i = 1; i < 200; i++) {
        indexer.remove_document(i);
    }
    const Indexer loaded = Indexer::deserialize(indexer.serialize());

    CHECK_EQ(loaded.get_index().get_total_documents(), 1);
    CHECK_EQ(loaded.get_index().get_document_frequency("t"), static_cast<std::size_t>(1));
}

TEST(an_index_loaded_from_a_file_can_be_changed) {
    const TempFile file("mutate");
    api_corpus().save(file.str());
    luxcore::Index loaded = luxcore::Index::load(file.str());
    luxcore::Index fresh = api_corpus();

    for (luxcore::Index* index : {&loaded, &fresh}) {
        index->add("new", "Brand new", "freshly added rocket");
        index->add("rocket", "Rocket", "now about gardens only");
        CHECK(index->remove("cjk"));
        CHECK(!index->remove("gone-1"));
        index->add("gone-1", "Back again", "returned note");
    }

    CHECK_EQ(loaded.size(), fresh.size());
    CHECK_EQ(api_results(loaded), api_results(fresh));
    CHECK_EQ(api_stored(loaded), api_stored(fresh));
    CHECK(!loaded.search("rocket").empty());
}

TEST(loading_a_missing_file_throws) {
    const TempFile file("missing");

    const std::string failure = failure_of([&] { luxcore::Index::load(file.str()); });

    CHECK(rejected(failure));
    CHECK(mentions(failure, file.str().c_str()));
}

TEST(loading_an_empty_or_garbage_file_throws) {
    const TempFile file("garbage");

    write_all(file.str(), "");
    CHECK(rejected(failure_of([&] { luxcore::Index::load(file.str()); })));

    write_all(file.str(), "hello, this is not an index");
    CHECK(mentions(failure_of([&] { luxcore::Index::load(file.str()); }), "magic"));

    api_corpus().save(file.str());
    const std::string bytes = read_all(file.str());
    write_all(file.str(), bytes.substr(0, bytes.size() / 2));
    CHECK(rejected(failure_of([&] { luxcore::Index::load(file.str()); })));
}

TEST(saving_to_an_unwritable_path_throws) {
    const TempFile file("no_directory");
    const std::string path = (file.path.parent_path() / "luxcore_no_such_directory" / "index.lux").string();

    CHECK(rejected(failure_of([&] { api_corpus().save(path); })));
    CHECK(rejected(failure_of([&] { api_corpus().save(std::filesystem::temp_directory_path().string()); })));
}
