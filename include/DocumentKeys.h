#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class ByteWriter;
class ByteReader;
class DocumentStore;

class DocumentKeys {
private:
    std::unordered_map<std::string, int> ids;
    std::vector<std::string> keys;

public:
    std::optional<int> find(const std::string& key) const {
        auto it = ids.find(key);
        if (it == ids.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    const std::string& key_of(int id) const {
        static const std::string none;
        if (id < 0 || static_cast<std::size_t>(id) >= keys.size()) {
            return none;
        }
        return keys[static_cast<std::size_t>(id)];
    }

    void bind(const std::string& key, int id) {
        if (static_cast<std::size_t>(id) >= keys.size()) {
            keys.resize(static_cast<std::size_t>(id) + 1);
        }
        keys[static_cast<std::size_t>(id)] = key;
        ids[key] = id;
    }

    void unbind(int id) {
        if (id < 0 || static_cast<std::size_t>(id) >= keys.size() || keys[static_cast<std::size_t>(id)].empty()) {
            return;
        }
        ids.erase(keys[static_cast<std::size_t>(id)]);
        keys[static_cast<std::size_t>(id)].clear();
    }

    void write(ByteWriter& writer) const;
    static DocumentKeys read(ByteReader& reader, const DocumentStore& store);
};
