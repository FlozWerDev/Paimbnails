#pragma once

// Downloads/caches synced icon assets and registers them in More Icons.

#include <Geode/Geode.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <vector>

namespace paimon::globalicon {

class GlobalIconStorage {
public:
    static GlobalIconStorage& get();

// Whether More Icons is available.
    static bool available();

// Namespaced More Icons registration name.
    static std::string registeredName(int accountID, GlobalIconSlot const& slot);

// Ensure one icon is cached and registered; callback returns its registered name.
    using EnsureCallback = geode::CopyableFunction<void(bool success, std::string const& iconName)>;
    void ensureIcon(int accountID, GlobalIconSlot const& slot, EnsureCallback cb);

// Batch registration uses one More Icons refresh; callback returns done/total.
    using EnsureManyCallback = geode::CopyableFunction<void(int succeeded, int total)>;
    void ensureIcons(int accountID, std::vector<GlobalIconSlot> const& slots, EnsureManyCallback cb);

// Whether this slot is registered and usable.
    bool isReady(int accountID, GlobalIconSlot const& slot) const;

// Delete cache directories beyond the newest kMaxCachedAccounts accounts.
    void pruneCache();

private:
    GlobalIconStorage() = default;

// Accounts kept on disk between sessions.
    static constexpr size_t kMaxCachedAccounts = 40;

    std::filesystem::path cacheDir(int accountID, std::string const& type) const;

// Resolved paths and missing files for a slot.
    struct SlotFiles {
        std::filesystem::path png;
        std::filesystem::path plist;
        bool needPlist = false;
        bool pngMissing = false;
        bool plistMissing = false;
    };
    SlotFiles resolveFiles(int accountID, GlobalIconSlot const& slot) const;

// Fetch missing files, then invoke cb(ok).
    using FetchCallback = geode::CopyableFunction<void(bool ok)>;
    void fetchSlot(GlobalIconSlot const& slot, SlotFiles const& files, FetchCallback cb);

// Register in More Icons; wrapRefresh controls ownership of the refresh pair.
    bool registerWithMoreIcons(std::string const& regName, GlobalIconSlot const& slot,
                               IconType type, std::filesystem::path const& png,
                               std::filesystem::path const& plist, bool wrapRefresh);

// Remove content-addressed files no longer referenced by current metadata.
    void pruneSlotDir(std::filesystem::path const& dir, SlotFiles const& files) const;

// Icons already registered this session.
    std::set<std::string> m_registered;
// In-flight downloads keyed by registered name.
    std::unordered_map<std::string, std::vector<EnsureCallback>> m_pending;
};

}
