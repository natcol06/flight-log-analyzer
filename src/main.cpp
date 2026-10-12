#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Make sure the user gave us a file name
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <log.ulg>\n";
        return 1;
    }

    // 2. Open the file in binary mode
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Could not open " << argv[1] << "\n";
        return 1;
    }

    // 3. Read the first 16 bytes
    std::array<std::uint8_t, 16> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    if (file.gcount() != static_cast<std::streamsize>(header.size())) {
        std::cerr << "File is too short to be a ULog file\n";
        return 1;
    }

    // 4. Check the magic bytes
    const std::array<std::uint8_t, 7> magic = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
    if (std::memcmp(header.data(), magic.data(), magic.size()) != 0) {
        std::cerr << "Not a ULog file (magic bytes don't match)\n";
        return 1;
    }

    // 5. Byte 7 is the version
    std::uint8_t version = header[7];

    // 6. Bytes 8-15 are the start time, stored little endian
    std::uint64_t start_us = 0;
    for (int i = 0; i < 8; ++i) {
        start_us |= static_cast<std::uint64_t>(header[8 + i]) << (8 * i);
    }

    std::cout << "Valid ULog file\n";
    std::cout << "Version: " << static_cast<int>(version) << "\n";
    std::cout << "Logging started at: " << start_us << " us ("
              << start_us / 1e6 << " s)\n";
    return 0;
}