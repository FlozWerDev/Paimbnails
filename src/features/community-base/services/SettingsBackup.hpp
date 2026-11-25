#pragma once

// snapshot of every paimbnails setting for the paimon base cloud copy:
// mod.json settings, saved values and the per-feature json files in the save
// folder. credentials, server urls, machine paths and caches never leave the pc.
//
// a restore is staged to disk and applied on the next launch, before features
// load their config, so nothing running now can overwrite it on exit.

#include <Geode/Geode.hpp>
#include <matjson.hpp>

namespace paimon::community::settings_backup {

inline constexpr int kSchema = 1;

struct Summary {
    size_t settings = 0;
    size_t saved = 0;
    size_t files = 0;
};

// keys that must not be uploaded or restored.
bool isPrivateKey(std::string_view key);

geode::Result<matjson::Value> capture();
Summary summarize(matjson::Value const& payload);
geode::Result<> validate(matjson::Value const& payload);

// writes the payload as pending and keeps the current settings as a local backup.
geode::Result<> stageRestore(matjson::Value const& payload);
bool hasLocalBackup();
// stages the local backup taken before the last restore.
geode::Result<> stageLocalBackup();
bool hasPendingRestore();

// called once at startup; applies a staged restore if there is one.
void applyPendingRestore();

} // namespace paimon::community::settings_backup
