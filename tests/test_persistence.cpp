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

TEST(byte_writer_emits_fixed_width_fields_little_endian) {
    ByteWriter writer;
    writer.u32(0x01020304);
    writer.u64(0x1112131415161718ULL);

    CHECK_EQ(writer.bytes(), std::string("\x04\x03\x02\x01\x18\x17\x16\x15\x14\x13\x12\x11", 12));
}

TEST(byte_writer_encodes_varints_in_seven_bit_groups) {
    const std::vector<std::pair<std::uint64_t, std::size_t>> cases = {
        {0, 1}, {127, 1}, {128, 2}, {16383, 2}, {16384, 3}, {0xFFFFFFFFULL, 5}, {UINT64_MAX, 10}};
    for (const auto& [value, size] : cases) {
        ByteWriter writer;
        writer.varint(value);
        CHECK_EQ(writer.size(), size);

        ByteReader reader(writer.bytes());
        CHECK_EQ(reader.varint(), value);
        CHECK(reader.at_end());
    }

    ByteWriter writer;
    writer.varint(300);
    CHECK_EQ(writer.bytes(), std::string("\xac\x02"));
}

TEST(byte_reader_round_trips_every_field_type) {
    ByteWriter writer;
    writer.u8(7);
    writer.u32(0xDEADBEEF);
    writer.u64(0x0123456789ABCDEFULL);
    writer.string("caf\xc3\xa9");
    writer.string("");

    ByteReader reader(writer.bytes());
    CHECK_EQ(static_cast<int>(reader.u8()), 7);
    CHECK_EQ(reader.u32(), static_cast<std::uint32_t>(0xDEADBEEF));
    CHECK_EQ(reader.u64(), static_cast<std::uint64_t>(0x0123456789ABCDEFULL));
    CHECK_EQ(std::string(reader.string("text")), "caf\xc3\xa9");
    CHECK_EQ(std::string(reader.string("text")), "");
    CHECK(reader.at_end());
}

TEST(byte_reader_rejects_reads_past_the_end) {
    const std::string two_bytes("\x01\x02");
    CHECK(rejected(failure_of([&] { ByteReader(two_bytes).u32(); })));
    CHECK(rejected(failure_of([&] { ByteReader(two_bytes).u64(); })));
    CHECK(rejected(failure_of([&] { ByteReader("").u8(); })));
    CHECK(rejected(failure_of([&] { ByteReader(std::string("\x80\x80")).varint(); })));
}

TEST(byte_reader_rejects_malformed_varints) {
    CHECK(rejected(failure_of([] { ByteReader(std::string(11, '\x80')).varint(); })));
    CHECK(rejected(failure_of([] { ByteReader(std::string("\xff\xff\xff\xff\xff\xff\xff\xff\xff\x7f")).varint(); })));
}

TEST(byte_reader_checks_lengths_and_counts_against_the_remaining_bytes) {
    ByteWriter writer;
    writer.varint(5);
    writer.append("abc");
    CHECK(rejected(failure_of([&] { ByteReader(writer.bytes()).string("name"); })));

    ByteWriter counts;
    counts.varint(3);
    counts.append("abcd");
    CHECK(rejected(failure_of([&] { ByteReader(counts.bytes()).count("items", 2); })));
    CHECK_EQ(ByteReader(counts.bytes()).count("items", 1), static_cast<std::size_t>(3));

    ByteWriter huge;
    huge.varint(UINT64_MAX);
    CHECK(rejected(failure_of([&] { ByteReader(huge.bytes()).string("name"); })));
    CHECK(rejected(failure_of([&] { ByteReader(huge.bytes()).count("items", 1); })));
}

TEST(a_saved_index_starts_with_the_magic_and_version_in_little_endian) {
    const std::string bytes = Indexer().serialize();

    CHECK_EQ(bytes.substr(0, 4), std::string("LUXC"));
    CHECK_EQ(bytes.substr(4, 4), std::string("\x01\x00\x00\x00", 4));
    CHECK_EQ(bytes.substr(8, 8), std::string("\x04\x00\x00\x00\x00\x00\x00\x00", 8));
    CHECK_EQ(bytes.size(), static_cast<std::size_t>(20));
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

TEST(round_trip_keeps_ids_keys_and_the_gaps_left_by_removed_documents) {
    const Indexer original = varied_corpus();
    const Indexer loaded = Indexer::deserialize(original.serialize(), "stopwords.txt");

    for (const char* key : {"rocket", "many", "accents", "cjk", "ids", "stopwords-only", "swap", "late"}) {
        CHECK(loaded.find_id(key).has_value());
        CHECK_EQ(*loaded.find_id(key), *original.find_id(key));
        CHECK_EQ(loaded.get_key(*loaded.find_id(key)), std::string(key));
    }
    for (const char* key : {"gone-1", "gone-2", "missing"}) {
        CHECK(!loaded.find_id(key).has_value());
    }
    CHECK(!loaded.contains(7));
    CHECK_EQ(loaded.get_key(5), "");
    CHECK(loaded.contains(5));
    CHECK_EQ(loaded.get_index().get_total_documents(), original.get_index().get_total_documents());
}

TEST(round_trip_serializes_to_identical_bytes_again) {
    const Indexer original = varied_corpus();
    const std::string bytes = original.serialize();
    const Indexer loaded = Indexer::deserialize(bytes, "stopwords.txt");

    CHECK(loaded.serialize() == bytes);
    CHECK(original.serialize() == bytes);
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

TEST(a_loaded_index_can_be_changed_and_continues_internal_ids) {
    Indexer original = varied_corpus();
    Indexer loaded = Indexer::deserialize(varied_corpus().serialize(), "stopwords.txt");
    const int last_id = *loaded.find_id("late");

    const std::string original_ids = mutate(original);
    const std::string loaded_ids = mutate(loaded);

    CHECK_EQ(loaded_ids, original_ids);
    CHECK_EQ(loaded_ids.substr(0, loaded_ids.find(',')), std::to_string(last_id + 1));
    CHECK_EQ(describe(loaded, corpus_ids + 8), describe(original, corpus_ids + 8));
    CHECK_EQ(results_of(loaded), results_of(original));
    CHECK(loaded.serialize() == original.serialize());
    CHECK(mentions(results_of(loaded), ",again,"));
}

TEST(a_loaded_index_survives_a_second_save_and_load_after_changes) {
    Indexer loaded = Indexer::deserialize(varied_corpus().serialize(), "stopwords.txt");
    mutate(loaded);
    const Indexer again = Indexer::deserialize(loaded.serialize(), "stopwords.txt");

    CHECK_EQ(describe(again, corpus_ids + 8), describe(loaded, corpus_ids + 8));
    CHECK_EQ(results_of(again), results_of(loaded));
}

TEST(an_empty_index_round_trips_and_stays_usable) {
    const Indexer empty;
    const std::string bytes = empty.serialize();
    Indexer loaded = Indexer::deserialize(bytes);

    CHECK_EQ(loaded.get_index().get_total_documents(), 0);
    CHECK_EQ(loaded.get_index().get_unique_terms_count(), static_cast<std::size_t>(0));
    CHECK(loaded.serialize() == bytes);
    CHECK(Searcher(loaded.get_index(), loaded.get_analyzer()).search("anything").empty());
    CHECK_EQ(loaded.add_document("first", make_text_document("Hello", "world")), 0);
    CHECK_EQ(loaded.get_key(0), "first");
}

TEST(an_index_whose_documents_were_all_removed_keeps_counting_ids_after_load) {
    Indexer indexer;
    indexer.add_document("a", make_text_document("one", "two"));
    indexer.add_document("b", make_text_document("three", "four"));
    indexer.remove_document("a");
    indexer.remove_document("b");

    Indexer loaded = Indexer::deserialize(indexer.serialize());

    CHECK_EQ(loaded.get_index().get_total_documents(), 0);
    CHECK(!loaded.find_id("a").has_value());
    CHECK_EQ(loaded.add_document("c", make_text_document("five", "six")), 2);
}

TEST(saving_does_not_change_the_index) {
    const Indexer indexer = varied_corpus();
    const std::string before = describe(indexer, corpus_ids) + results_of(indexer);

    const std::string first = indexer.serialize();
    const std::string second = indexer.serialize();

    CHECK(first == second);
    CHECK_EQ(describe(indexer, corpus_ids) + results_of(indexer), before);
}

TEST(a_loaded_index_uses_the_analyzer_it_is_given) {
    Indexer plain;
    plain.add_document("a", make_text_document("Plans", "to be or not to be"));
    Indexer loaded = Indexer::deserialize(plain.serialize(), "stopwords.txt");

    CHECK(loaded.get_index().get_document_frequency("to") > 0);
    CHECK(Searcher(loaded.get_index(), loaded.get_analyzer()).search("to be ").empty());
    CHECK(!Searcher(plain.get_index(), plain.get_analyzer()).search("to be ").empty());
}

TEST(load_rejects_a_wrong_magic_number) {
    std::string bytes = varied_corpus().serialize();
    bytes[0] = 'X';

    CHECK(mentions(load_failure(bytes), "magic"));
    CHECK(mentions(load_failure("this is a plain text file, not an index"), "magic"));
}

TEST(load_rejects_an_unsupported_version) {
    for (const char version : {'\x00', '\x02', '\x7f'}) {
        std::string bytes = varied_corpus().serialize();
        bytes[4] = version;
        const std::string failure = load_failure(bytes);

        CHECK(mentions(failure, "version"));
        CHECK(mentions(failure, std::to_string(static_cast<int>(version)).c_str()));
    }

    std::string far_future = varied_corpus().serialize();
    far_future[7] = '\x01';
    CHECK(mentions(load_failure(far_future), "version"));
}

TEST(load_rejects_a_file_too_short_for_the_header) {
    const std::string bytes = Indexer().serialize();

    CHECK(rejected(load_failure("")));
    CHECK(rejected(load_failure("L")));
    CHECK(rejected(load_failure(bytes.substr(0, 7))));
    CHECK(rejected(load_failure(bytes.substr(0, 12))));
}

TEST(load_rejects_trailing_bytes_and_a_wrong_declared_length) {
    const std::string bytes = small_corpus().serialize();

    CHECK(mentions(load_failure(bytes + "x"), "truncated or has trailing"));
    CHECK(mentions(load_failure(with_body_length(bytes, bytes.size() - 15)), "truncated or has trailing"));
    CHECK(mentions(load_failure(with_body_length(bytes, UINT64_MAX)), "truncated or has trailing"));

    const std::string padded = bytes + "x";
    CHECK(mentions(load_failure(with_body_length(padded, padded.size() - 16)), "unexpected bytes"));
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

TEST(crafted_counts_that_exceed_the_data_never_allocate) {
    ByteWriter huge_ids;
    huge_ids.varint(1000000000);
    huge_ids.varint(0);
    no_keys(huge_ids);
    huge_ids.varint(0);
    CHECK(mentions(load_failure(framed(huge_ids.bytes())), "implausible"));

    ByteWriter beyond_int;
    beyond_int.varint(static_cast<std::uint64_t>(1) << 40);
    beyond_int.varint(0);
    CHECK(mentions(load_failure(framed(beyond_int.bytes())), "out of range"));

    ByteWriter huge_documents;
    huge_documents.varint(2000000000);
    huge_documents.varint(2000000000);
    CHECK(mentions(load_failure(framed(huge_documents.bytes())), "does not fit"));

    ByteWriter huge_string;
    huge_string.varint(1);
    huge_string.varint(1);
    huge_string.varint(0);
    huge_string.varint(1);
    huge_string.varint(UINT64_MAX >> 1);
    CHECK(rejected(load_failure(framed(huge_string.bytes()))));

    ByteWriter huge_postings;
    one_document_store(huge_postings);
    no_keys(huge_postings);
    huge_postings.varint(1);
    huge_postings.string("t");
    huge_postings.varint(1);
    huge_postings.string("a");
    huge_postings.varint(4000000000ULL);
    CHECK(mentions(load_failure(framed(huge_postings.bytes())), "posting count"));

    ByteWriter huge_terms;
    one_document_store(huge_terms);
    no_keys(huge_terms);
    huge_terms.varint(1);
    huge_terms.string("t");
    huge_terms.varint(4000000000ULL);
    CHECK(mentions(load_failure(framed(huge_terms.bytes())), "term count"));

    ByteWriter huge_fields;
    one_document_store(huge_fields);
    no_keys(huge_fields);
    huge_fields.varint(4000000000ULL);
    CHECK(mentions(load_failure(framed(huge_fields.bytes())), "field count"));

    ByteWriter huge_positions;
    one_document_store(huge_positions);
    no_keys(huge_positions);
    one_posting_list(huge_positions, 0, (static_cast<std::uint64_t>(1000000) << 1) | 1);
    CHECK(mentions(load_failure(framed(huge_positions.bytes())), "does not fit"));
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

TEST(doc_ids_in_postings_are_stored_as_gaps_of_about_one_byte) {
    constexpr std::size_t documents = 20000;
    Indexer indexer;
    for (std::size_t i = 0; i < documents; i++) {
        Document document;
        document.add({"t", "alpha", FieldOptions{false, true, false}});
        indexer.add_document(document);
    }

    const std::size_t size = indexer.serialize().size();

    CHECK(size >= 5 * documents);
    CHECK(size < 5 * documents + 100);
}

TEST(positions_are_stored_as_gaps_of_about_one_byte) {
    std::string text;
    for (int i = 0; i < 1000; i++) {
        text += "zeta ";
    }
    Indexer indexer;
    Document document;
    document.add({"t", text, FieldOptions{false, true, true}});
    indexer.add_document(document);

    const std::size_t size = indexer.serialize().size();

    CHECK(size >= 1000);
    CHECK(size < 1100);
}

TEST(index_round_trips_through_a_file_with_identical_results) {
    const TempFile file("round_trip");
    const luxcore::Index original = api_corpus();
    original.save(file.str());

    const luxcore::Index loaded = luxcore::Index::load(file.str());

    CHECK_EQ(loaded.size(), original.size());
    CHECK_EQ(api_results(loaded), api_results(original));
    CHECK_EQ(api_stored(loaded), api_stored(original));
    CHECK(mentions(api_results(loaded), "rocket => rocket@"));
    CHECK_EQ(loaded.stored("cjk", "title").value_or("-"), "\xe4\xb8\x9c\xe4\xba\xac\xe5\xa4\xa7\xe5\xad\xa6");
    CHECK(!loaded.stored("gone-1", "title").has_value());
    CHECK(!loaded.search("rocket").empty());
    CHECK(loaded.search("rocket")[0].score == original.search("rocket")[0].score);
}

TEST(a_file_holds_the_header_and_nothing_else_than_the_index) {
    const TempFile file("file_bytes");
    api_corpus().save(file.str());

    const std::string bytes = read_all(file.str());

    CHECK_EQ(bytes.substr(0, 4), "LUXC");
    CHECK_EQ(std::filesystem::file_size(file.path), static_cast<std::uintmax_t>(bytes.size()));
    CHECK(Indexer::deserialize(bytes).get_index().get_total_documents() == 6);
}

TEST(saving_twice_gives_the_same_file_and_leaves_the_index_unchanged) {
    const TempFile first("twice_1");
    const TempFile second("twice_2");
    const luxcore::Index index = api_corpus();
    const std::string before = api_results(index);

    index.save(first.str());
    index.save(second.str());

    CHECK(read_all(first.str()) == read_all(second.str()));
    CHECK_EQ(api_results(index), before);
    CHECK_EQ(index.size(), static_cast<std::size_t>(6));
}

TEST(saving_replaces_an_existing_file_completely) {
    const TempFile file("replace");
    api_corpus().save(file.str());
    luxcore::Index small;
    small.add("only", "One", "document");
    small.save(file.str());

    const luxcore::Index loaded = luxcore::Index::load(file.str());

    CHECK_EQ(loaded.size(), static_cast<std::size_t>(1));
    CHECK(loaded.search("rocket").empty());
    CHECK_EQ(loaded.search("document")[0].key, "only");
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

TEST(an_empty_index_round_trips_through_a_file) {
    const TempFile file("empty");
    luxcore::Index().save(file.str());

    luxcore::Index loaded = luxcore::Index::load(file.str());

    CHECK_EQ(loaded.size(), static_cast<std::size_t>(0));
    CHECK(loaded.search("anything").empty());
    loaded.add("a", "Title", "body");
    CHECK_EQ(loaded.size(), static_cast<std::size_t>(1));
}

TEST(loading_a_missing_file_throws) {
    const TempFile file("missing");

    const std::string failure = failure_of([&] { luxcore::Index::load(file.str()); });

    CHECK(rejected(failure));
    CHECK(mentions(failure, file.str().c_str()));
}

TEST(loading_a_directory_throws) {
    const std::string directory = std::filesystem::temp_directory_path().string();

    CHECK(rejected(failure_of([&] { luxcore::Index::load(directory); })));
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
