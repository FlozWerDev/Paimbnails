#include "PaimonFormat.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <cstddef>
#include <cstring>

using namespace geode::prelude;

namespace PaimonFormat {
namespace {
    constexpr std::size_t kMaxDataBytes = 10ull * 1024 * 1024;
    constexpr uint8_t kXorKey[] = {0x50, 0x41, 0x49, 0x4D, 0x4F, 0x4E, 0x5F, 0x53, 0x45, 0x43, 0x52, 0x45, 0x54};

    uint64_t calculateHash(std::vector<uint8_t> const& data) {
        constexpr uint64_t kSalt = 0x9E3779B97F4A7C15;
        uint64_t hash = 0xCBF29CE484222325;
        for (int i = 0; i < 8; ++i) {
            hash ^= (kSalt >> (i * 8)) & 0xFF;
            hash *= 0x100000001B3;
        }
        for (uint8_t byte : data) {
            hash ^= byte;
            hash *= 0x100000001B3;
        }
        return hash;
    }

    std::vector<uint8_t> xorBytes(std::vector<uint8_t> const& data) {
        std::vector<uint8_t> result(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            result[i] = data[i] ^ kXorKey[i % sizeof(kXorKey)];
        }
        return result;
    }
}

    bool save(std::filesystem::path const& path, std::vector<uint8_t> const& data) {
        if (data.size() > kMaxDataBytes) {
            log::error("[PaimonFormat] Data exceeds {} byte limit", kMaxDataBytes);
            return false;
        }
        (void)geode::utils::file::createDirectoryAll(path.parent_path());

        auto encrypted = xorBytes(data);

        std::vector<uint8_t> buf;
        buf.reserve(6 + 1 + 4 + encrypted.size() + 8);

        buf.insert(buf.end(), {'P','A','I','M','O','N'});
        uint8_t version = 2;
        buf.push_back(version);

        uint32_t size = static_cast<uint32_t>(encrypted.size());
        buf.insert(buf.end(), reinterpret_cast<uint8_t const*>(&size), reinterpret_cast<uint8_t const*>(&size) + 4);

        buf.insert(buf.end(), encrypted.begin(), encrypted.end());

        uint64_t hash = calculateHash(encrypted);
        buf.insert(buf.end(), reinterpret_cast<uint8_t const*>(&hash), reinterpret_cast<uint8_t const*>(&hash) + 8);

        auto res = geode::utils::file::writeBinary(path, buf);
        if (res.isErr()) {
            log::error("[PaimonFormat] Failed to write file: {}", res.unwrapErr());
            return false;
        }
        return true;
    }
    
    std::vector<uint8_t> load(std::filesystem::path const& path) {
        std::error_code sizeEc;
        auto fileSize = std::filesystem::file_size(path, sizeEc);
        if (sizeEc || fileSize > kMaxDataBytes + 19) return {};
        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) {
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                log::error("[PaimonFormat] Failed to read file: {}", readRes.unwrapErr());
            }
            return {};
        }

        auto buf = std::move(readRes.unwrap());
        uint8_t const* ptr = buf.data();
        size_t remaining = buf.size();

        if (remaining < 6 || std::memcmp(ptr, "PAIMON", 6) != 0) {
            log::error("[PaimonFormat] Invalid file format (bad magic header)");
            return {};
        }
        ptr += 6; remaining -= 6;

        if (remaining < 1) return {};
        uint8_t version = *ptr;
        ptr += 1; remaining -= 1;
        if (version > 2) {
            log::warn("[PaimonFormat] Unsupported future file version: {}", version);
            return {};
        }

        if (remaining < 4) return {};
        uint32_t size;
        std::memcpy(&size, ptr, 4);
        ptr += 4; remaining -= 4;

        if (size > kMaxDataBytes || size > remaining) {
            log::error("[PaimonFormat] Invalid data size: {}", size);
            return {};
        }

        std::vector<uint8_t> encrypted(ptr, ptr + size);
        ptr += size; remaining -= size;

        if (version >= 2) {
            if (remaining < 8) {
                log::warn("[PaimonFormat] Incomplete v2 file (missing hash)");
                return {};
            }
            uint64_t storedHash;
            std::memcpy(&storedHash, ptr, 8);

            uint64_t calculatedHash = calculateHash(encrypted);
            if (storedHash != calculatedHash) {
                log::error("[PaimonFormat] Integrity check failed: file was modified or corrupted.");
                return {};
            }
        }

        return xorBytes(encrypted);
    }
}
