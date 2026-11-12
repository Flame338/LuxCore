#include "TestFramework.h"

#include "ByteIO.h"
#include "Indexer.h"
#include "LuxCore.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class RecordingSink : public ByteSink {
public:
    std::string bytes;
    std::vector<std::size_t> chunks;
    int overwrites = 0;

    void write(std::string_view more) override {
        bytes.append(more);
        chunks.push_back(more.size());
    }

    void overwrite(std::uint64_t offset, std::string_view replacement) override {
        bytes.replace(static_cast<std::size_t>(offset), replacement.size(), replacement);
        overwrites++;
    }
};

class FailingSink : public ByteSink {
public:
    std::size_t budget;

    explicit FailingSink(std::size_t budget) : budget(budget) {}

    void write(std::string_view more) override {
        if (more.size() > budget) {
            throw std::runtime_error("disk full");
        }
        budget -= more.size();
    }

    void overwrite(std::uint64_t, std::string_view) override {}
};

Indexer big_indexer() {
    Indexer indexer("", IndexOptions{.positions = true});
    for (int i = 0; i < 3000; i++) {
        std::string body;
        for (int w = 0; w < 120; w++) {
            body += "word" + std::to_string((i * 31 + w * 7) % 5000) + " ";
        }
        indexer.add_document("doc-" + std::to_string(i), make_text_document("title " + std::to_string(i), body));
    }
    return indexer;
}

}

TEST(streamed_serialize_writes_bounded_chunks_that_load_back) {
    const Indexer indexer = big_indexer();
    RecordingSink sink;
    indexer.serialize(sink);

    CHECK(sink.bytes.size() > (4u << 20));
    CHECK(sink.chunks.size() > 3);
    CHECK(*std::ranges::max_element(sink.chunks) <= (1u << 20) + 64);
    CHECK_EQ(sink.overwrites, 1);
    CHECK(sink.bytes == indexer.serialize());

    const Indexer loaded = Indexer::deserialize(sink.bytes, "", IndexOptions{.positions = true});
    CHECK(loaded.serialize() == sink.bytes);
}

TEST(a_sink_failure_stops_serialize_with_an_error) {
    const Indexer indexer = big_indexer();
    bool failed = false;
    try {
        FailingSink sink(3u << 20);
        indexer.serialize(sink);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    CHECK(failed);
}

TEST(a_saved_index_larger_than_the_write_buffer_loads_back_identically) {
    luxcore::Index index;
    for (int i = 0; i < 4000; i++) {
        std::string body;
        for (int w = 0; w < 80; w++) {
            body += "term" + std::to_string((i * 13 + w * 3) % 7000) + " ";
        }
        index.add("k" + std::to_string(i), "title " + std::to_string(i), body);
    }
    const std::filesystem::path first = std::filesystem::temp_directory_path() / "luxcore_streaming_first.lux";
    const std::filesystem::path second = std::filesystem::temp_directory_path() / "luxcore_streaming_second.lux";
    index.save(first.string());
    const luxcore::Index loaded = luxcore::Index::load(first.string());
    loaded.save(second.string());

    const auto bytes_of = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::string saved = bytes_of(first);
    CHECK(saved.size() > (2u << 20));
    CHECK(saved == bytes_of(second));
    CHECK_EQ(loaded.size(), index.size());
    CHECK_EQ(loaded.search("term42", 5).size(), index.search("term42", 5).size());
    std::error_code ignored;
    std::filesystem::remove(first, ignored);
    std::filesystem::remove(second, ignored);
}
