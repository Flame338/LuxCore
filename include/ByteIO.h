#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

[[noreturn]] void fail_format(const std::string& message);

class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual void write(std::string_view bytes) = 0;
    virtual void overwrite(std::uint64_t offset, std::string_view bytes) = 0;
};

class StringSink : public ByteSink {
public:
    std::string bytes;

    void write(std::string_view more) override { bytes.append(more); }
    void overwrite(std::uint64_t offset, std::string_view replacement) override {
        bytes.replace(static_cast<std::size_t>(offset), replacement.size(), replacement);
    }
};

class ByteWriter {
private:
    static constexpr std::size_t flush_size = 1 << 20;

    std::string buffer;
    ByteSink* sink = nullptr;
    std::uint64_t flushed = 0;

    void put(std::uint8_t value) { buffer.push_back(static_cast<char>(value)); }

    void spill() {
        if (sink != nullptr && buffer.size() >= flush_size) {
            flush();
        }
    }

public:
    ByteWriter() = default;
    explicit ByteWriter(ByteSink& target) : sink(&target) { buffer.reserve(flush_size + 64); }

    void reserve(std::size_t size) { buffer.reserve(size); }

    void flush() {
        if (sink != nullptr && !buffer.empty()) {
            sink->write(buffer);
            flushed += buffer.size();
            buffer.clear();
        }
    }

    void u8(std::uint8_t value) {
        put(value);
        spill();
    }

    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            put(static_cast<std::uint8_t>(value >> shift));
        }
        spill();
    }

    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            put(static_cast<std::uint8_t>(value >> shift));
        }
        spill();
    }

    void varint(std::uint64_t value) {
        while (value >= 0x80) {
            put(static_cast<std::uint8_t>(value | 0x80));
            value >>= 7;
        }
        put(static_cast<std::uint8_t>(value));
        spill();
    }

    void string(std::string_view value) {
        varint(value.size());
        append(value);
    }

    void append(std::string_view bytes) {
        if (sink != nullptr && buffer.size() + bytes.size() > flush_size) {
            flush();
            if (bytes.size() >= flush_size) {
                sink->write(bytes);
                flushed += bytes.size();
                return;
            }
        }
        buffer.append(bytes);
        spill();
    }

    std::uint64_t size() const { return flushed + buffer.size(); }
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
