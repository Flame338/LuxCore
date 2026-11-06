#include "Indexer.h"
#include "Searcher.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr int result_limit = 8;
constexpr std::size_t suggestion_limit = 5;

struct Options {
    int docs = 100000;
    int vocab = 40000;
    int queries = 150;
    int passes = 7;
    int stop = 100;
    int removes = 1000;
    int fuzzy = 0;
    std::uint64_t seed = 12345;
};

struct Weighted {
    const char* text;
    int weight;
};

struct Keystroke {
    std::string text;
    int last_word_length;
};

struct Sample {
    double total_us;
    double search_us;
    double suggest_us;
    int last_word_length;
};

using Clock = std::chrono::steady_clock;

double microseconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::micro>(to - from).count();
}

template <std::size_t N>
std::discrete_distribution<int> distribution_of(const Weighted (&items)[N]) {
    std::vector<double> weights;
    for (const Weighted& item : items) {
        weights.push_back(item.weight);
    }
    return std::discrete_distribution<int>(weights.begin(), weights.end());
}

std::vector<std::string> make_vocabulary(int size, std::mt19937_64& rng) {
    static const Weighted onsets[] = {
        {"s", 14}, {"c", 12}, {"p", 10}, {"t", 9}, {"b", 8}, {"m", 8}, {"d", 7}, {"r", 6},
        {"l", 6},  {"f", 5},  {"g", 5},  {"h", 5}, {"n", 4}, {"w", 3}, {"v", 2}, {"st", 6},
        {"tr", 4}, {"sh", 3}, {"ch", 3}, {"pl", 2}, {"br", 2}, {"gr", 2}, {"cl", 2}, {"", 3}};
    static const Weighted vowels[] = {{"a", 10}, {"e", 10}, {"i", 8}, {"o", 8}, {"u", 5},
                                      {"ai", 2}, {"ea", 2}, {"ou", 2}, {"io", 1}};
    static const Weighted codas[] = {{"", 40}, {"n", 10}, {"r", 8}, {"s", 8}, {"t", 8},
                                     {"l", 6}, {"m", 5},  {"nd", 3}, {"st", 3}, {"ng", 3}};

    auto onset = distribution_of(onsets);
    auto vowel = distribution_of(vowels);
    auto coda = distribution_of(codas);
    std::discrete_distribution<int> syllables({45.0, 40.0, 15.0});

    std::unordered_set<std::string> seen;
    std::vector<std::string> words;
    while (static_cast<int>(words.size()) < size) {
        std::string word;
        const int count = 2 + syllables(rng);
        for (int i = 0; i < count; i++) {
            word += onsets[onset(rng)].text;
            word += vowels[vowel(rng)].text;
            word += codas[coda(rng)].text;
        }
        if (seen.insert(word).second) {
            words.push_back(word);
        }
    }
    return words;
}

std::discrete_distribution<int> zipf_distribution(int size) {
    std::vector<double> weights(size);
    for (int i = 0; i < size; i++) {
        weights[i] = 1.0 / (i + 1.0);
    }
    return std::discrete_distribution<int>(weights.begin(), weights.end());
}

std::string sentence(const std::vector<std::string>& words, std::discrete_distribution<int>& zipf,
                     std::mt19937_64& rng, int count) {
    std::string text;
    for (int i = 0; i < count; i++) {
        if (i > 0) text += ' ';
        text += words[zipf(rng)];
    }
    return text;
}

void build_corpus(Indexer& indexer, const std::vector<std::string>& words,
                  std::discrete_distribution<int>& zipf, std::mt19937_64& rng, int docs) {
    for (int i = 0; i < docs; i++) {
        const std::string title = sentence(words, zipf, rng, 2 + static_cast<int>(rng() % 3));
        const std::string body = sentence(words, zipf, rng, 20 + static_cast<int>(rng() % 21));
        indexer.add_document("doc-" + std::to_string(i), make_text_document(title, body));
    }
}

std::vector<Keystroke> make_keystrokes(const std::vector<std::string>& words,
                                       std::discrete_distribution<int>& zipf, std::mt19937_64& rng,
                                       int queries, int stop) {
    std::discrete_distribution<int> word_count({50.0, 30.0, 20.0});
    std::vector<Keystroke> keystrokes;
    for (int q = 0; q < queries; q++) {
        std::string query;
        const int count = 1 + word_count(rng);
        for (int w = 0; w < count; w++) {
            std::string word;
            do {
                const int rank = zipf(rng);
                word = rank < stop ? std::string() : words[rank];
            } while (word.size() < 4);
            if (w > 0) query += ' ';
            query += word;
        }
        for (std::size_t length = 1; length <= query.size(); length++) {
            int last = 0;
            while (last < static_cast<int>(length) && query[length - 1 - last] != ' ') {
                last++;
            }
            keystrokes.push_back({query.substr(0, length), last});
        }
    }
    return keystrokes;
}

double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const std::size_t index =
        static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size()))) - 1;
    return values[std::min(index, values.size() - 1)];
}

std::vector<Sample> timed_pass(const Searcher& searcher, const std::vector<Keystroke>& keystrokes,
                               std::size_t& sink) {
    std::vector<Sample> samples;
    samples.reserve(keystrokes.size());
    for (const Keystroke& keystroke : keystrokes) {
        const Clock::time_point start = Clock::now();
        const std::vector<SearchHit> hits = searcher.search(keystroke.text, result_limit);
        const Clock::time_point middle = Clock::now();
        const std::vector<Suggestion> suggestions = searcher.suggest(keystroke.text, suggestion_limit);
        const Clock::time_point end = Clock::now();
        sink += hits.size() + suggestions.size();
        samples.push_back({microseconds(start, end), microseconds(start, middle),
                           microseconds(middle, end), keystroke.last_word_length});
    }
    return samples;
}

struct Audit {
    std::uint64_t digest = 14695981039346656037ULL;
    std::size_t errors = 0;
    std::size_t hits = 0;
    std::size_t suggestions = 0;
    std::size_t empty_results = 0;

    void mix(const std::string& text) {
        for (unsigned char c : text) {
            digest = (digest ^ c) * 1099511628211ULL;
        }
        digest = (digest ^ 0xFF) * 1099511628211ULL;
    }
};

Audit audit_pass(const Searcher& searcher, const std::vector<Keystroke>& keystrokes, int docs) {
    Audit audit;
    for (const Keystroke& keystroke : keystrokes) {
        const std::vector<SearchHit> hits = searcher.search(keystroke.text, result_limit);
        const std::vector<Suggestion> suggestions = searcher.suggest(keystroke.text, suggestion_limit);

        if (hits.empty()) audit.empty_results++;
        if (hits.size() > static_cast<std::size_t>(result_limit)) audit.errors++;
        if (suggestions.size() > suggestion_limit) audit.errors++;
        audit.hits += hits.size();
        audit.suggestions += suggestions.size();

        audit.mix(keystroke.text);
        for (std::size_t i = 0; i < hits.size(); i++) {
            const SearchHit& hit = hits[i];
            if (hit.doc_id < 0 || hit.doc_id >= docs || hit.key.empty() || !std::isfinite(hit.score) || hit.score <= 0.0) {
                audit.errors++;
            }
            if (i > 0 && (hits[i - 1].score < hit.score ||
                          (hits[i - 1].score == hit.score && hits[i - 1].doc_id > hit.doc_id))) {
                audit.errors++;
            }
            audit.mix(std::to_string(hit.doc_id) + ":" + std::to_string(std::llround(hit.score * 1e6)));
        }
        for (std::size_t i = 0; i < suggestions.size(); i++) {
            if (i > 0 && (suggestions[i - 1].doc_freq < suggestions[i].doc_freq)) audit.errors++;
            audit.mix(suggestions[i].term + "#" + std::to_string(suggestions[i].doc_freq));
        }
    }
    return audit;
}

std::vector<double> timed_removals(Indexer& indexer, int docs, int removes) {
    std::vector<double> samples;
    const int count = std::min(removes, docs);
    for (int i = 0; i < count; i++) {
        const int target = static_cast<int>(static_cast<long long>(i) * docs / count);
        const std::string key = "doc-" + std::to_string(target);
        const Clock::time_point start = Clock::now();
        indexer.remove_document(key);
        samples.push_back(microseconds(start, Clock::now()));
    }
    return samples;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        const long long value = std::atoll(argv[i + 1]);
        if (flag == "--docs") options.docs = static_cast<int>(value);
        if (flag == "--vocab") options.vocab = static_cast<int>(value);
        if (flag == "--queries") options.queries = static_cast<int>(value);
        if (flag == "--passes") options.passes = static_cast<int>(value);
        if (flag == "--stop") options.stop = static_cast<int>(value);
        if (flag == "--removes") options.removes = static_cast<int>(value);
        if (flag == "--fuzzy") options.fuzzy = static_cast<int>(value);
        if (flag == "--seed") options.seed = static_cast<std::uint64_t>(value);
    }
    return options;
}

}

int main(int argc, char** argv) {
    const Options options = parse(argc, argv);
    std::mt19937_64 rng(options.seed);

    const std::vector<std::string> words = make_vocabulary(options.vocab, rng);
    std::discrete_distribution<int> zipf = zipf_distribution(options.vocab);

    const std::filesystem::path stop_file = std::filesystem::temp_directory_path() / "luxbench_stopwords.txt";
    if (options.stop > 0) {
        std::ofstream out(stop_file);
        for (int rank = 0; rank < options.stop; rank++) {
            out << words[rank] << "\n";
        }
    }

    Indexer indexer(options.stop > 0 ? stop_file.string() : "");
    const Clock::time_point build_start = Clock::now();
    build_corpus(indexer, words, zipf, rng, options.docs);
    const double build_ms = microseconds(build_start, Clock::now()) / 1000.0;

    const std::vector<Keystroke> keystrokes = make_keystrokes(words, zipf, rng, options.queries, options.stop);
    const Searcher searcher(indexer.get_index(), indexer.get_analyzer(), FieldBoosts::standard(),
                            &indexer.get_keys(), Scoring::tf_idf(),
                            options.fuzzy ? Fuzziness::On : Fuzziness::Off);

    const Audit audit = audit_pass(searcher, keystrokes, options.docs);

    std::size_t sink = 0;
    std::vector<double> pass_p99;
    std::vector<Sample> pooled;
    for (int pass = 0; pass < options.passes; pass++) {
        const std::vector<Sample> samples = timed_pass(searcher, keystrokes, sink);
        std::vector<double> totals;
        for (const Sample& sample : samples) {
            totals.push_back(sample.total_us);
        }
        pass_p99.push_back(percentile(totals, 0.99));
        pooled.insert(pooled.end(), samples.begin(), samples.end());
    }

    std::vector<double> totals, searches, suggests;
    double sum = 0.0;
    for (const Sample& sample : pooled) {
        totals.push_back(sample.total_us);
        searches.push_back(sample.search_us);
        suggests.push_back(sample.suggest_us);
        sum += sample.total_us;
    }
    std::vector<double> sorted_p99 = pass_p99;
    std::sort(sorted_p99.begin(), sorted_p99.end());

    std::cout << std::fixed << std::setprecision(1);
    std::filesystem::remove(stop_file);

    std::cout << "corpus docs=" << options.docs << " vocab=" << options.vocab << " stop=" << options.stop
              << " index_terms=" << indexer.get_index().get_unique_terms_count()
              << " build_ms=" << build_ms << "\n";
    std::cout << "fuzzy=" << options.fuzzy << "\n";
    std::cout << "workload queries=" << options.queries << " keystrokes=" << keystrokes.size()
              << " passes=" << options.passes << " seed=" << options.seed << "\n";
    std::cout << "errors=" << audit.errors << "\n";
    std::cout << "work hits=" << audit.hits << " suggestions=" << audit.suggestions
              << " empty_results=" << audit.empty_results << " sink=" << sink << "\n";
    std::cout << "digest=" << std::hex << audit.digest << std::dec << "\n";
    std::cout << "METRIC p99_us=" << sorted_p99[sorted_p99.size() / 2] << " range_us=["
              << sorted_p99.front() << "," << sorted_p99.back() << "]\n";
    std::cout << "pooled p50_us=" << percentile(totals, 0.50) << " p95_us=" << percentile(totals, 0.95)
              << " p99_us=" << percentile(totals, 0.99) << " max_us=" << percentile(totals, 1.0)
              << " mean_us=" << sum / static_cast<double>(pooled.size()) << "\n";
    const std::vector<double> removals = timed_removals(indexer, options.docs, options.removes);
    if (!removals.empty()) {
        double removal_sum = 0.0;
        for (double sample : removals) {
            removal_sum += sample;
        }
        std::cout << "removal n=" << removals.size() << " mean_us=" << removal_sum / static_cast<double>(removals.size())
                  << " p99_us=" << percentile(removals, 0.99) << " max_us=" << percentile(removals, 1.0)
                  << " remaining=" << indexer.get_index().get_total_documents() << "\n";
    }
    std::cout << "split search_p99_us=" << percentile(searches, 0.99)
              << " suggest_p99_us=" << percentile(suggests, 0.99) << "\n";

    std::cout << "by last word length (letters typed in the last word):\n";
    const char* labels[] = {"finished", "1", "2", "3", "4+"};
    for (int bucket = 0; bucket < 5; bucket++) {
        std::vector<double> in_bucket;
        for (const Sample& sample : pooled) {
            const int key = std::min(sample.last_word_length, 4);
            if (key == bucket) in_bucket.push_back(sample.total_us);
        }
        if (in_bucket.empty()) continue;
        std::cout << "  " << std::setw(8) << labels[bucket] << "  n=" << std::setw(6) << in_bucket.size()
                  << "  p50_us=" << std::setw(9) << percentile(in_bucket, 0.50)
                  << "  p99_us=" << std::setw(9) << percentile(in_bucket, 0.99) << "\n";
    }
    return audit.errors == 0 ? 0 : 1;
}
