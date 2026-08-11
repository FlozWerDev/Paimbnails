#include "GalleryInstaller.hpp"

#include "GalleryStore.hpp"
#include "../../../framework/compat/ModCompat.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

#include <algorithm>
#include <fstream>

using namespace geode::prelude;

namespace paimon::icon_gallery {

namespace {

constexpr std::string_view kNamePrefix = "paimbgallery-";

bool writeBytes(std::filesystem::path const& path, std::vector<std::uint8_t> const& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<char const*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

// La galeria exporta casi todo a -uhd; el sufijo del .plist es lo que manda.
cocos2d::TextureQuality qualityFromName(std::string const& plistName) {
    auto stem = plistName;
    if (auto dot = stem.find_last_of('.'); dot != std::string::npos) stem.resize(dot);
    if (stem.size() >= 4 && stem.compare(stem.size() - 4, 4, "-uhd") == 0) {
        return cocos2d::kTextureQualityHigh;
    }
    if (stem.size() >= 3 && stem.compare(stem.size() - 3, 3, "-hd") == 0) {
        return cocos2d::kTextureQualityMedium;
    }
    return cocos2d::kTextureQualityLow;
}

// El .png y el .plist se guardan con el mismo nombre base (el del plist).
// El textureFileName que traen los plists de la galeria apunta a rutas que no
// existen en el ZIP, asi que no sirve de referencia; More Icons usa las rutas
// que le pasamos, y dejarlos emparejados evita sorpresas.
std::string sheetStem(GalleryPackage const& pkg) {
    auto stem = pkg.plistName;
    if (auto dot = stem.find_last_of('.'); dot != std::string::npos) stem.resize(dot);
    if (stem.empty()) stem = pkg.meta.slug;
    return stem;
}

struct SheetPaths {
    std::filesystem::path png;
    std::filesystem::path plist;
};

SheetPaths pathsFor(std::string const& slug, std::string const& stem) {
    auto dir = GalleryStore::installDirFor(slug);
    return {dir / (stem + ".png"), dir / (stem + ".plist")};
}

// Busca en installed/<slug>/ el par png+plist que se escribio al instalar.
bool findInstalledSheet(std::string const& slug, SheetPaths& out, std::string& stem) {
    auto dir = GalleryStore::installDirFor(slug);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".plist") continue;
        stem = entry.path().stem().string();
        auto png = dir / (stem + ".png");
        if (!std::filesystem::exists(png, ec)) continue;
        out = {png, entry.path()};
        return true;
    }
    return false;
}

}  // anonymous namespace

bool GalleryInstaller::moreIconsAvailable() {
    return paimon::compat::ModCompat::isMoreIconsLoaded();
}

std::string GalleryInstaller::registeredName(std::string_view slug) {
    return std::string(kNamePrefix) + std::string(slug);
}

Result<std::filesystem::path> GalleryInstaller::saveOnly(GalleryPackage const& pkg) {
    auto const& slug = pkg.meta.slug;
    if (slug.empty()) return Err("Icono sin nombre");

    auto stem = sheetStem(pkg);
    auto paths = pathsFor(slug, stem);

    if (!writeBytes(paths.png, pkg.sheetPng)) {
        return Err("No se pudo escribir {}", paths.png.filename().string());
    }
    if (!writeBytes(paths.plist, pkg.plist)) {
        return Err("No se pudo escribir {}", paths.plist.filename().string());
    }
    return Ok(GalleryStore::installDirFor(slug));
}

Result<> GalleryInstaller::install(GalleryPackage const& pkg) {
    if (!moreIconsAvailable()) return Err("More Icons no esta instalado");

    auto saved = saveOnly(pkg);
    if (!saved) return Err("{}", saved.unwrapErr());

    auto const& slug = pkg.meta.slug;
    auto stem = sheetStem(pkg);
    auto paths = pathsFor(slug, stem);
    auto regName = registeredName(slug);

    more_icons::preRefreshIcons();
    if (auto* existing = more_icons::getIcon(regName, pkg.meta.type)) {
        more_icons::updateIcon(existing);
        more_icons::refreshIcons();
        GalleryStore::get().markInstalled(slug, true);
        return Ok();
    }

    auto* info = more_icons::addIcon(
        regName, pkg.meta.displayName(), pkg.meta.type,
        paths.png, paths.plist,
        qualityFromName(pkg.plistName),
        "flozwer.paimbnails2", "Icon Gallery");
    more_icons::refreshIcons();

    if (!info) return Err("More Icons rechazo el icono");

    GalleryStore::get().markInstalled(slug, true);
    log::info("[icon-gallery] '{}' instalado como '{}'", slug, regName);
    return Ok();
}

Result<> GalleryInstaller::uninstall(std::string const& slug, IconType type) {
    if (moreIconsAvailable()) {
        auto regName = registeredName(slug);
        more_icons::preRefreshIcons();
        if (auto* active = more_icons::activeIcon(type);
            active && active->getName() == regName) {
            more_icons::setIcon(nullptr, type);
        }
        if (auto* info = more_icons::getIcon(regName, type)) {
            more_icons::removeIcon(info);
        }
        more_icons::refreshIcons();
    }

    std::error_code ec;
    std::filesystem::remove_all(GalleryStore::installDirFor(slug), ec);

    GalleryStore::get().markInstalled(slug, false);
    return Ok();
}

bool GalleryInstaller::equip(std::string const& slug, IconType type) {
    if (!moreIconsAvailable()) return false;
    auto* info = more_icons::getIcon(registeredName(slug), type);
    if (!info) return false;
    more_icons::setIcon(info, type);
    return true;
}

bool GalleryInstaller::isEquipped(std::string const& slug, IconType type) {
    if (!moreIconsAvailable()) return false;
    auto* active = more_icons::activeIcon(type);
    return active && active->getName() == registeredName(slug);
}

void GalleryInstaller::registerAllInstalled() {
    if (!moreIconsAvailable()) return;

    auto& store = GalleryStore::get();
    std::error_code ec;
    auto dir = GalleryStore::installDir();
    if (!std::filesystem::is_directory(dir, ec)) return;

    bool touched = false;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;

        auto slug = entry.path().filename().string();
        auto const* icon = store.find(slug);
        // Sin metadatos no se sabe el gamemode; se registra al abrir la ficha,
        // que es cuando la tienda ya bajo el icon.json.
        if (!icon || !icon->metaLoaded) continue;

        auto regName = registeredName(slug);
        if (more_icons::getIcon(regName, icon->type)) continue;

        SheetPaths paths;
        std::string stem;
        if (!findInstalledSheet(slug, paths, stem)) continue;

        if (!touched) {
            more_icons::preRefreshIcons();
            touched = true;
        }
        more_icons::addIcon(
            regName, icon->displayName(), icon->type,
            paths.png, paths.plist,
            qualityFromName(paths.plist.filename().string()),
            "flozwer.paimbnails2", "Icon Gallery");
    }
    if (touched) more_icons::refreshIcons();
}

}  // namespace paimon::icon_gallery
