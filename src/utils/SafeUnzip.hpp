#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::zip {

struct ZipInspectionLimits {
    size_t maxEntries = 512;
    uint64_t maxSingleEntryBytes = 32ull * 1024 * 1024;
    uint64_t maxTotalUncompressedBytes = 64ull * 1024 * 1024;
    uint64_t maxAllowedRatio = 250; // applied when entry > 1 MB
};

inline bool isSafeRelativeZipEntry(std::string_view name) {
    if (name.empty()) return false;
    if (name.find('\0') != std::string_view::npos) return false;
    if (name.front() == '/' || name.front() == '\\') return false;
    if (name.size() >= 2 && name[1] == ':') return false;

    size_t start = 0;
    while (start <= name.size()) {
        size_t sep = name.find_first_of("/\\", start);
        size_t end = (sep == std::string_view::npos) ? name.size() : sep;
        auto seg = name.substr(start, end - start);
        if (seg == "..") return false;
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }
    return true;
}

inline uint16_t readU16LE(uint8_t const* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readU32LE(uint8_t const* p) {
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t readU64LE(uint8_t const* p) {
    return static_cast<uint64_t>(readU32LE(p)) | (static_cast<uint64_t>(readU32LE(p + 4)) << 32);
}

// Inspects the ZIP Central Directory before any decompression occurs to block
// ZIP bombs (huge uncompressed totals / extreme ratios) and ZipSlip entries.
inline bool inspectZipFile(std::filesystem::path const& path, ZipInspectionLimits const& limits, std::string& errorOut) {
    std::error_code ec;
    auto fileSizeU = std::filesystem::file_size(path, ec);
    if (ec || fileSizeU < 22) {
        errorOut = "Invalid or empty ZIP archive.";
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errorOut = "Failed to open ZIP archive.";
        return false;
    }

    // End of Central Directory (EOCD) sits in the last <= 65,557 bytes.
    uint64_t const tailRead = std::min<uint64_t>(fileSizeU, 65536ull + 22ull);
    in.seekg(static_cast<std::streamoff>(fileSizeU - tailRead), std::ios::beg);
    std::vector<uint8_t> tail(static_cast<size_t>(tailRead));
    if (!in.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()))) {
        errorOut = "Failed to read ZIP directory.";
        return false;
    }

    size_t eocdOffset = std::string::npos;
    for (size_t i = tail.size() - 22 + 1; i > 0; --i) {
        size_t pos = i - 1;
        if (tail[pos] == 0x50 && tail[pos + 1] == 0x4b && tail[pos + 2] == 0x05 && tail[pos + 3] == 0x06) {
            uint16_t commentLen = readU16LE(tail.data() + pos + 20);
            if (pos + 22 + commentLen == tail.size()) {
                eocdOffset = pos;
                break;
            }
        }
    }
    if (eocdOffset == std::string::npos) {
        errorOut = "ZIP central directory header not found.";
        return false;
    }

    uint16_t totalEntries = readU16LE(tail.data() + eocdOffset + 10);
    uint32_t cdSize = readU32LE(tail.data() + eocdOffset + 12);
    uint32_t cdOffset = readU32LE(tail.data() + eocdOffset + 16);

    if (totalEntries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        errorOut = "Unsupported oversized ZIP64 archive.";
        return false;
    }
    if (totalEntries > limits.maxEntries) {
        errorOut = "ZIP contains too many entries.";
        return false;
    }
    if (static_cast<uint64_t>(cdOffset) + static_cast<uint64_t>(cdSize) > fileSizeU || cdSize > 16u * 1024 * 1024) {
        errorOut = "Corrupt ZIP central directory bounds.";
        return false;
    }

    std::vector<uint8_t> cd(cdSize);
    in.seekg(static_cast<std::streamoff>(cdOffset), std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cd.size()))) {
        errorOut = "Failed to read ZIP central directory.";
        return false;
    }

    uint64_t totalUncompressed = 0;
    size_t pos = 0;
    size_t parsedEntries = 0;
    while (pos + 46 <= cd.size()) {
        if (cd[pos] != 0x50 || cd[pos + 1] != 0x4b || cd[pos + 2] != 0x01 || cd[pos + 3] != 0x02) {
            errorOut = "Invalid ZIP central directory entry.";
            return false;
        }
        uint64_t compSize = readU32LE(cd.data() + pos + 20);
        uint64_t uncompSize = readU32LE(cd.data() + pos + 24);
        uint16_t nameLen = readU16LE(cd.data() + pos + 28);
        uint16_t extraLen = readU16LE(cd.data() + pos + 30);
        uint16_t commentLen = readU16LE(cd.data() + pos + 32);

        size_t entryEnd = pos + 46 + nameLen + extraLen + commentLen;
        if (entryEnd > cd.size()) {
            errorOut = "Truncated ZIP central directory entry.";
            return false;
        }

        std::string_view entryName(reinterpret_cast<char const*>(cd.data() + pos + 46), nameLen);
        if (!isSafeRelativeZipEntry(entryName)) {
            errorOut = "Unsafe path inside ZIP archive.";
            return false;
        }

        // Check Zip64 extra field (0x0001) if present so forged 0xFFFFFFFF fields cannot bypass limits.
        size_t ep = pos + 46 + nameLen;
        size_t const extraEnd = ep + extraLen;
        while (ep + 4 <= extraEnd) {
            uint16_t hdrId = readU16LE(cd.data() + ep);
            uint16_t dataSz = readU16LE(cd.data() + ep + 2);
            if (ep + 4 + dataSz > extraEnd) break;
            if (hdrId == 0x0001) {
                size_t zOff = ep + 4;
                if (uncompSize == 0xFFFFFFFFu && zOff + 8 <= ep + 4 + dataSz) {
                    uncompSize = readU64LE(cd.data() + zOff);
                    zOff += 8;
                }
                if (compSize == 0xFFFFFFFFu && zOff + 8 <= ep + 4 + dataSz) {
                    compSize = readU64LE(cd.data() + zOff);
                }
            }
            ep += 4 + dataSz;
        }

        if (uncompSize > limits.maxSingleEntryBytes) {
            errorOut = "ZIP entry exceeds maximum allowed uncompressed size.";
            return false;
        }
        if (uncompSize > 1024ull * 1024ull) {
            uint64_t effectiveComp = std::max<uint64_t>(compSize, 1);
            if (uncompSize / effectiveComp > limits.maxAllowedRatio) {
                errorOut = "Suspicious ZIP compression ratio detected.";
                return false;
            }
        }
        if (totalUncompressed > limits.maxTotalUncompressedBytes - uncompSize) {
            errorOut = "ZIP total uncompressed size exceeds safety limit.";
            return false;
        }
        totalUncompressed += uncompSize;
        ++parsedEntries;
        if (parsedEntries > limits.maxEntries) {
            errorOut = "ZIP contains too many entries.";
            return false;
        }
        pos = entryEnd;
    }

    return true;
}

} // namespace paimon::zip
