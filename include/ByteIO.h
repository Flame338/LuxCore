#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

[[noreturn]] void fail_format(const std::string& message);

class ByteWriter {
private:
    std::string buffer;

public:
    void reserve(std::size_t size) { buffer.reserve(size); }

    void u8(std::uint8_t value) { buffer.push_back(static_cast<char>(value)); }

    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void varint(std::uint64_t value) {
        while (value >= 0x80) {
            u8(static_cast<std::uint8_t>(value | 0x80));
            value >>= 7;
        }
        u8(static_cast<std::uint8_t>(value));
    }

    void string(std::string_view value) {
        varint(value.size());
        buffer.append(value);
    }

    void append(std::string_view bytes) { buffer.append(bytes); }

    std::size_t size() const { return buffer.size(); }
    const std::string& bytes() const { return buffer; }
    std::string take() { return std::move(buffer); }
};

class ByteReader {
private:
    const char* cursor;
    const char* end;
    std::size_t total;

public:
    explicit ByteReader(std::string_view data)
        : cursor(data.data()), end(data.data() + data.size()), total(data.size()) {}

    std::size_t remaining() const { return static_cast<std::size_t>(end - cursor); }
    std::size_t total_size() const { return total; }
    bool at_end() const { return cursor == end; }

    std::uint8_t u8() {
        if (cursor == end) {
            fail_format("unexpected end of data");
        }
        return static_cast<std::uint8_t>(*cursor++);
    }

    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return value;
    }

    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(u8()) << shift;
        }
        return value;
    }

    std::uint64_t varint() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const std::uint8_t byte = u8();
            if (shift == 63 && byte > 1) {
                fail_format("variable-length integer is too large");
            }
            value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return value;
            }
        }
        fail_format("variable-length integer is too long");
    }

    std::uint64_t varint_at_most(const char* what, std::uint64_t limit) {
        const std::uint64_t value = varint();
        if (value > limit) {
            fail_format(std::string(what) + " " + std::to_string(value) + " is out of range");
        }
        return value;
    }

    std::size_t count(const char* what, std::size_t min_bytes_each) {
        const std::uint64_t value = varint();
        if (value > remaining() / min_bytes_each) {
            fail_format(std::string(what) + " " + std::to_string(value) + " does not fit in the remaining " +
                        std::to_string(remaining()) + " bytes");
        }
        return static_cast<std::size_t>(value);
    }

    std::string_view string(const char* what) {
        const std::size_t length = count(what, 1);
        const std::string_view value(cursor, length);
        cursor += length;
        return value;
    }
};
