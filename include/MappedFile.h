#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

// A read-only view of a whole file. The pages are file-backed, so reading them
// does not add to the process's private memory.
class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    std::string_view bytes() const { return {data, size}; }

private:
    const char* data = nullptr;
    std::size_t size = 0;
#if defined(_WIN32)
    void* file = nullptr;
    void* mapping = nullptr;
#else
    int descriptor = -1;
#endif
};
