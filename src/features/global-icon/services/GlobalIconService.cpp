#include "GlobalIconService.hpp"
#include "GlobalIconClient.hpp"
#include "../GlobalIconTypes.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../utils/Debug.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/Base64.hpp"

#include <matjson.hpp>
#include <fstream>
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
        std::ifstream in(p, std::ios::binary | std::ios::ate);
        if (!in) return std::nullopt;

        std::streamoff const size = in.tellg();
        if (size <= 0 || size > kMaxFileBytes) return std::nullopt;
        in.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!in.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
        return std::move(data);
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

std::string GlobalIconService::describeSyncError(std::string const& response) {
    auto const& loc = Localization::get();

    // HttpClient formats failures as "HTTP <code>: <body>".
    int status = 0;
    if (response.rfind("HTTP ", 0) == 0) {
        auto colon = response.find(':');
        if (colon != std::string::npos) {
            auto parsed = geode::utils::numFromString<int>(response.substr(5, colon - 5));
            if (parsed.isOk()) status = parsed.unwrap();
        }
    }

    switch (status) {
        case 401:
        case 403:
            return loc.getString("globalicon.err_unauthorized");
        case 404:
        case 405:
            // The route isn't there: the deployed server predates /api/icons/sync.
            return loc.getString("globalicon.err_outdated");
        case 413:
            return loc.getString("globalicon.err_too_large");
        case 500:
        case 502:
        case 503:
            return loc.getString("globalicon.err_server");
        default:
            break;
    }

    // 400s carry the server's own validation message, which is the useful part.
    auto brace = response.find('{');
    if (brace != std::string::npos) {
        auto parsed = matjson::parse(response.substr(brace));
        if (parsed.isOk()) {
            auto const& err = parsed.unwrap()["error"];
            if (err.isString()) {
                auto text = err.asString().unwrapOr("");
                if (!text.empty()) return text;
            }
        }
    }

    if (status == 0) return loc.getString("globalicon.err_offline");
    return loc.getString("globalicon.upload_failed");
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

        std::optional<std::vector<uint8_t>> plistBytes;
        auto sheetPath = info->getSheet();
        if (!sheetPath.empty()) {
            plistBytes = readFile(sheetPath);
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
            {"pngData", paimon::base64Encode(*pngBytes)},
        });
        if (plistBytes && !plistBytes->empty()) {
            slot.set("plistData", paimon::base64Encode(*plistBytes));
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
        [cb = std::move(cb), accountID](bool success, std::string const& resp) {
            if (!success) {
                log::warn("[GlobalIcon] sync failed: {}", resp);
            }
            // The cached document is stale either way: a success replaced it,
            // and a failure may have left the server mid-change.
            GlobalIconClient::get().invalidate(accountID);
            if (cb) cb(success, resp);
        });
}

void GlobalIconService::clearIcons(int accountID, std::string const& username, ResultCallback cb) {
    GlobalIconClient::get().clearIcons(accountID, username,
        [cb = std::move(cb), accountID](bool success, std::string const& resp) {
            GlobalIconClient::get().invalidate(accountID);
            if (cb) cb(success, resp);
        });
}

} // namespace paimon::globalicon
