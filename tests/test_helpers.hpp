#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// ---------- Write little-endian values into a byte list ----------

inline void put_u8(std::vector<std::uint8_t>& out, std::uint8_t v) {
    out.push_back(v);
}

inline void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

inline void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

inline void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

inline void put_float(std::vector<std::uint8_t>& out, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    put_u32(out, bits);
}

// ---------- Builds a small ULog file one message at a time ----------

class ULogBuilder {
public:
    explicit ULogBuilder(std::uint64_t start_us = 1000000) {
        const std::uint8_t magic[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
        bytes_.insert(bytes_.end(), std::begin(magic), std::end(magic));
        bytes_.push_back(1);  // version
        put_u64(bytes_, start_us);
    }

    void add_format(const std::string& text) {
        add_message('F', std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    void add_subscription(std::uint8_t multi_id, std::uint16_t msg_id, const std::string& name) {
        std::vector<std::uint8_t> p;
        put_u8(p, multi_id);
        put_u16(p, msg_id);
        p.insert(p.end(), name.begin(), name.end());
        add_message('A', p);
    }

    void add_data(std::uint16_t msg_id, const std::vector<std::uint8_t>& data) {
        std::vector<std::uint8_t> p;
        put_u16(p, msg_id);
        p.insert(p.end(), data.begin(), data.end());
        add_message('D', p);
    }

    // Removes bytes from the end to fake a log that got cut off
    void cut_last(std::size_t n) { bytes_.resize(bytes_.size() - n); }

    // Writes the file to the computer's temp folder and returns its path
    std::string save(const std::string& name) const {
        std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes_.data()),
                  static_cast<std::streamsize>(bytes_.size()));
        return path.string();
    }

private:
    void add_message(char type, const std::vector<std::uint8_t>& payload) {
        put_u16(bytes_, static_cast<std::uint16_t>(payload.size()));
        bytes_.push_back(static_cast<std::uint8_t>(type));
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    std::vector<std::uint8_t> bytes_;
};