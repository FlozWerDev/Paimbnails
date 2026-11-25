#include "GlobalIconService.hpp"
#include "GlobalIconClient.hpp"
#include "../GlobalIconTypes.hpp"
#include "../../community-base/services/CommunityClient.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/Debug.hpp"
#include "../../../utils/FormatDetect.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"

#include <Geode/binding/GameManager.hpp>
#include <matjson.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>

#include "../../../framework/compat/MoreIconsAPI.hpp"

using namespace geode::prelude;

namespace paimon::globalicon {

namespace {
constexpr uint64_t kMaxPngBytes = 4 * 1024 * 1024;   // server limit per texture
constexpr uint64_t kMaxPlistBytes = 1024 * 1024;     // server limit per sheet
constexpr size_t kMaxGradientImages = 32;
constexpr std::array<char const*, 9> kGradientTypes = {
    "cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing", "jetpack",
};

using community::tx;

bool isPng(std::vector<uint8_t> const& data) {
    return paimon::format::isPng(data.data(), data.size());
}

bool isGradientKey(std::string const& key) {
    constexpr std::string_view prefix = "icon-gradients-";
    if (!key.starts_with(prefix)) return false;
    std::string_view rest(key);
    rest.remove_prefix(prefix.size());
    if (rest.ends_with("-p2")) rest.remove_suffix(3);
    return std::any_of(kGradientTypes.begin(), kGradientTypes.end(), [rest](char const* type) { return rest == type; });
}

struct CustomSlot {
    std::string type;
    std::string name;
    std::string packID;
    std::string packName;
    int quality = 3;
    int specialID = 0;
    int fireCount = 0;
    std::vector<uint8_t> png;
    std::vector<uint8_t> plist;
    std::string pngSha;
    std::string plistSha;
};

struct PendingUpload {
    std::string kind;
    std::vector<uint8_t> const* data;
    std::string* sha;
};

struct PublishJob {
    LinkOptions options;
    matjson::Value vanilla;
    matjson::Value gradients;
    std::vector<std::string> imagePaths;
    std::vector<std::vector<uint8_t>> imageData;
    std::vector<std::string> imageSha;
    std::vector<CustomSlot> slots;
    std::vector<PendingUpload> uploads;
    size_t next = 0;
    GlobalIconService::ResultCallback cb;
};

matjson::Value captureVanilla() {
    auto* gm = GameManager::get();
    auto atLeast = [](int value, int low) { return std::max(value, low); };
    return matjson::makeObject({
        {"cube", atLeast(gm->getPlayerFrame(), 1)},
        {"ship", atLeast(gm->getPlayerShip(), 1)},
        {"ball", atLeast(gm->getPlayerBall(), 1)},
        {"ufo", atLeast(gm->getPlayerBird(), 1)},
        {"wave", atLeast(gm->getPlayerDart(), 1)},
        {"robot", atLeast(gm->getPlayerRobot(), 1)},
        {"spider", atLeast(gm->getPlayerSpider(), 1)},
        {"swing", atLeast(gm->getPlayerSwing(), 1)},
        {"jetpack", atLeast(gm->getPlayerJetpack(), 1)},
        {"trail", atLeast(gm->getPlayerStreak(), 0)},
        {"deathEffect", atLeast(gm->getPlayerDeathEffect(), 0)},
        {"color1", atLeast(gm->getPlayerColor(), 0)},
        {"color2", atLeast(gm->getPlayerColor2(), 0)},
        {"glowColor", gm->getPlayerGlow() ? atLeast(gm->getPlayerGlowColor(), 0) : -1},
        {"glow", gm->getPlayerGlow()},
    });
}

// copies the player's gradient slots and queues the images their points use.
// images that are not png or cannot be read are dropped; the point keeps its color.
matjson::Value captureGradients(PublishJob& job) {
    auto doc = matjson::Value::object();
    std::unordered_map<std::string, size_t> imageIndex;
    for (auto const& entry : Mod::get()->getSaveContainer()) {
        auto key = entry.getKey();
        if (!key || !isGradientKey(*key) || !entry.isObject()) continue;
        auto colors = matjson::Value::object();
        for (int color = 1; color <= 5; ++color) {
            auto colorKey = "color" + std::to_string(color);
            auto const& config = entry[colorKey];
            if (!config.isObject() || !config["points"].isArray()) continue;
            auto points = matjson::Value::array();
            for (auto const& point : config["points"]) {
                auto clean = matjson::makeObject({
                    {"pos", matjson::makeObject({{"x", paimon::json::doubleOr(point["pos"]["x"])},
                                                 {"y", paimon::json::doubleOr(point["pos"]["y"])}})},
                    {"color", matjson::makeObject({{"r", paimon::json::integerOr<int>(point["color"]["r"])},
                                                   {"g", paimon::json::integerOr<int>(point["color"]["g"])},
                                                   {"b", paimon::json::integerOr<int>(point["color"]["b"])}})},
                });
                auto image = paimon::json::stringOr(point["image"]);
                if (!image.empty()) {
                    auto found = imageIndex.find(image);
                    if (found == imageIndex.end() && job.imagePaths.size() < kMaxGradientImages) {
                        auto bytes = paimon::file::readBytes(paimon::assets::pathFromUtf8(image), kMaxPngBytes);
                        if (isPng(bytes)) {
                            found = imageIndex.emplace(image, job.imagePaths.size()).first;
                            job.imagePaths.push_back(image);
                            job.imageData.push_back(std::move(bytes));
                        } else {
                            PaimonDebug::warn("[GlobalIcon] Gradient image is not a readable PNG; sharing its color only");
                        }
                    }
                    if (found != imageIndex.end()) clean["image"] = "path:" + std::to_string(found->second);
                }
                points.push(clean);
                if (points.size() >= 24) break;
            }
            colors[colorKey] = matjson::makeObject({{"points", points}, {"linear", config["linear"].asBool().unwrapOr(true)}});
        }
        if (colors.size() > 0) doc[*key] = colors;
    }
    return doc;
}

std::vector<CustomSlot> captureCustom() {
    std::vector<CustomSlot> slots;
    if (!paimon::compat::ModCompat::isMoreIconsLoaded()) return slots;
    for (IconType type : syncableIconTypes()) {
        auto* info = more_icons::activeIcon(type);
        if (!info || info->isVanilla() || info->isZipped()) continue;
        auto png = paimon::file::readBytes(info->getTexture(), kMaxPngBytes);
        if (!isPng(png)) continue;
        CustomSlot slot;
        slot.type = std::string(iconTypeToString(type));
        // getName() can carry the pack prefix ("pack:name"); the short name is a clean label.
        slot.name = info->getShortName().empty() ? info->getName() : info->getShortName();
        for (auto& c : slot.name) {
            if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) c = '_';
        }
        slot.packID = info->getPackID();
        slot.packName = info->getPackName();
        slot.quality = std::clamp(static_cast<int>(info->getQuality()), 1, 3);
        slot.specialID = std::max(0, static_cast<int>(info->getSpecialID()));
        slot.fireCount = std::max(0, static_cast<int>(info->getFireCount()));
        slot.png = std::move(png);
        if (auto sheet = info->getSheet(); !sheet.empty()) slot.plist = paimon::file::readBytes(sheet, kMaxPlistBytes);
        slots.push_back(std::move(slot));
    }
    return slots;
}

// "path:<n>" placeholders become "asset:<sha>" once every image is uploaded.
// contains() first: the non-const operator[] would insert "image": null into every point.
void resolveImages(matjson::Value& gradients, std::vector<std::string> const& shas) {
    for (auto& container : gradients) {
        for (auto& config : container) {
            if (!config.contains("points")) continue;
            for (auto& point : config["points"]) {
                if (!point.contains("image")) continue;
                auto image = paimon::json::stringOr(point["image"]);
                std::optional<size_t> index;
                if (image.starts_with("path:")) {
                    if (auto parsed = geode::utils::numFromString<size_t>(image.substr(5))) index = parsed.unwrap();
                }
                if (index && *index < shas.size() && !shas[*index].empty()) {
                    point["image"] = "asset:" + shas[*index];
                } else {
                    point.erase("image");
                }
            }
        }
    }
}

void finishPublish(std::shared_ptr<PublishJob> job) {
    auto body = matjson::makeObject({
        {"share", matjson::makeObject({
            {"vanilla", job->options.vanilla}, {"gradients", job->options.gradients}, {"custom", job->options.custom}})},
    });
    if (job->options.vanilla) body["vanilla"] = job->vanilla;
    if (job->options.gradients) {
        resolveImages(job->gradients, job->imageSha);
        body["gradients"] = job->gradients;
    }
    if (job->options.custom) {
        auto custom = matjson::Value::array();
        for (auto const& slot : job->slots) {
            custom.push(matjson::makeObject({
                {"type", slot.type}, {"name", slot.name}, {"packID", slot.packID}, {"packName", slot.packName},
                {"quality", slot.quality}, {"specialID", slot.specialID}, {"fireCount", slot.fireCount},
                {"png", slot.pngSha}, {"plist", slot.plistSha},
            }));
        }
        body["custom"] = custom;
    }
    community::CommunityClient::get().publishIcons(std::move(body),
        [job](bool ok, matjson::Value const&, std::string const& error) {
            if (auto identity = community::currentIdentity()) GlobalIconClient::get().invalidate(identity->accountID);
            if (ok) {
                GlobalIconService::setEnabledLocally(true);
                GlobalIconService::saveOptions(job->options);
            }
            if (job->cb) job->cb(ok, error);
        });
}

void uploadNext(std::shared_ptr<PublishJob> job) {
    if (job->next >= job->uploads.size()) {
        finishPublish(job);
        return;
    }
    auto const& upload = job->uploads[job->next];
    community::CommunityClient::get().uploadAsset(upload.kind, *upload.data,
        [job](bool ok, matjson::Value const& json, std::string const& error) {
            if (!ok) {
                if (job->cb) job->cb(false, error);
                return;
            }
            *job->uploads[job->next].sha = paimon::json::stringOr(json["sha256"]);
            job->next++;
            uploadNext(job);
        });
}

} // namespace

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

LinkOptions GlobalIconService::savedOptions() {
    auto* mod = Mod::get();
    return {mod->getSavedValue<bool>("global-icon-share-vanilla", true),
            mod->getSavedValue<bool>("global-icon-share-gradients", false),
            mod->getSavedValue<bool>("global-icon-share-custom", false)};
}

void GlobalIconService::saveOptions(LinkOptions options) {
    auto* mod = Mod::get();
    mod->setSavedValue<bool>("global-icon-share-vanilla", options.vanilla);
    mod->setSavedValue<bool>("global-icon-share-gradients", options.gradients);
    mod->setSavedValue<bool>("global-icon-share-custom", options.custom);
}

bool GlobalIconService::hasCustomIcons() {
    if (!paimon::compat::ModCompat::isMoreIconsLoaded()) return false;
    for (IconType type : syncableIconTypes()) {
        auto* info = more_icons::activeIcon(type);
        if (info && !info->isVanilla() && !info->isZipped()) return true;
    }
    return false;
}

bool GlobalIconService::hasGradients() {
    for (auto const& entry : Mod::get()->getSaveContainer()) {
        auto key = entry.getKey();
        if (key && isGradientKey(*key) && entry.isObject() && entry.size() > 0) return true;
    }
    return false;
}

void GlobalIconService::publish(LinkOptions options, ResultCallback cb) {
    if (m_busy) {
        if (cb) cb(false, tx("Ya se esta vinculando tu icono.", "Your icon is already being linked."));
        return;
    }
    if (!community::currentIdentity()) {
        if (cb) cb(false, tx("Inicia sesion en Geometry Dash.", "Log in to Geometry Dash."));
        return;
    }
    auto job = std::make_shared<PublishJob>();
    job->options = options;
    if (options.vanilla) job->vanilla = captureVanilla();
    if (options.gradients) {
        job->gradients = captureGradients(*job);
        if (job->gradients.size() == 0) job->options.gradients = false;
    }
    if (options.custom) {
        job->slots = captureCustom();
        if (job->slots.empty()) job->options.custom = false;
    }
    if (!job->options.any()) {
        if (cb) cb(false, options.custom && !options.gradients && !options.vanilla
            ? tx("No usas iconos custom de More Icons ahora mismo.", "You are not using custom More Icons icons right now.")
            : tx("No hay nada que vincular.", "There is nothing to link."));
        return;
    }
    job->imageSha.resize(job->imageData.size());
    for (size_t i = 0; i < job->imageData.size(); ++i) {
        job->uploads.push_back({"png", &job->imageData[i], &job->imageSha[i]});
    }
    for (auto& slot : job->slots) {
        job->uploads.push_back({"png", &slot.png, &slot.pngSha});
        if (!slot.plist.empty()) job->uploads.push_back({"plist", &slot.plist, &slot.plistSha});
    }
    PaimonDebug::log("[GlobalIcon] Linking vanilla={} gradients={} custom={} ({} files)",
        job->options.vanilla, job->options.gradients, job->options.custom, job->uploads.size());
    m_busy = true;
    job->cb = [cb = std::move(cb)](bool ok, std::string const& message) {
        GlobalIconService::get().m_busy = false;
        if (cb) cb(ok, message);
    };
    uploadNext(job);
}

void GlobalIconService::clearIcons(ResultCallback cb) {
    community::CommunityClient::get().clearIcons([cb = std::move(cb)](bool ok, matjson::Value const&, std::string const& error) {
        if (auto identity = community::currentIdentity()) GlobalIconClient::get().invalidate(identity->accountID);
        if (ok) GlobalIconService::setEnabledLocally(false);
        if (cb) cb(ok, error);
    });
}

} // namespace paimon::globalicon
