#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luxcore {

struct Hit {
    std::string key;
    double score;
};

class Index {
public:
    Index();
    ~Index();
    Index(Index&&) noexcept;
    Index& operator=(Index&&) noexcept;
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    void add(const std::string& key, const std::string& title, const std::string& body);
    bool remove(const std::string& key);
    std::size_t size() const;

    std::optional<std::string> stored(const std::string& key, const std::string& field) const;
    std::vector<Hit> search(const std::string& query, std::size_t limit = 10) const;
    std::vector<std::string> suggest(const std::string& query, std::size_t limit = 5) const;

    void save(const std::string& path) const;
    static Index load(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}
