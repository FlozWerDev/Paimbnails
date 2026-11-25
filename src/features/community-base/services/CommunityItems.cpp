#include "CommunityItems.hpp"

#include "../../global-icon/GlobalIconGradients.hpp"
#include "../../global-icon/services/GlobalIconClient.hpp"
#include "../../global-icon/services/GlobalIconStorage.hpp"
#include "../../icon-gradients/GradientUtils.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/Base64.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/LocalAssetStore.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <unordered_map>

#include "../../../framework/compat/MoreIconsAPI.hpp"

using namespace geode::prelude;

namespace paimon::community::items {

namespace {
constexpr uint64_t kMaxPngBytes = 4 * 1024 * 1024;
constexpr uint64_t kMaxPlistBytes = 1024 * 1024;
constexpr size_t kMaxGradientImages = 32;

bool isPng(std::vector<uint8_t> const& data) {
    static constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    return data.size() > sizeof(signature) && std::equal(std::begin(signature), std::end(signature), data.begin());
}

// names travel as labels and file stems: no path separators or reserved characters.
std::string safeName(std::string name) {
    for (auto& c : name) {
        if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 32) c = '_';
    }
    auto first = name.find_first_not_of(" ._");
    if (first == std::string::npos) return "icon";
    name = name.substr(first);
    if (name.size() > 60) name.resize(60);
    return name;
}

struct Upload {
    std::string kind;
    std::vector<uint8_t> data;
    std::string sha;
};
using Uploads = std::shared_ptr<std::vector<Upload>>;

// one file at a time; the server answers with its sha-256.
void uploadAll(Uploads uploads, size_t index, std::function<void(bool, std::string const&)> done) {
    if (index >= uploads->size()) {
        done(true, "");
        return;
    }
    auto& upload = (*uploads)[index];
    CommunityClient::get().uploadAsset(upload.kind, upload.data,
        [uploads, index, done](bool ok, matjson::Value const& json, std::string const& error) {
            if (!ok) { done(false, error); return; }
            (*uploads)[index].sha = json["sha256"].asString().unwrapOr("");
            uploadAll(uploads, index + 1, done);
        });
}

JsonCallback submitCallback(Done done) {
    return [done](bool ok, matjson::Value const& json, std::string const& error) {
        if (!ok) { done(false, error); return; }
        bool approved = json["status"].asString().unwrapOr("") == "approved";
        done(true, approved
            ? tx("Publicado en Paimon Shop.", "Published on Paimon Shop.")
            : tx("Enviado. Saldra en Paimon Shop cuando un moderador lo apruebe.",
                 "Sent. It shows up in Paimon Shop once a moderator approves it."));
    };
}

std::string_view moreIconsFolder(IconType type) {
    switch (type) {
        case IconType::Cube: return "icon";
        case IconType::Ship: return "ship";
        case IconType::Ball: return "ball";
        case IconType::Ufo: return "ufo";
        case IconType::Wave: return "wave";
        case IconType::Robot: return "robot";
        case IconType::Spider: return "spider";
        case IconType::Swing: return "swing";
        case IconType::Jetpack: return "jetpack";
        default: return "";
    }
}

// more icons picks the texture quality from the file suffix.
std::string_view qualitySuffix(int quality) {
    return quality >= 3 ? "-uhd" : quality == 2 ? "-hd" : "";
}
} // namespace

std::vector<IconType> const& gamemodes() {
    static std::vector<IconType> const types = {
        IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo, IconType::Wave,
        IconType::Robot, IconType::Spider, IconType::Swing, IconType::Jetpack,
    };
    return types;
}

char const* gamemodeName(IconType type) {
    switch (type) {
        case IconType::Cube: return "Cube";
        case IconType::Ship: return "Ship";
        case IconType::Ball: return "Ball";
        case IconType::Ufo: return "UFO";
        case IconType::Wave: return "Wave";
        case IconType::Robot: return "Robot";
        case IconType::Spider: return "Spider";
        case IconType::Swing: return "Swing";
        case IconType::Jetpack: return "Jetpack";
        default: return "Icon";
    }
}

char const* typeName(std::string const& type) {
    if (type == "presets") return "Presets";
    if (type == "gradients") return tx("Gradientes", "Gradients");
    if (type == "icons") return tx("Iconos", "Icons");
    return tx("Fondos", "Backgrounds");
}

bool hasGradient(IconType type) {
    auto key = paimon::icon_gradients::GradientUtils::getConfigKey(type, false);
    auto const& container = Mod::get()->getSaveContainer();
    if (!container.contains(key)) return false;
    auto const& value = container[key];
    if (!value.isObject()) return false;
    for (int color = 1; color <= 5; ++color) {
        auto const& config = value["color" + std::to_string(color)];
        if (config.isObject() && config["points"].isArray() && config["points"].size() > 0) return true;
    }
    return false;
}

bool hasCustomIcon(IconType type) {
    if (!paimon::compat::ModCompat::isMoreIconsLoaded()) return false;
    auto* info = more_icons::activeIcon(type);
    return info && !info->isVanilla() && !info->isZipped();
}

void publishPreset(shop::Preset const& preset, Done done) {
    if (auto valid = shop::validatePreset(preset); !valid) {
        done(false, valid.unwrapErr());
        return;
    }
    auto body = matjson::makeObject({
        {"category", shop::categoryId(preset.category)},
        {"title", preset.title},
        {"description", preset.description},
        {"configuration", preset.configuration},
        {"preview", preset.preview},
    });
    CommunityClient::get().submitItem("presets", std::move(body), submitCallback(std::move(done)));
}

void submitGradient(IconType type, std::string const& title, std::string const& description, Done done) {
    if (!hasGradient(type)) {
        done(false, tx("No tienes un gradiente guardado para ese modo.", "You have no saved gradient for that gamemode."));
        return;
    }
    auto const& saved = Mod::get()->getSaveContainer()[paimon::icon_gradients::GradientUtils::getConfigKey(type, false)];
    auto uploads = std::make_shared<std::vector<Upload>>();
    std::unordered_map<std::string, size_t> imageIndex;
    // points keep "path:<n>" until file n is uploaded and its sha is known.
    auto gradient = matjson::Value::object();
    for (int color = 1; color <= 5; ++color) {
        auto colorKey = "color" + std::to_string(color);
        auto const& config = saved[colorKey];
        if (!config.isObject() || !config["points"].isArray() || config["points"].size() == 0) continue;
        auto points = matjson::Value::array();
        for (auto const& point : config["points"]) {
            if (points.size() >= 24) break;
            auto clean = matjson::makeObject({
                {"pos", matjson::makeObject({{"x", point["pos"]["x"].asDouble().unwrapOr(0.0)},
                                             {"y", point["pos"]["y"].asDouble().unwrapOr(0.0)}})},
                {"color", matjson::makeObject({{"r", point["color"]["r"].asInt().unwrapOr(0)},
                                               {"g", point["color"]["g"].asInt().unwrapOr(0)},
                                               {"b", point["color"]["b"].asInt().unwrapOr(0)}})},
            });
            auto image = point["image"].asString().unwrapOr("");
            if (!image.empty()) {
                auto found = imageIndex.find(image);
                if (found == imageIndex.end() && uploads->size() < kMaxGradientImages) {
                    auto bytes = paimon::file::readBytes(paimon::assets::pathFromUtf8(image), kMaxPngBytes);
                    if (isPng(bytes)) {
                        found = imageIndex.emplace(image, uploads->size()).first;
                        uploads->push_back({"png", std::move(bytes), ""});
                    }
                }
                if (found != imageIndex.end()) clean["image"] = "path:" + std::to_string(found->second);
            }
            points.push(clean);
        }
        gradient[colorKey] = matjson::makeObject({{"points", points}, {"linear", config["linear"].asBool().unwrapOr(true)}});
    }
    auto iconType = std::string(paimon::globalicon::iconTypeToString(type));
    uploadAll(uploads, 0, [uploads, gradient, iconType, title, description, done](bool ok, std::string const& error) mutable {
        if (!ok) { done(false, error); return; }
        for (auto& config : gradient) {
            for (auto& point : config["points"]) {
                if (!point.contains("image")) continue;
                auto index = geode::utils::numFromString<size_t>(point["image"].asString().unwrapOr("").substr(5));
                if (index && index.unwrap() < uploads->size()) point["image"] = "asset:" + (*uploads)[index.unwrap()].sha;
                else point.erase("image");
            }
        }
        CommunityClient::get().submitItem("gradients", matjson::makeObject({
            {"iconType", iconType}, {"title", title}, {"description", description}, {"gradient", gradient},
        }), submitCallback(done));
    });
}

void submitIcon(IconType type, std::string const& title, std::string const& description, Done done) {
    if (!hasCustomIcon(type)) {
        done(false, tx("No llevas un icono custom de More Icons en ese modo.", "You are not wearing a More Icons icon in that gamemode."));
        return;
    }
    auto* info = more_icons::activeIcon(type);
    auto png = paimon::file::readBytes(info->getTexture(), kMaxPngBytes);
    if (!isPng(png)) {
        done(false, tx("No se pudo leer la textura del icono.", "Could not read the icon texture."));
        return;
    }
    auto uploads = std::make_shared<std::vector<Upload>>();
    uploads->push_back({"png", std::move(png), ""});
    if (!info->getSheet().empty()) {
        auto plist = paimon::file::readBytes(info->getSheet(), kMaxPlistBytes);
        if (!plist.empty()) uploads->push_back({"plist", std::move(plist), ""});
    }
    auto shortName = info->getShortName().empty() ? info->getName() : info->getShortName();
    auto body = matjson::makeObject({
        {"iconType", std::string(paimon::globalicon::iconTypeToString(type))},
        {"title", title},
        {"description", description},
        {"name", safeName(shortName)},
        {"packID", info->getPackID().substr(0, 128)},
        {"packName", info->getPackName().substr(0, 64)},
        {"quality", std::clamp(info->getQuality(), 1, 3)},
        {"specialID", std::max(0, info->getSpecialID())},
        {"fireCount", std::max(0, info->getFireCount())},
    });
    uploadAll(uploads, 0, [uploads, body, done](bool ok, std::string const& error) mutable {
        if (!ok) { done(false, error); return; }
        body["png"] = (*uploads)[0].sha;
        body["plist"] = uploads->size() > 1 ? (*uploads)[1].sha : std::string();
        CommunityClient::get().submitItem("icons", std::move(body), submitCallback(done));
    });
}

paimon::globalicon::GlobalIconMeta gradientMeta(matjson::Value const& item) {
    paimon::globalicon::GlobalIconMeta meta;
    meta.accountID = static_cast<int>(item["accountID"].asInt().unwrapOr(0));
    meta.username = item["username"].asString().unwrapOr("");
    meta.enabled = true;
    meta.share = {false, true, false};
    auto type = paimon::globalicon::iconTypeFromString(item["iconType"].asString().unwrapOr(""));
    meta.gradients = matjson::Value::object();
    if (type) meta.gradients[paimon::icon_gradients::GradientUtils::getConfigKey(*type, false)] = item["gradient"];
    if (item["assets"].isObject()) {
        for (auto const& entry : item["assets"]) {
            auto sha = entry.getKey();
            auto url = entry.asString().unwrapOr("");
            if (sha && sha->size() == 64 && !url.empty()) meta.assets[*sha] = url;
        }
    }
    return meta;
}

void takeGradient(matjson::Value const& item, bool apply, Done done) {
    auto meta = gradientMeta(item);
    if (meta.accountID <= 0 || meta.gradients.size() == 0) {
        done(false, tx("Gradiente invalido.", "Invalid gradient."));
        return;
    }
    paimon::globalicon::ensureGradientImages(meta, [meta, apply, done](paimon::globalicon::GradientPaths const& paths) {
        int written = paimon::globalicon::copyGradients(meta, paths, apply);
        if (written <= 0) {
            done(false, apply ? tx("No se pudo aplicar el gradiente.", "Could not apply the gradient.")
                              : tx("Ese gradiente ya estaba en tu biblioteca.", "That gradient was already in your library."));
            return;
        }
        done(true, apply ? tx("Gradiente aplicado a tu icono.", "Gradient applied to your icon.")
                         : tx("Gradiente guardado en tu biblioteca de Icon Gradients.", "Gradient saved to your Icon Gradients library."));
    });
}

paimon::globalicon::GlobalIconSlot iconSlot(matjson::Value const& item) {
    auto const& icon = item["icon"];
    paimon::globalicon::GlobalIconSlot slot;
    slot.type = icon["type"].asString().unwrapOr("");
    slot.name = icon["name"].asString().unwrapOr("");
    slot.packID = icon["packID"].asString().unwrapOr("");
    slot.packName = icon["packName"].asString().unwrapOr("");
    slot.quality = static_cast<int>(icon["quality"].asInt().unwrapOr(3));
    slot.pngFile = icon["pngFile"].asString().unwrapOr("");
    slot.pngUrl = icon["pngUrl"].asString().unwrapOr("");
    slot.plistFile = icon["plistFile"].asString().unwrapOr("");
    slot.plistUrl = icon["plistUrl"].asString().unwrapOr("");
    return slot;
}

void previewIcon(matjson::Value const& item, std::function<void(std::string const&)> done) {
    if (!paimon::globalicon::GlobalIconStorage::available()) { done(""); return; }
    int accountID = static_cast<int>(item["accountID"].asInt().unwrapOr(0));
    paimon::globalicon::GlobalIconStorage::get().ensureIcon(accountID, iconSlot(item),
        [done](bool ok, std::string const& name) { done(ok ? name : ""); });
}

void takeIcon(matjson::Value const& item, bool use, Done done) {
    auto* moreIcons = Loader::get()->getLoadedMod("hiimjustin000.more_icons");
    auto slot = iconSlot(item);
    auto type = paimon::globalicon::iconTypeFromString(slot.type);
    if (!moreIcons || !type || slot.pngUrl.empty()) {
        done(false, tx("Necesitas More Icons para usar iconos custom.", "You need More Icons to use custom icons."));
        return;
    }
    auto folder = paimon::paths::toStdPath(moreIcons->getConfigDir()) / std::string(moreIconsFolder(*type));
    auto stem = safeName(slot.name);
    auto suffix = std::string(qualitySuffix(slot.quality));
    // never overwrite an icon the player already has with the same name.
    std::error_code ec;
    if (std::filesystem::exists(folder / (stem + suffix + ".png"), ec)) {
        stem += "-paimon-" + slot.pngFile.substr(0, 6);
    }
    auto png = folder / (stem + suffix + ".png");
    auto plist = folder / (stem + suffix + ".plist");
    auto quality = slot.quality;
    paimon::globalicon::GlobalIconClient::get().downloadFile(slot.pngUrl,
        [slot, folder, png, plist, stem, quality, type, use, done](bool ok, std::vector<uint8_t> const& texture) {
            if (!ok || !isPng(texture)) { done(false, tx("No se pudo descargar el icono.", "Could not download the icon.")); return; }
            auto finish = [png, plist, stem, quality, type, use, done, texture, folder](std::vector<uint8_t> const& sheet) {
                std::error_code ec;
                std::filesystem::create_directories(folder, ec);
                if (!paimon::file::writeAtomically(png, std::span<uint8_t const>(texture.data(), texture.size()))
                    || (!sheet.empty() && !paimon::file::writeAtomically(plist, std::span<uint8_t const>(sheet.data(), sheet.size())))) {
                    done(false, tx("No se pudo guardar en la carpeta de More Icons.", "Could not write to the More Icons folder."));
                    return;
                }
                // more icons reads the folder on startup; this makes it usable right away.
                more_icons::preRefreshIcons();
                auto* info = more_icons::addIcon(stem, stem, *type, png, sheet.empty() ? std::filesystem::path() : plist,
                    static_cast<cocos2d::TextureQuality>(std::clamp(quality, 1, 3)));
                more_icons::refreshIcons();
                if (use && info) more_icons::setIcon(info, *type);
                done(true, use ? tx("Icono guardado en More Icons y puesto.", "Icon saved to More Icons and equipped.")
                               : tx("Icono guardado en tu carpeta de More Icons.", "Icon saved to your More Icons folder."));
            };
            if (slot.plistUrl.empty()) { finish({}); return; }
            paimon::globalicon::GlobalIconClient::get().downloadFile(slot.plistUrl,
                [finish, done](bool ok, std::vector<uint8_t> const& sheet) {
                    if (!ok || sheet.empty()) { done(false, tx("No se pudo descargar la hoja del icono.", "Could not download the icon sheet.")); return; }
                    finish(sheet);
                });
        });
}

void fetchPreset(matjson::Value const& item, std::function<void(geode::Result<shop::Preset>)> done) {
    auto id = item["id"].asString().unwrapOr("");
    CommunityClient::get().getItem("presets", id, [done](bool ok, matjson::Value const& json, std::string const& error) {
        if (!ok) { done(Err(error)); return; }
        auto full = json["item"];
        CommunityClient::get().downloadPublic(full["previewUrl"].asString().unwrapOr(""),
            [done, full](bool ok, std::vector<uint8_t> const& bytes) {
                auto category = shop::parseCategory(full["category"].asString().unwrapOr(""));
                if (!ok || !category) {
                    done(Err(tx("No se pudo descargar el preset.", "Could not download the preset.")));
                    return;
                }
                shop::Preset preset;
                preset.title = full["title"].asString().unwrapOr("");
                preset.description = full["description"].asString().unwrapOr("");
                preset.author = full["username"].asString().unwrapOr("");
                preset.accountID = static_cast<int>(full["accountID"].asInt().unwrapOr(0));
                preset.createdAt = full["createdAt"].asInt().unwrapOr(0) / 1000;
                preset.category = category.unwrap();
                preset.configuration = full["configuration"];
                preset.preview = paimon::base64Encode(bytes);
                if (auto valid = shop::validatePreset(preset); !valid) { done(Err(valid.unwrapErr())); return; }
                done(Ok(std::move(preset)));
            });
    });
}

} // namespace paimon::community::items
