#include "TestFramework.h"

#include "Indexer.h"
#include "LuxCore.h"
#include "Scoring.h"
#include "Searcher.h"

#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int reader_threads = 8;
constexpr int anchor_count = 40;
constexpr int churn_total = 3000;
constexpr int stress_readers = 4;
constexpr long long reader_iteration_cap = 200000;

const std::string plain_alphabet = "abcdefghijklmnopqrstuvwxyz";
const std::string churn_alphabet = "abcdefghijklmnoprstuvwy";

template <typename Body>
int run_threads(int count, Body body) {
    std::atomic<bool> go{false};
    std::atomic<int> exceptions{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < count; t++) {
        threads.emplace_back([&, t] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            try {
                body(t);
            } catch (...) {
                exceptions.fetch_add(1);
            }
        });
    }
    go.store(true);
    for (std::thread& thread : threads) {
        thread.join();
    }
    return exceptions.load();
}

std::string random_word(std::mt19937& rng, const std::string& alphabet, int shortest, int longest) {
    const int length = shortest + static_cast<int>(rng() % static_cast<unsigned>(longest - shortest + 1));
    std::string word;
    for (int i = 0; i < length; i++) {
        word += alphabet[rng() % alphabet.size()];
    }
    return word;
}

std::vector<std::string> random_vocabulary(std::mt19937& rng, const std::string& alphabet, int size) {
    std::set<std::string> unique;
    while (static_cast<int>(unique.size()) < size) {
        unique.insert(random_word(rng, alphabet, 4, 9));
    }
    return {unique.begin(), unique.end()};
}

std::string random_text(std::mt19937& rng, const std::vector<std::string>& vocabulary, int count) {
    std::string text;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            text += ' ';
        }
        text += vocabulary[rng() % vocabulary.size()];
    }
    return text;
}

std::string with_typo(std::string word) {
    if (word.size() > 3) {
        word[2] = word[2] == 'e' ? 'a' : 'e';
    }
    return word;
}

struct Corpus {
    std::vector<std::string> keys;
    std::vector<std::string> titles;
    std::vector<std::string> bodies;
    std::vector<std::string> queries;
};

Corpus make_corpus(int docs) {
    std::mt19937 rng(7);
    const std::vector<std::string> vocabulary = random_vocabulary(rng, plain_alphabet, 150);
    Corpus corpus;
    for (int i = 0; i < docs; i++) {
        corpus.keys.push_back("doc-" + std::to_string(i));
        corpus.titles.push_back(random_text(rng, vocabulary, 2 + static_cast<int>(rng() % 2)));
        corpus.bodies.push_back(random_text(rng, vocabulary, 15 + static_cast<int>(rng() % 10)));
    }
    for (int i = 0; i < 40; i++) {
        const std::string& word = vocabulary[rng() % vocabulary.size()];
        const std::string& other = vocabulary[rng() % vocabulary.size()];
        corpus.queries.push_back(word);
        corpus.queries.push_back(word.substr(0, 2));
        corpus.queries.push_back(word.substr(0, 3));
        corpus.queries.push_back(with_typo(word) + " " + other.substr(0, 2));
        corpus.queries.push_back(word + " " + other);
    }
    return corpus;
}

bool same_hits(const std::vector<luxcore::Hit>& left, const std::vector<luxcore::Hit>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); i++) {
        if (left[i].key != right[i].key || left[i].score != right[i].score) {
            return false;
        }
    }
    return true;
}

bool same_hits(const std::vector<SearchHit>& left, const std::vector<SearchHit>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); i++) {
        if (left[i].doc_id != right[i].doc_id || left[i].key != right[i].key || left[i].score != right[i].score) {
            return false;
        }
    }
    return true;
}

bool same_suggestions(const std::vector<Suggestion>& left, const std::vector<Suggestion>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); i++) {
        if (left[i].term != right[i].term || left[i].doc_freq != right[i].doc_freq) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> keys_of(const std::vector<luxcore::Hit>& hits) {
    std::vector<std::string> keys;
    for (const luxcore::Hit& hit : hits) {
        keys.push_back(hit.key);
    }
    return keys;
}

std::string anchor_word(int index) {
    std::string word = "xqz";
    word += static_cast<char>('a' + index / 676);
    word += static_cast<char>('a' + index / 26 % 26);
    word += static_cast<char>('a' + index % 26);
    return word + "mnop";
}

std::string anchor_key(int index) {
    return "anchor-" + std::to_string(index);
}

std::string anchor_body(int index) {
    return anchor_word(index) + " stays here with a few ordinary words";
}

std::optional<int> number_after(const std::string& key, const std::string& prefix) {
    if (key.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    int value = 0;
    const char* first = key.data() + prefix.size();
    const char* last = key.data() + key.size();
    const auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc() || end != last) {
        return std::nullopt;
    }
    return value;
}

bool known_key(const std::string& key) {
    if (const std::optional<int> anchor = number_after(key, "anchor-")) {
        return *anchor >= 0 && *anchor < anchor_count;
    }
    if (const std::optional<int> churn = number_after(key, "churn-")) {
        return *churn >= 0 && *churn < churn_total;
    }
    return false;
}

struct Churn {
    std::vector<std::string> vocabulary;
    std::vector<std::string> titles;
    std::vector<std::string> first_bodies;
    std::vector<std::string> second_bodies;
};

Churn make_churn() {
    std::mt19937 rng(11);
    Churn churn;
    churn.vocabulary = random_vocabulary(rng, churn_alphabet, 300);
    for (int i = 0; i < churn_total; i++) {
        churn.titles.push_back(random_text(rng, churn.vocabulary, 3));
        churn.first_bodies.push_back(random_text(rng, churn.vocabulary, 25));
        churn.second_bodies.push_back(random_text(rng, churn.vocabulary, 25));
    }
    return churn;
}

std::vector<std::string> anchor_prefixes(int index) {
    const std::string word = anchor_word(index);
    return {word.substr(0, 5), word.substr(0, 6), word};
}

}

TEST(threads_concurrent_reads_on_an_unchanged_index_match_single_thread) {
    const Corpus corpus = make_corpus(400);
    luxcore::Index index;
    for (std::size_t i = 0; i < corpus.keys.size(); i++) {
        index.add(corpus.keys[i], corpus.titles[i], corpus.bodies[i]);
    }

    std::vector<std::vector<luxcore::Hit>> expected_hits;
    std::vector<std::vector<std::string>> expected_suggestions;
    for (const std::string& query : corpus.queries) {
        expected_hits.push_back(index.search(query, 8));
        expected_suggestions.push_back(index.suggest(query, 5));
    }

    std::atomic<int> wrong_hits{0};
    std::atomic<int> wrong_suggestions{0};
    std::atomic<int> wrong_stored{0};
    const int exceptions = run_threads(reader_threads, [&](int thread) {
        for (int round = 0; round < 3; round++) {
            for (std::size_t q = 0; q < corpus.queries.size(); q++) {
                const std::size_t at = (q + static_cast<std::size_t>(thread) * 17) % corpus.queries.size();
                if (!same_hits(index.search(corpus.queries[at], 8), expected_hits[at])) {
                    wrong_hits.fetch_add(1);
                }
                if (index.suggest(corpus.queries[at], 5) != expected_suggestions[at]) {
                    wrong_suggestions.fetch_add(1);
                }
                const std::size_t doc = at % corpus.keys.size();
                if (index.stored(corpus.keys[doc], "title") != corpus.titles[doc] || index.size() != corpus.keys.size()) {
                    wrong_stored.fetch_add(1);
                }
            }
        }
    });

    CHECK_EQ(exceptions, 0);
    CHECK_EQ(wrong_hits.load(), 0);
    CHECK_EQ(wrong_suggestions.load(), 0);
    CHECK_EQ(wrong_stored.load(), 0);
}

TEST(threads_concurrent_reads_on_an_unchanged_searcher_match_single_thread) {
    const Corpus corpus = make_corpus(400);
    Indexer indexer;
    for (std::size_t i = 0; i < corpus.keys.size(); i++) {
        indexer.add_document(corpus.keys[i], make_text_document(corpus.titles[i], corpus.bodies[i]));
    }
    const Searcher searcher(indexer, {.scoring = Scoring::bm25_with(), .fuzziness = Fuzziness::On});

    std::vector<std::vector<SearchHit>> expected_hits;
    std::vector<std::vector<Suggestion>> expected_suggestions;
    for (const std::string& query : corpus.queries) {
        expected_hits.push_back(searcher.search(query, 8));
        expected_suggestions.push_back(searcher.suggest(query, 5));
    }

    std::atomic<int> wrong_hits{0};
    std::atomic<int> wrong_suggestions{0};
    const int exceptions = run_threads(reader_threads, [&](int thread) {
        const Searcher own(indexer, {.scoring = Scoring::bm25_with(), .fuzziness = Fuzziness::On});
        for (int round = 0; round < 3; round++) {
            for (std::size_t q = 0; q < corpus.queries.size(); q++) {
                const std::size_t at = (q + static_cast<std::size_t>(thread) * 17) % corpus.queries.size();
                const Searcher& used = thread % 2 == 0 ? searcher : own;
                if (!same_hits(used.search(corpus.queries[at], 8), expected_hits[at])) {
                    wrong_hits.fetch_add(1);
                }
                if (!same_suggestions(used.suggest(corpus.queries[at], 5), expected_suggestions[at])) {
                    wrong_suggestions.fetch_add(1);
                }
            }
        }
    });

    CHECK_EQ(exceptions, 0);
    CHECK_EQ(wrong_hits.load(), 0);
    CHECK_EQ(wrong_suggestions.load(), 0);
}

TEST(threads_searches_stay_correct_while_a_writer_adds_replaces_and_removes) {
    const Churn churn = make_churn();
    luxcore::Index index;
    for (int a = 0; a < anchor_count; a++) {
        index.add(anchor_key(a), anchor_word(a), anchor_body(a));
    }

    std::vector<std::vector<std::string>> expected_suggestions;
    for (int a = 0; a < anchor_count; a++) {
        for (const std::string& prefix : anchor_prefixes(a)) {
            expected_suggestions.push_back(index.suggest(prefix, 5));
        }
        const std::vector<std::string> keys = keys_of(index.search(anchor_word(a), 10));
        CHECK_EQ(keys, std::vector<std::string>{anchor_key(a)});
    }

    std::set<std::string> live;
    std::atomic<bool> writer_done{false};
    std::atomic<int> writer_errors{0};
    std::atomic<int> missing_anchor{0};
    std::atomic<int> wrong_suggestions{0};
    std::atomic<int> unknown_keys{0};
    std::atomic<int> bad_scores{0};
    std::atomic<int> bad_stored{0};
    std::atomic<int> bad_sizes{0};
    std::atomic<long long> reader_iterations{0};

    const int exceptions = run_threads(1 + stress_readers, [&](int thread) {
        if (thread == 0) {
            try {
                for (int i = 0; i < churn_total; i++) {
                    const std::string key = "churn-" + std::to_string(i);
                    index.add(key, churn.titles[i], churn.first_bodies[i]);
                    live.insert(key);
                    if (i % 5 == 4) {
                        const int again = i / 2;
                        index.add("churn-" + std::to_string(again), churn.titles[again], churn.second_bodies[again]);
                        live.insert("churn-" + std::to_string(again));
                    }
                    if (i % 7 == 6) {
                        const std::string gone = "churn-" + std::to_string(i / 3);
                        index.remove(gone);
                        live.erase(gone);
                    }
                }
            } catch (...) {
                writer_errors.fetch_add(1);
            }
            writer_done.store(true);
            return;
        }

        std::mt19937 rng(100 + static_cast<unsigned>(thread));
        for (long long iteration = 0; iteration < reader_iteration_cap && !writer_done.load(); iteration++) {
            const int a = static_cast<int>(rng() % anchor_count);
            if (keys_of(index.search(anchor_word(a), 10)) != std::vector<std::string>{anchor_key(a)}) {
                missing_anchor.fetch_add(1);
            }
            const std::vector<std::string> prefixes = anchor_prefixes(a);
            const std::size_t which = rng() % prefixes.size();
            if (index.suggest(prefixes[which], 5) != expected_suggestions[static_cast<std::size_t>(a) * 3 + which]) {
                wrong_suggestions.fetch_add(1);
            }
            if (index.stored(anchor_key(a), "body") != anchor_body(a)) {
                bad_stored.fetch_add(1);
            }

            const std::string& word = churn.vocabulary[rng() % churn.vocabulary.size()];
            const std::string query = iteration % 3 == 0 ? word.substr(0, 3) : iteration % 3 == 1 ? with_typo(word) + " " + word.substr(0, 2) : word;
            for (const luxcore::Hit& hit : index.search(query, 10)) {
                if (!known_key(hit.key)) {
                    unknown_keys.fetch_add(1);
                }
                if (!std::isfinite(hit.score) || hit.score <= 0.0) {
                    bad_scores.fetch_add(1);
                }
            }
            for (const std::string& term : index.suggest(word.substr(0, 2), 5)) {
                if (term.empty()) {
                    unknown_keys.fetch_add(1);
                }
            }

            const int c = static_cast<int>(rng() % churn_total);
            const std::optional<std::string> title = index.stored("churn-" + std::to_string(c), "title");
            if (title && *title != churn.titles[c]) {
                bad_stored.fetch_add(1);
            }
            const std::size_t size = index.size();
            if (size < anchor_count || size > anchor_count + churn_total) {
                bad_sizes.fetch_add(1);
            }
            reader_iterations.fetch_add(1);
            std::this_thread::yield();
        }
    });

    CHECK_EQ(exceptions, 0);
    CHECK_EQ(writer_errors.load(), 0);
    CHECK(reader_iterations.load() > 0);
    CHECK_EQ(missing_anchor.load(), 0);
    CHECK_EQ(wrong_suggestions.load(), 0);
    CHECK_EQ(unknown_keys.load(), 0);
    CHECK_EQ(bad_scores.load(), 0);
    CHECK_EQ(bad_stored.load(), 0);
    CHECK_EQ(bad_sizes.load(), 0);

    CHECK_EQ(index.size(), static_cast<std::size_t>(anchor_count) + live.size());
    for (int i = 0; i < churn_total; i++) {
        const std::string key = "churn-" + std::to_string(i);
        const std::optional<std::string> title = index.stored(key, "title");
        CHECK_EQ(title.has_value(), live.count(key) == 1);
    }
}

TEST(threads_several_writers_lose_no_document) {
    constexpr int writers = 4;
    constexpr int per_writer = 250;
    const Churn churn = make_churn();
    luxcore::Index index;
    std::atomic<int> wrong_hits{0};

    const int exceptions = run_threads(writers + 2, [&](int thread) {
        if (thread < writers) {
            for (int i = 0; i < per_writer; i++) {
                const int source = thread * per_writer + i;
                index.add("w" + std::to_string(thread) + "-" + std::to_string(i), churn.titles[source],
                          churn.first_bodies[source]);
            }
            return;
        }
        for (int i = 0; i < 400; i++) {
            const std::string& word = churn.vocabulary[static_cast<std::size_t>(i) % churn.vocabulary.size()];
            for (const luxcore::Hit& hit : index.search(word, 10)) {
                if (hit.key.empty() || hit.key[0] != 'w') {
                    wrong_hits.fetch_add(1);
                }
            }
        }
    });

    CHECK_EQ(exceptions, 0);
    CHECK_EQ(wrong_hits.load(), 0);
    CHECK_EQ(index.size(), static_cast<std::size_t>(writers * per_writer));
    for (int thread = 0; thread < writers; thread++) {
        for (int i = 0; i < per_writer; i++) {
            const int source = thread * per_writer + i;
            CHECK_EQ(index.stored("w" + std::to_string(thread) + "-" + std::to_string(i), "title"),
                     std::optional<std::string>(churn.titles[source]));
        }
    }
}

TEST(threads_save_while_a_writer_adds_writes_a_loadable_snapshot) {
    const Churn churn = make_churn();
    luxcore::Index index;
    for (int a = 0; a < anchor_count; a++) {
        index.add(anchor_key(a), anchor_word(a), anchor_body(a));
    }
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    std::atomic<int> bad_snapshots{0};
    std::atomic<int> save_errors{0};

    const int exceptions = run_threads(2, [&](int thread) {
        if (thread == 0) {
            for (int i = 0; i < 800; i++) {
                index.add("churn-" + std::to_string(i), churn.titles[i], churn.first_bodies[i]);
            }
            return;
        }
        for (int snapshot = 0; snapshot < 5; snapshot++) {
            const std::filesystem::path file = directory / ("luxcore_threads_" + std::to_string(snapshot) + ".lux");
            try {
                index.save(file.string());
                const luxcore::Index loaded = luxcore::Index::load(file.string());
                if (loaded.size() < static_cast<std::size_t>(anchor_count) ||
                    loaded.size() > static_cast<std::size_t>(anchor_count) + 800 ||
                    loaded.stored(anchor_key(0), "body") != anchor_body(0)) {
                    bad_snapshots.fetch_add(1);
                }
            } catch (...) {
                save_errors.fetch_add(1);
            }
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
        }
    });

    CHECK_EQ(exceptions, 0);
    CHECK_EQ(save_errors.load(), 0);
    CHECK_EQ(bad_snapshots.load(), 0);
}

TEST(threads_prepare_on_many_threads_while_one_commits_and_one_searches) {
    const Churn churn = make_churn();
    constexpr int documents = 2000;
    constexpr int preparers = 8;
    luxcore::Index serial;
    luxcore::Index index;
    for (int a = 0; a < anchor_count; a++) {
        serial.add(anchor_key(a), anchor_word(a), anchor_body(a));
        index.add(anchor_key(a), anchor_word(a), anchor_body(a));
    }
    for (int i = 0; i < documents; i++) {
        serial.add("churn-" + std::to_string(i), churn.titles[i], churn.first_bodies[i]);
    }

    std::vector<std::optional<luxcore::Prepared>> slots(documents);
    std::vector<std::atomic<bool>> ready(documents);
    std::atomic<int> next{0};
    std::atomic<bool> committing{true};
    std::atomic<int> missing_anchors{0};
    std::atomic<long long> searches{0};

    const int exceptions = run_threads(preparers + 2, [&](int thread) {
        if (thread < preparers) {
            for (int i = next.fetch_add(1); i < documents; i = next.fetch_add(1)) {
                slots[i].emplace(index.prepare(churn.titles[i], churn.first_bodies[i]));
                ready[i].store(true, std::memory_order_release);
            }
            return;
        }
        if (thread == preparers) {
            for (int i = 0; i < documents; i++) {
                while (!ready[i].load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                index.commit("churn-" + std::to_string(i), std::move(*slots[i]));
            }
            committing.store(false);
            return;
        }
        for (int round = 0; committing.load() || round < 50; round++) {
            const int a = round % anchor_count;
            const std::vector<luxcore::Hit> hits = index.search(anchor_word(a), 5);
            if (hits.empty() || hits.front().key != anchor_key(a)) {
                missing_anchors.fetch_add(1);
            }
            index.search(churn.vocabulary[round % churn.vocabulary.size()], 10);
            searches.fetch_add(1);
        }
    });

    CHECK_EQ(exceptions, 0);
    CHECK_EQ(missing_anchors.load(), 0);
    CHECK(searches.load() >= 50);
    CHECK_EQ(index.size(), serial.size());

    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path serial_file = directory / "luxcore_threads_prepare_serial.lux";
    const std::filesystem::path prepared_file = directory / "luxcore_threads_prepare_prepared.lux";
    serial.save(serial_file.string());
    index.save(prepared_file.string());
    const auto bytes_of = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    CHECK(bytes_of(serial_file) == bytes_of(prepared_file));
    std::error_code ignored;
    std::filesystem::remove(serial_file, ignored);
    std::filesystem::remove(prepared_file, ignored);
}
