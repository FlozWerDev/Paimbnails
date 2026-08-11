#include "GlobalIconStorage.hpp"
#include "GlobalIconClient.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../utils/Debug.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
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

    bool fileExists(std::filesystem::path const& path) {
        if (path.empty()) return false;
        std::error_code ec;
        return std::filesystem::exists(path, ec) && !ec;
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
    return "globalicon-" + std::to_string(accountID) + "-" + slot.type + "-" + slot.name;
}

std::filesystem::path GlobalIconStorage::cacheDir(int accountID, std::string const& type) const {
    return Mod::get()->getSaveDir() / "global_icons_cache" / std::to_string(accountID) / type;
}

GlobalIconStorage::SlotFiles GlobalIconStorage::resolveFiles(int accountID, GlobalIconSlot const& slot) const {
    auto dir = cacheDir(accountID, slot.type);

    SlotFiles files;
    files.png = dir / (slot.pngFile.empty() ? (slot.name + ".png") : slot.pngFile);
    files.needPlist = !slot.plistUrl.empty();
    if (files.needPlist) {
        files.plist = dir / (slot.plistFile.empty() ? (slot.name + ".plist") : slot.plistFile);
    }
    files.pngMissing = !fileExists(files.png);
    files.plistMissing = files.needPlist && !fileExists(files.plist);
    return files;
}

void GlobalIconStorage::pruneSlotDir(std::filesystem::path const& dir, SlotFiles const& files) const {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) return;
        if (!entry.is_regular_file(ec) || ec) continue;
        auto const& path = entry.path();
        if (path == files.png || (files.needPlist && path == files.plist)) continue;
        std::error_code rmEc;
        std::filesystem::remove(path, rmEc);
    }
}

bool GlobalIconStorage::registerWithMoreIcons(std::string const& regName, GlobalIconSlot const& slot,
                                              IconType type, std::filesystem::path const& png,
                                              std::filesystem::path const& plist, bool wrapRefresh) {
    // Wrap external icon edits in pre/refresh (API requirement).
    if (wrapRefresh) more_icons::preRefreshIcons();
    auto* info = more_icons::addIcon(
        regName, slot.name, type, png, plist,
        qualityFromInt(slot.quality),
        slot.packID.empty() ? "paimon.global-icon" : slot.packID,
        slot.packName.empty() ? "Global Icon" : slot.packName);
    if (wrapRefresh) more_icons::refreshIcons();

    if (info) {
        m_registered.insert(regName);
        PaimonDebug::log("[GlobalIcon] Registered icon '{}' (type {})", regName, static_cast<int>(type));
        return true;
    }
    PaimonDebug::warn("[GlobalIcon] addIcon failed for '{}'", regName);
    return false;
}

bool GlobalIconStorage::isReady(int accountID, GlobalIconSlot const& slot) const {
    if (!available()) return false;
    auto typeOpt = iconTypeFromString(slot.type);
    if (!typeOpt || slot.name.empty()) return false;
    auto regName = registeredName(accountID, slot);
    return m_registered.count(regName) && more_icons::getIcon(regName, *typeOpt) != nullptr;
}

void GlobalIconStorage::fetchSlot(GlobalIconSlot const& slot, SlotFiles const& files, FetchCallback cb) {
    if (!files.pngMissing && !files.plistMissing) {
        if (cb) cb(true);
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(files.png.parent_path(), ec);

    auto fetchPlist = [slot, files](FetchCallback inner) {
        if (!files.plistMissing) {
            if (inner) inner(true);
            return;
        }
        GlobalIconClient::get().downloadFile(slot.plistUrl,
            [files, inner = std::move(inner)](bool ok, std::vector<uint8_t> const& data) {
                bool good = ok && !data.empty() && writeFile(files.plist, data);
                if (inner) inner(good);
            });
    };

    if (!files.pngMissing) {
        fetchPlist(std::move(cb));
        return;
    }

    GlobalIconClient::get().downloadFile(slot.pngUrl,
        [files, cb = std::move(cb), fetchPlist](bool ok, std::vector<uint8_t> const& data) mutable {
            if (!ok || data.empty() || !writeFile(files.png, data)) {
                if (cb) cb(false);
                return;
            }
            fetchPlist(std::move(cb));
        });
}

void GlobalIconStorage::ensureIcon(int accountID, GlobalIconSlot const& slot, EnsureCallback cb) {
    if (!available()) {
        if (cb) cb(false, "");
        return;
    }
    auto typeOpt = iconTypeFromString(slot.type);
    if (!typeOpt || slot.name.empty() || slot.pngUrl.empty()) {
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

    // A download for this exact icon is already running.
    auto pendingIt = m_pending.find(regName);
    if (pendingIt != m_pending.end()) {
        if (cb) pendingIt->second.push_back(std::move(cb));
        return;
    }

    auto files = resolveFiles(accountID, slot);
    if (!files.pngMissing && !files.plistMissing) {
        bool ok = registerWithMoreIcons(regName, slot, type, files.png, files.plist, true);
        if (cb) cb(ok, ok ? regName : "");
        return;
    }

    auto& waiters = m_pending[regName];
    if (cb) waiters.push_back(std::move(cb));

    fetchSlot(slot, files, [accountID, slot, files, type, regName](bool ok) {
        auto& self = GlobalIconStorage::get();
        bool registered = ok && self.registerWithMoreIcons(regName, slot, type, files.png, files.plist, true);
        if (registered) self.pruneSlotDir(files.png.parent_path(), files);

        auto node = self.m_pending.extract(regName);
        if (node.empty()) return;
        for (auto& waiter : node.mapped()) {
            if (waiter) waiter(registered, registered ? regName : "");
        }
    });
}

void GlobalIconStorage::ensureIcons(int accountID, std::vector<GlobalIconSlot> const& slots, EnsureManyCallback cb) {
    int total = static_cast<int>(slots.size());
    if (!available() || total == 0) {
        if (cb) cb(0, total);
        return;
    }

    struct Job {
        GlobalIconSlot slot;
        SlotFiles files;
        IconType type;
        std::string regName;
        bool fetched = false;
    };

    struct State {
        std::vector<Job> jobs;
        int remaining = 0;
        int total = 0;
        EnsureManyCallback cb;
    };

    auto state = std::make_shared<State>();
    state->total = total;
    state->cb = std::move(cb);

    for (auto const& slot : slots) {
        auto typeOpt = iconTypeFromString(slot.type);
        if (!typeOpt || slot.name.empty() || slot.pngUrl.empty()) continue;
        Job job;
        job.slot = slot;
        job.type = *typeOpt;
        job.regName = registeredName(accountID, slot);
        job.files = resolveFiles(accountID, slot);
        state->jobs.push_back(std::move(job));
    }

    state->remaining = static_cast<int>(state->jobs.size());
    if (state->remaining == 0) {
        if (state->cb) state->cb(0, total);
        return;
    }

    // One refresh for the whole batch, once every download has settled.
    auto finish = [state]() {
        auto& self = GlobalIconStorage::get();
        int succeeded = 0;

        more_icons::preRefreshIcons();
        for (auto const& job : state->jobs) {
            if (!job.fetched) continue;
            if (self.m_registered.count(job.regName) &&
                more_icons::getIcon(job.regName, job.type) != nullptr) {
                succeeded++;
                continue;
            }
            if (self.registerWithMoreIcons(job.regName, job.slot, job.type,
                                           job.files.png, job.files.plist, false)) {
                succeeded++;
            }
        }
        more_icons::refreshIcons();

        for (auto const& job : state->jobs) {
            if (job.fetched) self.pruneSlotDir(job.files.png.parent_path(), job.files);
        }

        if (state->cb) state->cb(succeeded, state->total);
    };

    for (size_t i = 0; i < state->jobs.size(); i++) {
        auto& job = state->jobs[i];
        fetchSlot(job.slot, job.files, [state, i, finish](bool ok) {
            state->jobs[i].fetched = ok;
            if (--state->remaining == 0) finish();
        });
    }
}

void GlobalIconStorage::pruneCache() {
    auto root = Mod::get()->getSaveDir() / "global_icons_cache";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) return;

    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type when;
    };
    std::vector<Entry> accounts;

    for (auto const& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) return;
        if (!entry.is_directory(ec) || ec) continue;
        std::error_code timeEc;
        auto when = std::filesystem::last_write_time(entry.path(), timeEc);
        if (timeEc) continue;
        accounts.push_back({entry.path(), when});
    }

    if (accounts.size() <= kMaxCachedAccounts) return;

    std::sort(accounts.begin(), accounts.end(),
              [](Entry const& a, Entry const& b) { return a.when > b.when; });

    for (size_t i = kMaxCachedAccounts; i < accounts.size(); i++) {
        std::error_code rmEc;
        std::filesystem::remove_all(accounts[i].path, rmEc);
    }
    PaimonDebug::log("[GlobalIcon] Pruned {} cached account(s)", accounts.size() - kMaxCachedAccounts);
}

} // namespace paimon::globalicon
