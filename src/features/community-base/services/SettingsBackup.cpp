#include "SettingsBackup.hpp"

#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/FilesystemPath.hpp"

#include <Geode/loader/Setting.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>

using namespace geode::prelude;

namespace paimon::community::settings_backup {
namespace {

constexpr size_t kMaxValueBytes = 256 * 1024;
constexpr size_t kMaxPayloadBytes = 1400 * 1024;
constexpr size_t kMaxFileBytes = 256 * 1024;
constexpr char const* kFormat = "paimbnails-settings";

// per-feature configuration files kept in the save folder.
constexpr std::array<char const*, 12> kFeatureFiles = {
    "rtx_config.json", "cursor_config.json", "custom-slider.json", "frame_interp.json",
    "info_suite.json", "info_search_presets.json", "main_menu_layout.json",
    "main_menu_layout_presets.json", "pet_config.json", "progressbar_config.json",
    "profile_pic_config.json", "transitions.json",
};

std::filesystem::path saveDir() { return paimon::paths::toStdPath(Mod::get()->getSaveDir()); }
std::filesystem::path stateDir() { return saveDir() / "community-base"; }
std::filesystem::path pendingPath() { return stateDir() / "pending-restore.json"; }
std::filesystem::path localBackupPath() { return stateDir() / "before-restore.json"; }

bool isFeatureFile(std::string_view name) {
    return std::any_of(kFeatureFiles.begin(), kFeatureFiles.end(), [name](char const* file) { return name == file; });
}

bool fileExists(std::filesystem::path const& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

// titles, buttons, info rows and file pickers carry no portable value.
bool portableSetting(std::shared_ptr<Setting> const& setting) {
    using geode::cast::typeinfo_pointer_cast;
    return setting && !typeinfo_pointer_cast<TitleSetting>(setting) && !typeinfo_pointer_cast<ButtonSetting>(setting)
        && !typeinfo_pointer_cast<InfoSetting>(setting) && !typeinfo_pointer_cast<FileSetting>(setting);
}

// load() takes the stored value as-is; going through setValue afterwards runs the
// setting's own validation (ranges, one-of lists) and falls back to the default.
bool restoreSetting(std::shared_ptr<Setting> const& setting, matjson::Value const& value) {
    using geode::cast::typeinfo_pointer_cast;
    if (!setting->load(value)) return false;
    if (auto s = typeinfo_pointer_cast<IntSetting>(setting)) s->setValue(s->getValue());
    else if (auto s = typeinfo_pointer_cast<FloatSetting>(setting)) s->setValue(s->getValue());
    else if (auto s = typeinfo_pointer_cast<StringSetting>(setting)) s->setValue(s->getValue());
    else if (auto s = typeinfo_pointer_cast<BoolSetting>(setting)) s->setValue(s->getValue());
    return true;
}

geode::Result<matjson::Value> readJsonFile(std::filesystem::path const& path, size_t limit) {
    auto bytes = paimon::file::readBytes(path, limit);
    if (bytes.empty()) return Err("empty or unreadable");
    auto parsed = matjson::parse(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size()));
    if (!parsed) return Err("invalid json");
    return Ok(parsed.unwrap());
}

} // namespace

bool isPrivateKey(std::string_view key) {
    auto lower = geode::utils::string::toLower(std::string(key));
    for (auto needle : {"token", "secret", "password", "credential", "cookie", "session", "path", "folder", "scan-dir"}) {
        if (lower.find(needle) != std::string::npos) return true;
    }
    if (lower.ends_with("-url") || lower.ends_with("_url")) return true;
    for (auto prefix : {"is-verified-", "msgnotif-latest-", "web-requests-verification-", "spotify-",
                        "global-icon-", "community-base-", "factory-reset"}) {
        if (lower.starts_with(prefix)) return true;
    }
    for (auto exact : {"mod-code", "mentions-seen", "forum-cache"}) {
        if (lower == exact) return true;
    }
    return false;
}

geode::Result<matjson::Value> capture() {
    auto* mod = Mod::get();
    auto settings = matjson::Value::object();
    for (auto const& key : mod->getSettingKeys()) {
        if (isPrivateKey(key)) continue;
        auto setting = mod->getSetting(key);
        if (!portableSetting(setting)) continue;
        matjson::Value value;
        if (setting->save(value)) settings[key] = value;
    }

    size_t total = settings.dump(matjson::NO_INDENTATION).size();
    auto saved = matjson::Value::object();
    for (auto const& entry : mod->getSaveContainer()) {
        auto key = entry.getKey();
        if (!key || isPrivateKey(*key)) continue;
        // big blobs are caches, not choices the player made.
        auto size = entry.dump(matjson::NO_INDENTATION).size();
        if (size > kMaxValueBytes) {
            log::info("[PaimonBase] Skipping saved value '{}' ({} bytes) from the cloud copy", *key, size);
            continue;
        }
        saved[*key] = entry;
        total += size + key->size();
    }

    auto files = matjson::Value::object();
    for (auto const* name : kFeatureFiles) {
        auto path = saveDir() / name;
        if (!fileExists(path)) continue;
        auto json = readJsonFile(path, kMaxFileBytes);
        if (!json) {
            log::warn("[PaimonBase] Skipping {}: {}", name, json.unwrapErr());
            continue;
        }
        total += json.unwrap().dump(matjson::NO_INDENTATION).size();
        files[name] = json.unwrap();
    }

    if (total > kMaxPayloadBytes) {
        return Err(fmt::format("Tus ajustes ocupan {} KB y el limite es {} KB / Your settings take {} KB, the limit is {} KB",
            total / 1024, kMaxPayloadBytes / 1024, total / 1024, kMaxPayloadBytes / 1024));
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return Ok(matjson::makeObject({
        {"format", kFormat},
        {"schema", kSchema},
        {"modVersion", mod->getVersion().toVString()},
        {"capturedAt", static_cast<int64_t>(now)},
        {"settings", settings},
        {"saved", saved},
        {"files", files},
    }));
}

Summary summarize(matjson::Value const& payload) {
    return {payload["settings"].size(), payload["saved"].size(), payload["files"].size()};
}

geode::Result<> validate(matjson::Value const& payload) {
    if (!payload.isObject() || payload["format"].asString().unwrapOr("") != kFormat) {
        return Err("La copia no es de Paimbnails / Not a Paimbnails settings copy");
    }
    if (payload["schema"].asInt().unwrapOr(0) != kSchema) {
        return Err("Version de copia no compatible / Unsupported copy version");
    }
    if (!payload["settings"].isObject() || !payload["saved"].isObject() || !payload["files"].isObject()) {
        return Err("Copia incompleta / Incomplete copy");
    }
    for (auto const& entry : payload["files"]) {
        auto name = entry.getKey();
        if (!name || !isFeatureFile(*name)) return Err("La copia trae un archivo desconocido / Copy contains an unknown file");
    }
    return Ok();
}

geode::Result<> stageRestore(matjson::Value const& payload) {
    GEODE_UNWRAP(validate(payload));
    auto current = capture();
    if (current && !paimon::file::writeAtomically(localBackupPath(), current.unwrap().dump(matjson::NO_INDENTATION))) {
        return Err("No se pudo guardar el respaldo local / Could not write the local backup");
    }
    if (!paimon::file::writeAtomically(pendingPath(), payload.dump(matjson::NO_INDENTATION))) {
        return Err("No se pudo preparar la restauracion / Could not stage the restore");
    }
    return Ok();
}

bool hasLocalBackup() { return fileExists(localBackupPath()); }
bool hasPendingRestore() { return fileExists(pendingPath()); }

geode::Result<> stageLocalBackup() {
    auto json = readJsonFile(localBackupPath(), 4 * 1024 * 1024);
    if (!json) return Err("No hay respaldo local legible / No readable local backup");
    GEODE_UNWRAP(validate(json.unwrap()));
    if (!paimon::file::writeAtomically(pendingPath(), json.unwrap().dump(matjson::NO_INDENTATION))) {
        return Err("No se pudo preparar la restauracion / Could not stage the restore");
    }
    return Ok();
}

void applyPendingRestore() {
    auto path = pendingPath();
    if (!fileExists(path)) return;
    auto json = readJsonFile(path, 4 * 1024 * 1024);
    // drop the staged file first: a restore that crashes must not loop on every launch.
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (!json || !validate(json.unwrap())) {
        log::warn("[PaimonBase] Ignoring an invalid staged settings restore");
        return;
    }
    auto const& payload = json.unwrap();
    auto* mod = Mod::get();
    size_t settings = 0, saved = 0, files = 0;
    for (auto const& entry : payload["settings"]) {
        auto key = entry.getKey();
        if (!key || isPrivateKey(*key) || !mod->hasSetting(*key)) continue;
        auto setting = mod->getSetting(*key);
        if (portableSetting(setting) && restoreSetting(setting, entry)) settings++;
    }
    auto& container = mod->getSaveContainer();
    for (auto const& entry : payload["saved"]) {
        auto key = entry.getKey();
        if (!key || isPrivateKey(*key)) continue;
        container[*key] = entry;
        saved++;
    }
    for (auto const& entry : payload["files"]) {
        auto name = entry.getKey();
        if (!name || !isFeatureFile(*name)) continue;
        if (paimon::file::writeAtomically(saveDir() / *name, entry.dump())) files++;
    }
    auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    container["community-base-restored-at"] = static_cast<int64_t>(now);
    if (auto result = mod->saveData(); !result) {
        log::warn("[PaimonBase] Restored settings but saving failed: {}", result.unwrapErr());
    }
    log::info("[PaimonBase] Restored {} settings, {} saved values and {} files", settings, saved, files);
}

} // namespace paimon::community::settings_backup

$execute {
    paimon::community::settings_backup::applyPendingRestore();
}
