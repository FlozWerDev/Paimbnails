#include "GlobalIconGradients.hpp"
#include "services/GlobalIconClient.hpp"
#include "../icon-gradients/GradientCache.hpp"
#include "../icon-gradients/GradientUtils.hpp"
#include "../../utils/AtomicFileWrite.hpp"
#include "../../utils/FilesystemPath.hpp"
#include "../../utils/LocalAssetStore.hpp"

#include <Geode/binding/ProfilePage.hpp>
#include <algorithm>
#include <fstream>
#include <memory>
#include <span>

using namespace geode::prelude;

namespace paimon::globalicon {

namespace {

namespace grad = paimon::icon_gradients;

std::filesystem::path cachedImage(int accountID, std::string const& sha) {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "global_icons_cache" / std::to_string(accountID)
        / "gradients" / (sha + ".png");
}

std::filesystem::path copiedImage(std::string const& sha) {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "copied-gradient-images" / (sha + ".png");
}

bool isPngFile(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    std::ifstream in(path, std::ios::binary);
    char header[8]{};
    in.read(header, sizeof(header));
    return in.gcount() == 8 && std::string_view(header, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8);
}

bool isPngBytes(std::vector<uint8_t> const& data) {
    return data.size() > 8 && std::string_view(reinterpret_cast<char const*>(data.data()), 8)
        == std::string_view("\x89PNG\r\n\x1a\n", 8);
}

// the type each slot of the profile's "player-menu" shows (cube..jetpack).
IconType slotType(int slot) {
    return static_cast<IconType>(slot);
}

grad::GradientConfig configFor(matjson::Value const& container, int color) {
    auto const& value = container["color" + std::to_string(color)];
    if (!value.isObject()) return {};
    return grad::GradientUtils::configFromObject(value);
}

grad::Gradient gradientFor(matjson::Value const& localized, IconType type) {
    auto const& container = localized[grad::GradientUtils::getConfigKey(type, false)];
    if (!container.isObject()) return {};
    return {configFor(container, 1), configFor(container, 2), configFor(container, 3),
            configFor(container, 4), configFor(container, 5)};
}

} // namespace

grad::Gradient sharedGradient(matjson::Value const& localized, IconType type) {
    return gradientFor(localized, type);
}

void ensureGradientImages(GlobalIconMeta const& meta, geode::CopyableFunction<void(GradientPaths const&)> cb) {
    struct State {
        GradientPaths paths;
        int remaining = 0;
        geode::CopyableFunction<void(GradientPaths const&)> cb;
    };
    auto state = std::make_shared<State>();
    state->cb = std::move(cb);
    std::vector<std::pair<std::string, std::string>> missing;
    for (auto const& [sha, url] : meta.assets) {
        auto path = cachedImage(meta.accountID, sha);
        if (isPngFile(path)) state->paths[sha] = paimon::paths::toUtf8(path);
        else missing.emplace_back(sha, url);
    }
    state->remaining = static_cast<int>(missing.size());
    if (state->remaining == 0) {
        if (state->cb) state->cb(state->paths);
        return;
    }
    for (auto const& [sha, url] : missing) {
        GlobalIconClient::get().downloadFile(url, [state, sha, accountID = meta.accountID](bool ok, std::vector<uint8_t> const& data) {
            auto path = cachedImage(accountID, sha);
            if (ok && isPngBytes(data)) {
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                if (paimon::file::writeAtomically(path, std::span<uint8_t const>(data.data(), data.size()))) {
                    state->paths[sha] = paimon::paths::toUtf8(path);
                }
            }
            if (--state->remaining == 0 && state->cb) state->cb(state->paths);
        });
    }
}

matjson::Value localizeGradients(matjson::Value const& shared, GradientPaths const& paths) {
    auto out = matjson::Value::object();
    if (!shared.isObject()) return out;
    for (auto const& container : shared) {
        auto key = container.getKey();
        if (!key || !container.isObject()) continue;
        auto colors = matjson::Value::object();
        for (auto const& config : container) {
            auto colorKey = config.getKey();
            if (!colorKey || !config["points"].isArray()) continue;
            auto points = matjson::Value::array();
            for (auto const& point : config["points"]) {
                auto copy = matjson::makeObject({{"pos", point["pos"]}, {"color", point["color"]}});
                auto image = point["image"].asString().unwrapOr("");
                if (image.starts_with("asset:")) {
                    if (auto found = paths.find(image.substr(6)); found != paths.end()) copy["image"] = found->second;
                }
                points.push(copy);
            }
            colors[*colorKey] = matjson::makeObject({{"points", points}, {"linear", config["linear"].asBool().unwrapOr(true)}});
        }
        out[*key] = colors;
    }
    return out;
}

void renderProfileGradients(ProfilePage* page) {
    if (!page || page->m_ownProfile || page->m_accountID <= 0) return;
    if (!Mod::get()->getSettingValue<bool>("global-icons-enabled") || !grad::moduleEnabled()) return;
    WeakRef<ProfilePage> weak = page;
    GlobalIconClient::get().getMetadata(page->m_accountID, [weak](bool ok, bool found, GlobalIconMeta const& meta) {
        if (!ok || !found || !meta.share.gradients || !meta.gradients.isObject()) return;
        ensureGradientImages(meta, [weak, shared = meta.gradients](GradientPaths const& paths) {
            auto page = weak.lock();
            if (!page || !page->m_mainLayer) return;
            auto* menu = page->m_mainLayer->getChildByID("player-menu");
            if (!menu) return;
            auto localized = localizeGradients(shared, paths);
            int slot = 0;
            for (auto* entry : menu->getChildrenExt<CCNode*>()) {
                // only the gamemodes have gradient slots; trail and death effect come after them.
                if (slot > static_cast<int>(IconType::Jetpack)) break;
                if (auto* doll = entry->getChildByType<SimplePlayer>(0)) {
                    grad::GradientUtils::applyGradient(doll, gradientFor(localized, slotType(slot)), false, false, 99);
                }
                slot++;
            }
        });
    });
}

int copyGradients(GlobalIconMeta const& meta, GradientPaths const& cachePaths, bool apply) {
    if (!meta.gradients.isObject()) return 0;
    // copied gradients outlive the cache, which is pruned per account.
    GradientPaths kept;
    for (auto const& [sha, path] : cachePaths) {
        auto target = copiedImage(sha);
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (!isPngFile(target)) std::filesystem::copy_file(paimon::assets::pathFromUtf8(path), target,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (isPngFile(target)) kept[sha] = paimon::paths::toUtf8(target);
    }
    auto localized = localizeGradients(meta.gradients, kept);
    int written = 0;
    auto* mod = Mod::get();
    if (apply) {
        for (auto const& container : localized) {
            auto key = container.getKey();
            if (!key) continue;
            mod->setSavedValue(*key, container);
            written++;
        }
        return written;
    }
    auto library = mod->getSavedValue<matjson::Value>(grad::kSavedGradientsKey);
    if (!library.isArray()) library = matjson::Value::array();
    for (auto const& container : localized) {
        for (auto const& config : container) {
            if (!config["points"].isArray() || config["points"].size() == 0) continue;
            auto parsed = grad::GradientUtils::configFromObject(config);
            if (grad::GradientUtils::isGradientSaved(parsed)) continue;
            library.push(grad::GradientUtils::getSaveObject(parsed));
            written++;
        }
    }
    mod->setSavedValue(grad::kSavedGradientsKey, library);
    return written;
}

} // namespace paimon::globalicon
