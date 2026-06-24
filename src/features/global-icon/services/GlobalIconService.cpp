#include "GlobalIconService.hpp"
#include "GlobalIconClient.hpp"
#include "../GlobalIconTypes.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../utils/Debug.hpp"

#include <matjson.hpp>
#include <fstream>
#include <iterator>
#include <optional>

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

using namespace geode::prelude;

namespace paimon::globalicon {

namespace {
    constexpr int64_t kMaxFileBytes = 4 * 1024 * 1024;   // 4 MB (server limit)
    constexpr int64_t kMaxSyncBytes = 20 * 1024 * 1024;  // 20 MB (server limit)

    std::optional<std::vector<uint8_t>> readFile(std::filesystem::path const& p) {
        if (p.empty()) return std::nullopt;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) return std::nullopt;
        std::ifstream in(p, std::ios::binary);
        if (!in) return std::nullopt;
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::string base64Encode(std::vector<uint8_t> const& data) {
        static char const* T =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);
        size_t n = data.size(), i = 0;
        for (; i + 2 < n; i += 3) {
            unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                         (static_cast<unsigned>(data[i + 1]) << 8) |
                          static_cast<unsigned>(data[i + 2]);
            out += T[(v >> 18) & 0x3F];
            out += T[(v >> 12) & 0x3F];
            out += T[(v >> 6) & 0x3F];
            out += T[v & 0x3F];
        }
        if (i < n) {
            unsigned v = static_cast<unsigned>(data[i]) << 16;
            if (i + 1 < n) v |= static_cast<unsigned>(data[i + 1]) << 8;
            out += T[(v >> 18) & 0x3F];
            out += T[(v >> 12) & 0x3F];
            out += (i + 1 < n) ? T[(v >> 6) & 0x3F] : '=';
            out += '=';
        }
        return out;
    }
}

GlobalIconService& GlobalIconService::get() {
    static GlobalIconService instance;
    return instance;
}

bool GlobalIconService::isEnabledLocally() {
    return Mod::get()->getSavedValue<bool>("global-icon-enabled", false);
}

void GlobalIconService::setEnabledLocally(bool enabled) {
    Mod::get()->setSavedValue<bool>("global-icon-enabled", enabled);
}

void GlobalIconService::uploadActiveIcons(int accountID, std::string const& username, ResultCallback cb) {
    if (!paimon::compat::ModCompat::isMoreIconsLoaded()) {
        if (cb) cb(false, "More Icons not installed");
        return;
    }
    if (accountID <= 0) {
        if (cb) cb(false, "Invalid account");
        return;
    }

    matjson::Value iconsArr = matjson::Value::array();
    int64_t totalBytes = 0;
    int slots = 0;

    for (IconType type : syncableIconTypes()) {
        auto* info = more_icons::activeIcon(type);
        if (!info) continue;          // vanilla/none -> not uploaded
        if (info->isVanilla()) continue;
        if (info->isZipped()) continue; // files inside a .zip: not directly readable

        auto pngBytes = readFile(info->getTexture());
        if (!pngBytes || pngBytes->empty()) continue;
        if (static_cast<int64_t>(pngBytes->size()) > kMaxFileBytes) continue;

        std::optional<std::vector<uint8_t>> plistBytes;
        auto sheetPath = info->getSheet();
        if (!sheetPath.empty()) {
            plistBytes = readFile(sheetPath);
            if (plistBytes && static_cast<int64_t>(plistBytes->size()) > kMaxFileBytes) {
                plistBytes.reset();
            }
        }

        int64_t slotBytes = static_cast<int64_t>(pngBytes->size()) +
            (plistBytes ? static_cast<int64_t>(plistBytes->size()) : 0);
        if (totalBytes + slotBytes > kMaxSyncBytes) break;
        totalBytes += slotBytes;

        matjson::Value slot = matjson::makeObject({
            {"type", std::string(iconTypeToString(type))},
            {"name", info->getName()},
            {"packID", info->getPackID()},
            {"packName", info->getPackName()},
            {"quality", info->getQuality()},
            {"specialID", info->getSpecialID()},
            {"fireCount", info->getFireCount()},
            {"pngData", base64Encode(*pngBytes)},
        });
        if (plistBytes && !plistBytes->empty()) {
            slot.set("plistData", base64Encode(*plistBytes));
        }
        iconsArr.push(slot);
        if (++slots >= 24) break; // server limit
    }

    if (slots == 0) {
        if (cb) cb(false, "no custom icons");
        return;
    }

    matjson::Value body = matjson::makeObject({
        {"accountID", accountID},
        {"username", username},
        {"enabled", true},
        {"icons", iconsArr},
    });

    PaimonDebug::log("[GlobalIcon] Uploading {} icon slots ({} bytes)", slots, totalBytes);
    GlobalIconClient::get().syncIcons(body.dump(matjson::NO_INDENTATION),
        [cb = std::move(cb)](bool success, std::string const& resp) {
            if (!success) {
                log::warn("[GlobalIcon] sync failed: {}", resp);
            }
            if (cb) cb(success, resp);
        });
}

void GlobalIconService::clearIcons(int accountID, std::string const& username, ResultCallback cb) {
    GlobalIconClient::get().clearIcons(accountID, username,
        [cb = std::move(cb)](bool success, std::string const& resp) {
            if (cb) cb(success, resp);
        });
}

} // namespace paimon::globalicon
