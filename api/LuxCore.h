#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace luxcore {

class IoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class QueryError : public std::invalid_argument {
public:
    QueryError(const std::string& message, std::size_t offset);
    std::size_t offset() const;

private:
    std::size_t error_offset;
};

struct Hit {
    std::string key;
    double score;
};

struct IndexConfig {
    bool positions = false;
};

struct SnippetOptions {
    std::size_t length = 160;
    std::string start_marker = "<em>";
    std::string end_marker = "</em>";
    std::string ellipsis = "...";
};

struct PrepareOptions {
    bool store_body = true;
};

class Prepared {
public:
    ~Prepared();
    Prepared(Prepared&&) noexcept;
    Prepared& operator=(Prepared&&) noexcept;
    Prepared(const Prepared&) = delete;
    Prepared& operator=(const Prepared&) = delete;

private:
    friend class Index;
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit Prepared(std::unique_ptr<Impl> impl);
};

class Index {
public:
    Index();
    explicit Index(IndexConfig config);
    ~Index();
    Index(Index&&) noexcept;
    Index& operator=(Index&&) noexcept;
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    void add(const std::string& key, const std::string& title, const std::string& body);
    Prepared prepare(std::string_view title, std::string_view body, PrepareOptions options = {}) const;
    void commit(const std::string& key, Prepared prepared);
    bool remove(const std::string& key);
    std::size_t size() const;

    std::optional<std::string> stored(const std::string& key, const std::string& field) const;
    std::vector<Hit> search(const std::string& query, std::size_t limit = 10) const;
    std::vector<std::string> suggest(const std::string& query, std::size_t limit = 5) const;

    std::vector<Hit> search_query(const std::string& text, std::size_t limit = 10) const;
    std::optional<std::string> snippet(const std::string& key, const std::string& field, const std::string& text,
                                       const SnippetOptions& options = {}) const;
    std::optional<std::string> snippet_query(const std::string& key, const std::string& field, const std::string& text,
                                             const SnippetOptions& options = {}) const;

    std::string snippet_text(std::string_view body, const std::string& text, const SnippetOptions& options = {}) const;
    std::string snippet_text_query(std::string_view body, const std::string& text,
                                   const SnippetOptions& options = {}) const;

    std::string debug_stats() const;

    void save(const std::string& path) const;
    static Index load(const std::string& path, IndexConfig config = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}
