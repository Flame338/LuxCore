#include "TestFramework.h"

#include "Indexer.h"
#include "LuxCore.h"
#include "Searcher.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

// The bytes end exactly where an inaccessible page starts, so a read one byte past the end
// faults at once instead of quietly reading heap or zero padding.
class GuardedBytes {
private:
    char* region = nullptr;
    std::size_t region_size = 0;
    std::string_view content;

public:
    explicit GuardedBytes(std::string_view source) {
#if defined(_WIN32)
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const std::size_t page = info.dwPageSize;
#else
        const std::size_t page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
        const std::size_t data_pages = (source.size() + page - 1) / page * page;
        region_size = data_pages + page;
#if defined(_WIN32)
        region = static_cast<char*>(VirtualAlloc(nullptr, region_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD previous;
        if (region != nullptr) {
            VirtualProtect(region + data_pages, page, PAGE_NOACCESS, &previous);
        }
#else
        void* mapped = mmap(nullptr, region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        region = mapped == MAP_FAILED ? nullptr : static_cast<char*>(mapped);
        if (region != nullptr) {
            mprotect(region + data_pages, page, PROT_NONE);
        }
#endif
        if (region == nullptr) {
            throw std::runtime_error("cannot allocate the guarded buffer");
        }
        char* start = region + data_pages - source.size();
        if (!source.empty()) {
            std::memcpy(start, source.data(), source.size());
        }
        content = std::string_view(start, source.size());
    }

    ~GuardedBytes() {
#if defined(_WIN32)
        VirtualFree(region, 0, MEM_RELEASE);
#else
        munmap(region, region_size);
#endif
    }

    GuardedBytes(const GuardedBytes&) = delete;
    GuardedBytes& operator=(const GuardedBytes&) = delete;

    std::string_view view() const { return content; }
};

std::string text_for(std::mt19937& rng) {
    static const char* const words[] = {"rocket", "launch",  "pad",   "caf\xC3\xA9", "\xE6\x9D\xB1\xE4\xBA\xAC",
                                        "fox",    "den",     "alpha", "beta",       "getUserById",
                                        "parse_http_request",        "garden",      "caf\xC3\xA9s"};
    std::string text;
    const std::size_t count = 1 + rng() % 14;
    for (std::size_t i = 0; i < count; i++) {
        text += std::string(words[rng() % std::size(words)]) + (rng() % 4 == 0 ? std::to_string(rng() % 20) : "") + " ";
    }
    return text;
}

Indexer corpus(bool positions) {
    std::mt19937 rng(positions ? 3 : 4);
    Indexer indexer("", IndexOptions{.positions = positions});
    for (int i = 0; i < 40; i++) {
        Document document;
        document.add({"title", text_for(rng), FieldOptions::text()});
        document.add({"body", text_for(rng), FieldOptions::text()});
        if (i % 7 == 0) {
            document.add({"note", "stored only " + std::to_string(i), {.stored = true, .indexed = false, .analyzed = false}});
        }
        indexer.add_document("key" + std::to_string(i), document);
    }
    for (int i = 0; i < 40; i += 5) {
        indexer.remove_document("key" + std::to_string(i));
    }
    return indexer;
}

std::string with_body_length(std::string bytes, std::uint64_t body) {
    for (int i = 0; i < 8; i++) {
        bytes[8 + i] = static_cast<char>(body >> (8 * i));
    }
    return bytes;
}

enum class Outcome { loaded, rejected, escaped };

Outcome load_guarded(const std::string& bytes, bool positions) {
    const GuardedBytes guarded(bytes);
    try {
        Indexer loaded = Indexer::deserialize(guarded.view(), "", IndexOptions{.positions = positions});
        loaded.add_document("probe", [] {
            Document document;
            document.add({"title", "alpha", FieldOptions::text()});
            document.add({"body", "beta gamma", FieldOptions::text()});
            return document;
        }());
        Searcher(loaded.get_index(), loaded.get_analyzer()).search("alpha beta");
        Searcher(loaded.get_index(), loaded.get_analyzer()).search("rocket launc");
        Indexer::deserialize(loaded.serialize(), "", IndexOptions{.positions = positions});
        return Outcome::loaded;
    } catch (const std::runtime_error&) {
        return Outcome::rejected;
    } catch (...) {
        return Outcome::escaped;
    }
}

}

TEST(every_truncation_of_a_saved_index_is_rejected_without_reading_past_the_end) {
    for (const bool positions : {false, true}) {
        const std::string bytes = corpus(positions).serialize();
        for (std::size_t length = 0; length < bytes.size(); length++) {
            CHECK(load_guarded(bytes.substr(0, length), positions) == Outcome::rejected);
            if (length >= 16) {
                const std::string cut = with_body_length(bytes.substr(0, length), length - 16);
                CHECK(load_guarded(cut, positions) == Outcome::rejected);
            }
        }
    }
}

TEST(random_multi_byte_damage_and_insertions_never_escape_as_anything_but_a_rejection) {
    for (const bool positions : {false, true}) {
        const std::string bytes = corpus(positions).serialize();
        std::mt19937 rng(positions ? 91 : 92);
        for (int round = 0; round < 1500; round++) {
            std::string damaged = bytes;
            const int edits = 1 + static_cast<int>(rng() % 5);
            for (int edit = 0; edit < edits; edit++) {
                const std::size_t at = 16 + rng() % (damaged.size() - 16);
                switch (rng() % 3) {
                case 0:
                    damaged[at] = static_cast<char>(rng() % 256);
                    break;
                case 1:
                    damaged.insert(at, 1, static_cast<char>(rng() % 256));
                    break;
                default:
                    damaged.erase(at, 1 + rng() % 3);
                    break;
                }
            }
            damaged = with_body_length(damaged, damaged.size() - 16);
            CHECK(load_guarded(damaged, positions) != Outcome::escaped);
        }
    }
}

namespace {

struct TempFile {
    std::filesystem::path path;

    TempFile() : path(std::filesystem::temp_directory_path() / "luxcore_load_fuzz.lux") {
        std::filesystem::remove(path);
    }

    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void write(const std::string& bytes) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
};

}

TEST(loading_damaged_files_through_the_memory_mapped_path_rejects_them_cleanly) {
    for (const bool positions : {false, true}) {
        const std::string bytes = corpus(positions).serialize();
        const TempFile file;
        const luxcore::IndexConfig config{.positions = positions};
        std::mt19937 rng(positions ? 5 : 6);

        const auto load = [&](const std::string& content) {
            file.write(content);
            try {
                const luxcore::Index loaded = luxcore::Index::load(file.path.string(), config);
                loaded.search("rocket", 5);
                return Outcome::loaded;
            } catch (const luxcore::FormatError&) {
                return Outcome::rejected;
            } catch (const luxcore::IoError&) {
                return content.empty() ? Outcome::rejected : Outcome::escaped;
            } catch (...) {
                return Outcome::escaped;
            }
        };

        CHECK(load(bytes) == Outcome::loaded);
        for (std::size_t length = 0; length < bytes.size(); length += 1 + length / 40) {
            CHECK(load(bytes.substr(0, length)) == Outcome::rejected);
        }
        for (int round = 0; round < 300; round++) {
            std::string damaged = bytes;
            damaged[rng() % damaged.size()] ^= static_cast<char>(1 + rng() % 255);
            CHECK(load(damaged) != Outcome::escaped);
        }
    }
}
