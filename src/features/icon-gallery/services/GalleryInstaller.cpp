#include "GalleryInstaller.hpp"

#include "GalleryStore.hpp"
#include "../../texture-studio/data/PlistBuilder.hpp"
#include "../../texture-studio/data/RectPacker.hpp"
#include "../../texture-studio/data/SpritesheetReader.hpp"
#include "../../../framework/compat/ModCompat.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

#include <matjson.hpp>

#include <algorithm>
#include <fstream>
#include <cmath>

using namespace geode::prelude;

namespace paimon::icon_gallery {

namespace {

constexpr std::string_view kNamePrefix = "paimbgallery-";
constexpr char const* kRecordName = "install.json";

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

// More Icons reads frame coordinates at the director's content scale; its
// quality flag does not resize an atlas supplied through addIcon. Keep the
// downloaded original and repack individual frames for the current quality.
Result<SheetPaths> prepareSheet(SheetPaths const& source) {
    namespace ts = paimon::texture_studio;
    auto quality = qualityFromName(source.plist.filename().string());
    float sourceScale = quality == kTextureQualityHigh ? 4.f
        : quality == kTextureQualityMedium ? 2.f : 1.f;
    float targetScale = CCDirector::sharedDirector()->getContentScaleFactor();
    if (targetScale == sourceScale) return Ok(source);
    float ratio = targetScale / sourceScale;

    auto loaded = ts::SpritesheetReader::loadFromPaths(source.plist, source.png);
    if (!loaded) return Err("{}", loaded.unwrapErr());
    auto sheet = std::move(loaded.unwrap());
    if (sheet.frames.empty()) return Err("La hoja no contiene frames");

    std::vector<ts::RectPackInput> inputs;
    for (std::size_t i = 0; i < sheet.frames.size(); ++i) {
        auto& frame = sheet.frames[i];
        int w = std::max(1, static_cast<int>(std::lround(frame.pixels.width() * ratio)));
        int h = std::max(1, static_cast<int>(std::lround(frame.pixels.height() * ratio)));
        frame.pixels = frame.pixels.resizedBilinear(w, h);
        frame.info.spriteW = frame.info.rectW = w;
        frame.info.spriteH = frame.info.rectH = h;
        frame.info.sourceW = std::max(w, static_cast<int>(std::lround(frame.info.sourceW * ratio)));
        frame.info.sourceH = std::max(h, static_cast<int>(std::lround(frame.info.sourceH * ratio)));
        frame.info.offsetX *= ratio;
        frame.info.offsetY *= ratio;
        frame.info.rotated = false;
        inputs.push_back({std::to_string(i), w, h});
    }
    auto packed = ts::RectPacker::pack(std::move(inputs));
    if (packed.sheetWidth <= 0 || packed.sheetHeight <= 0)
        return Err("No se pudo adaptar la hoja del icono");

    auto suffix = targetScale >= 4.f ? "-uhd" : targetScale >= 2.f ? "-hd" : "";
    auto dir = source.png.parent_path() / "runtime";
    SheetPaths output{dir / (std::string("icon") + suffix + ".png"),
                      dir / (std::string("icon") + suffix + ".plist")};
    ts::ImageBuffer atlas(packed.sheetWidth, packed.sheetHeight);
    ts::ParsedSpritesheet result;
    result.metadata = sheet.metadata;
    result.metadata.format = 3;
    result.metadata.sizeW = packed.sheetWidth;
    result.metadata.sizeH = packed.sheetHeight;
    result.metadata.textureFileName = output.png.filename().string();
    result.metadata.realTextureFileName = result.metadata.textureFileName;
    result.metadata.smartUpdate.clear();
    for (auto const& placement : packed.placements) {
        auto& frame = sheet.frames[std::stoul(placement.id)];
        atlas.blitOverwrite(placement.x, placement.y, frame.pixels);
        frame.info.rectX = placement.x;
        frame.info.rectY = placement.y;
        result.frames.push_back(std::move(frame.info));
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return Err("No se pudo crear la carpeta del icono: {}", ec.message());
    if (auto saved = atlas.saveToPng(output.png); !saved) return Err("{}", saved.unwrapErr());
    if (auto saved = ts::PlistBuilder::buildFile(result, output.plist); !saved)
        return Err("{}", saved.unwrapErr());
    return Ok(output);
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

// installed/<slug>/install.json: lo unico que hace falta para volver a
// registrar el icono al arrancar, cuando no hay red ni catalogo cargado.
struct InstallRecord {
    std::string name;
    IconType type = IconType::Cube;
};

std::string readText(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void writeRecord(std::string const& slug, InstallRecord const& rec) {
    auto json = matjson::Value::object();
    json["name"] = rec.name;
    json["type"] = iconTypeName(rec.type);
    auto dumped = json.dump(matjson::NO_INDENTATION);
    writeBytes(GalleryStore::installDirFor(slug) / kRecordName,
               std::vector<std::uint8_t>(dumped.begin(), dumped.end()));
}

bool readRecord(std::string const& slug, InstallRecord& out) {
    auto text = readText(GalleryStore::installDirFor(slug) / kRecordName);
    if (text.empty()) return false;

    auto parsed = matjson::parse(text);
    if (!parsed || !parsed.unwrap().isObject()) return false;

    auto const& root = parsed.unwrap();
    if (!iconTypeFromName(root["type"].asString().unwrapOr(""), out.type)) return false;
    out.name = root["name"].asString().unwrapOr(slug);
    if (out.name.empty()) out.name = slug;
    return true;
}

// Instalaciones anteriores al install.json: el gamemode sale del icon.json que
// la tienda dejo en cache al pintar la tarjeta.
bool recordFromMetaCache(std::string const& slug, InstallRecord& out) {
    auto text = readText(GalleryStore::metaDir() / (slug + ".json"));
    if (text.empty()) return false;

    auto meta = GalleryClient::parseMeta(text, slug);
    if (!meta) return false;

    auto icon = std::move(meta.unwrap());
    out.name = icon.displayName();
    out.type = icon.type;
    return true;
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
    writeRecord(slug, {pkg.meta.displayName(), pkg.meta.type});
    return Ok(GalleryStore::installDirFor(slug));
}

Result<> GalleryInstaller::install(GalleryPackage const& pkg) {
    if (!moreIconsAvailable()) return Err("More Icons no esta instalado");

    auto saved = saveOnly(pkg);
    if (!saved) return Err("{}", saved.unwrapErr());

    auto const& slug = pkg.meta.slug;
    auto stem = sheetStem(pkg);
    auto prepared = prepareSheet(pathsFor(slug, stem));
    if (!prepared) return Err("{}", prepared.unwrapErr());
    auto paths = std::move(prepared.unwrap());
    auto regName = registeredName(slug);

    more_icons::preRefreshIcons();
    if (auto* existing = more_icons::getIcon(regName, pkg.meta.type)) {
        existing->setTexture(paths.png);
        existing->setSheet(paths.plist);
        existing->setQuality(qualityFromName(paths.plist.filename().string()));
        more_icons::updateIcon(existing);
        more_icons::refreshIcons();
        GalleryStore::get().markInstalled(slug, true);
        return Ok();
    }

    auto* info = more_icons::addIcon(
        regName, pkg.meta.displayName(), pkg.meta.type,
        paths.png, paths.plist,
        qualityFromName(paths.plist.filename().string()),
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

    std::error_code ec;
    auto dir = GalleryStore::installDir();
    if (!std::filesystem::is_directory(dir, ec)) return;

    int registered = 0;
    bool touched = false;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;

        auto slug = entry.path().filename().string();
        InstallRecord rec;
        if (!readRecord(slug, rec)) {
            if (!recordFromMetaCache(slug, rec)) continue;
            // Se instalo antes de que existiera el install.json: se deja
            // escrito para no depender de la cache de metadatos otra vez.
            writeRecord(slug, rec);
        }

        auto regName = registeredName(slug);
        if (more_icons::getIcon(regName, rec.type)) continue;

        SheetPaths paths;
        std::string stem;
        if (!findInstalledSheet(slug, paths, stem)) continue;
        auto prepared = prepareSheet(paths);
        if (!prepared) {
            log::warn("[icon-gallery] no se pudo adaptar '{}': {}", slug, prepared.unwrapErr());
            continue;
        }
        paths = std::move(prepared.unwrap());

        if (!touched) {
            more_icons::preRefreshIcons();
            touched = true;
        }
        more_icons::addIcon(
            regName, rec.name, rec.type,
            paths.png, paths.plist,
            qualityFromName(paths.plist.filename().string()),
            "flozwer.paimbnails2", "Icon Gallery");
        ++registered;
    }
    if (touched) more_icons::refreshIcons();
    if (registered > 0) {
        log::info("[icon-gallery] {} iconos devueltos a More Icons", registered);
    }
}

}  // namespace paimon::icon_gallery
