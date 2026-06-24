#include "GlobalIconStorage.hpp"
#include "GlobalIconClient.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../utils/Debug.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

#include <fstream>
#include <system_error>

using namespace geode::prelude;

namespace paimon::globalicon {

namespace {
    cocos2d::TextureQuality qualityFromInt(int q) {
        if (q < 1) q = 1;
        if (q > 3) q = 3;
        return static_cast<cocos2d::TextureQuality>(q);
    }

    bool writeFile(std::filesystem::path const& path, std::vector<uint8_t> const& data) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<char const*>(data.data()), static_cast<std::streamsize>(data.size()));
        return out.good();
    }
}

GlobalIconStorage& GlobalIconStorage::get() {
    static GlobalIconStorage instance;
    return instance;
}

bool GlobalIconStorage::available() {
    return paimon::compat::ModCompat::isMoreIconsLoaded();
}

std::string GlobalIconStorage::registeredName(int accountID, GlobalIconSlot const& slot) {
    return "globalicon-" + std::to_string(accountID) + "-" + slot.name;
}

std::filesystem::path GlobalIconStorage::cacheDir(int accountID, std::string const& type) const {
    return Mod::get()->getSaveDir() / "global_icons_cache" / std::to_string(accountID) / type;
}

void GlobalIconStorage::registerWithMoreIcons(std::string const& regName, GlobalIconSlot const& slot,
                                              IconType type, std::filesystem::path const& png,
                                              std::filesystem::path const& plist, EnsureCallback const& cb) {
    // Wrap external icon edits in pre/refresh (API requirement).
    more_icons::preRefreshIcons();
    auto* info = more_icons::addIcon(
        regName, slot.name, type, png, plist,
        qualityFromInt(slot.quality), "paimon.global-icon", "Global Icon");
    more_icons::refreshIcons();

    if (info) {
        m_registered.insert(regName);
        PaimonDebug::log("[GlobalIcon] Registered icon '{}' (type {})", regName, static_cast<int>(type));
        if (cb) cb(true, regName);
    } else {
        PaimonDebug::warn("[GlobalIcon] addIcon failed for '{}'", regName);
        if (cb) cb(false, "");
    }
}

void GlobalIconStorage::ensureIcon(int accountID, GlobalIconSlot const& slot, EnsureCallback cb) {
    if (!available()) {
        if (cb) cb(false, "");
        return;
    }
    auto typeOpt = iconTypeFromString(slot.type);
    if (!typeOpt || slot.name.empty()) {
        if (cb) cb(false, "");
        return;
    }
    IconType type = *typeOpt;
    std::string regName = registeredName(accountID, slot);

    // already registered this session and still present in More Icons?
    if (m_registered.count(regName) && more_icons::getIcon(regName, type) != nullptr) {
        if (cb) cb(true, regName);
        return;
    }

    auto dir = cacheDir(accountID, slot.type);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto pngName   = slot.pngFile.empty()   ? (slot.name + ".png")   : slot.pngFile;
    auto pngPath   = dir / pngName;
    bool needPlist = !slot.plistUrl.empty();
    auto plistPath = needPlist
        ? dir / (slot.plistFile.empty() ? (slot.name + ".plist") : slot.plistFile)
        : std::filesystem::path{};

    std::error_code pngEc;
    bool havePng = std::filesystem::exists(pngPath, pngEc) && !pngEc;
    std::error_code plistEc;
    bool havePlist = !needPlist ||
        (std::filesystem::exists(plistPath, plistEc) && !plistEc);
    if (havePng && havePlist) {
        registerWithMoreIcons(regName, slot, type, pngPath, plistPath, cb);
        return;
    }

    // Download PNG, then plist (if any), then register.
    GlobalIconClient::get().downloadFile(slot.pngUrl,
        [this, regName, slot, type, pngPath, plistPath, needPlist, cb]
        (bool ok, std::vector<uint8_t> const& data) mutable {
            if (!ok || data.empty() || !writeFile(pngPath, data)) {
                if (cb) cb(false, "");
                return;
            }
            if (!needPlist) {
                registerWithMoreIcons(regName, slot, type, pngPath, plistPath, cb);
                return;
            }
            GlobalIconClient::get().downloadFile(slot.plistUrl,
                [this, regName, slot, type, pngPath, plistPath, cb]
                (bool ok2, std::vector<uint8_t> const& pdata) mutable {
                    if (!ok2 || pdata.empty() || !writeFile(plistPath, pdata)) {
                        if (cb) cb(false, "");
                        return;
                    }
                    registerWithMoreIcons(regName, slot, type, pngPath, plistPath, cb);
                });
        });
}

} // namespace paimon::globalicon
