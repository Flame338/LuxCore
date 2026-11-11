#include "MappedFile.h"

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

MappedFile::MappedFile(const std::filesystem::path& path) {
    // Share delete and write so a reader never blocks the writer's rename over this path.
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        file = nullptr;
        throw std::runtime_error("cannot open for reading");
    }
    LARGE_INTEGER length;
    if (!GetFileSizeEx(file, &length)) {
        CloseHandle(file);
        throw std::runtime_error("cannot read the file size");
    }
    if (length.QuadPart == 0) {
        return;
    }
    mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const void* view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (view == nullptr) {
        if (mapping) {
            CloseHandle(mapping);
        }
        CloseHandle(file);
        throw std::runtime_error("cannot map the file");
    }
    data = static_cast<const char*>(view);
    size = static_cast<std::size_t>(length.QuadPart);
}

MappedFile::~MappedFile() {
    if (data) {
        UnmapViewOfFile(data);
    }
    if (mapping) {
        CloseHandle(mapping);
    }
    if (file) {
        CloseHandle(file);
    }
}

#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

MappedFile::MappedFile(const std::filesystem::path& path) {
    descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        throw std::runtime_error("cannot open for reading");
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0) {
        close(descriptor);
        throw std::runtime_error("cannot read the file size");
    }
    if (status.st_size == 0) {
        return;
    }
    void* view = mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (view == MAP_FAILED) {
        close(descriptor);
        throw std::runtime_error("cannot map the file");
    }
    madvise(view, static_cast<std::size_t>(status.st_size), MADV_SEQUENTIAL);
    data = static_cast<const char*>(view);
    size = static_cast<std::size_t>(status.st_size);
}

MappedFile::~MappedFile() {
    if (data) {
        munmap(const_cast<char*>(data), size);
    }
    if (descriptor >= 0) {
        close(descriptor);
    }
}

#endif
