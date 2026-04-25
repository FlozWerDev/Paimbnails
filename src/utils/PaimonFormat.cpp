#include "PaimonFormat.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

namespace PaimonFormat {
    bool save(std::filesystem::path const& path, std::vector<uint8_t> const& data) {
        (void)geode::utils::file::createDirectoryAll(path.parent_path());

        auto encrypted = encrypt(data);

        // Build the full file in memory: magic(6) + version(1) + size(4) + data(N) + hash(8)
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
        auto readRes = geode::utils::file::readBinary(path);
        if (readRes.isErr()) {
            // File doesn't exist or can't be read — silent for missing, log for errors
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                log::error("[PaimonFormat] Failed to read file: {}", readRes.unwrapErr());
            }
            return {};
        }

        auto buf = std::move(readRes.unwrap());
        uint8_t const* ptr = buf.data();
        size_t remaining = buf.size();

        // magic(6)
        if (remaining < 6 || std::string(reinterpret_cast<char const*>(ptr), 6) != "PAIMON") {
            log::error("[PaimonFormat] Invalid file format (bad magic header)");
            return {};
        }
        ptr += 6; remaining -= 6;

        // version(1)
        if (remaining < 1) return {};
        uint8_t version = *ptr;
        ptr += 1; remaining -= 1;
        if (version > 2) {
            log::warn("[PaimonFormat] Unsupported future file version: {}", version);
            return {};
        }

        // size(4)
        if (remaining < 4) return {};
        uint32_t size;
        memcpy(&size, ptr, 4);
        ptr += 4; remaining -= 4;

        if (size == 0 || size > 10 * 1024 * 1024 || size > remaining) {
            log::error("[PaimonFormat] Invalid data size: {}", size);
            return {};
        }

        // encrypted data
        std::vector<uint8_t> encrypted(ptr, ptr + size);
        ptr += size; remaining -= size;

        // hash check (v2+)
        if (version >= 2) {
            if (remaining < 8) {
                log::warn("[PaimonFormat] Incomplete v2 file (missing hash)");
                return {};
            }
            uint64_t storedHash;
            memcpy(&storedHash, ptr, 8);

            uint64_t calculatedHash = calculateHash(encrypted);
            if (storedHash != calculatedHash) {
                log::error("[PaimonFormat] Integrity check failed: file was modified or corrupted.");
                return {};
            }
        }

        return decrypt(encrypted);
    }
}

