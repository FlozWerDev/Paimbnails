#include "GalleryClient.hpp"

#include "../../../utils/WebHelper.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

using namespace geode::prelude;

namespace paimon::icon_gallery {

namespace {

// Un .gdicon pesa ~30-70 KB. 4 MB deja margen de sobra y corta descargas
// absurdas si algun dia la galeria sirve otra cosa en esa ruta.
constexpr std::size_t kMaxPackageBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxRegistryBytes = 2u * 1024u * 1024u;

std::string jStr(matjson::Value const& v, std::string const& def = "") {
    return v.isString() ? v.asString().unwrapOr(def) : def;
}

bool jBool(matjson::Value const& v, bool def = false) {
    return v.isBool() ? v.asBool().unwrapOr(def) : def;
}

// "#aabbcc" -> ccColor3B. Devuelve false si no tiene ese formato.
bool parseHexColor(std::string const& text, cocos2d::ccColor3B& out) {
    if (text.size() != 7 || text[0] != '#') return false;
    unsigned int r = 0, g = 0, b = 0;
    if (std::sscanf(text.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3) return false;
    out = {static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b)};
    return true;
}

// "2026-07-25T12:43:05.932Z" -> ms desde epoch (UTC). 0 si no se entiende.
// Solo se usa para ordenar, asi que basta con que sea monotono.
std::int64_t parseIsoDate(std::string const& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                    &year, &month, &day, &hour, &minute, &second) < 3) {
        return 0;
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;

    // Dias desde 1970-01-01 (algoritmo de Howard Hinnant, days_from_civil).
    int y = year;
    unsigned m = static_cast<unsigned>(month);
    unsigned d = static_cast<unsigned>(day);
    y -= m <= 2;
    int const era = (y >= 0 ? y : y - 399) / 400;
    unsigned const yoe = static_cast<unsigned>(y - era * 400);
    unsigned const doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    unsigned const doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    std::int64_t const days = static_cast<std::int64_t>(era) * 146097 +
                              static_cast<std::int64_t>(doe) - 719468;

    std::int64_t const secs = days * 86400 + hour * 3600 + minute * 60 + second;
    return secs * 1000;
}

bool endsWithNoCase(std::string const& text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        auto lhs = static_cast<unsigned char>(text[text.size() - suffix.size() + i]);
        auto rhs = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) return false;
    }
    return true;
}

}  // anonymous namespace

Result<GalleryIcon> GalleryClient::parseMeta(std::string const& json,
                                            std::string const& slug) {
    auto parsed = matjson::parse(json);
    if (!parsed) return Err("icon.json ilegible: {}", parsed.unwrapErr());

    auto const& root = parsed.unwrap();
    if (!root.isObject()) return Err("icon.json no es un objeto");

    GalleryIcon icon;
    icon.slug = slug;
    icon.path = "icons/" + slug + ".gdicon";
    icon.metaLoaded = true;
    icon.name = jStr(root["iconName"], slug);
    icon.author = jStr(root["author"]);
    icon.description = jStr(root["description"]);
    icon.format = jStr(root["format"], "more-icons");
    icon.uuid = jStr(root["uuid"]);
    icon.isCollab = jBool(root["isCollab"]);
    icon.hasProjectFiles = jBool(root["hasProjectFiles"]);
    icon.createdAtMs = parseIsoDate(jStr(root["creationDate"]));

    if (!iconTypeFromName(jStr(root["iconType"], "Cube"), icon.type)) {
        icon.type = IconType::Cube;
    }

    if (auto const& collab = root["collabWith"]; collab.isArray()) {
        for (auto const& entry : collab) {
            auto who = jStr(entry);
            if (!who.empty()) icon.collabWith.push_back(std::move(who));
        }
    }

    // "colors" es una lista; la galeria solo usa el primer juego.
    if (auto const& colors = root["colors"]; colors.isArray()) {
        for (auto const& set : colors) {
            if (!set.isObject()) continue;
            bool const ok1 = parseHexColor(jStr(set["p1"]), icon.color1);
            bool const ok2 = parseHexColor(jStr(set["p2"]), icon.color2);
            parseHexColor(jStr(set["glow"]), icon.colorGlow);
            icon.hasColors = ok1 || ok2;
            break;
        }
    }

    return Ok(std::move(icon));
}

Result<GalleryPackage> GalleryClient::unpack(std::vector<std::uint8_t> const& bytes,
                                             std::string const& slug) {
    if (bytes.empty()) return Err("el .gdicon vino vacio");

    auto unzip = file::Unzip::create(ByteSpan(bytes.data(), bytes.size()));
    if (!unzip) return Err("no es un .gdicon valido: {}", unzip.unwrapErr());
    auto archive = std::move(unzip.unwrap());

    GalleryPackage pkg;

    auto jsonBytes = archive.extract("icon.json");
    if (!jsonBytes) return Err("al .gdicon le falta icon.json");
    auto const& raw = jsonBytes.unwrap();
    auto meta = parseMeta(std::string(raw.begin(), raw.end()), slug);
    if (!meta) return Err("{}", meta.unwrapErr());
    pkg.meta = std::move(meta.unwrap());

    // La hoja es el unico .png que no es la vista previa; el nombre no siempre
    // coincide con el del .plist (hay envios con el png tal cual lo exporto el
    // autor, p.ej. "1000433913.png" junto a "spider_32-uhd.plist").
    for (auto const& entry : archive.getEntries()) {
        auto name = entry.filename().string();
        if (name == "icon.json") continue;
        if (name == "preview.png") {
            if (auto data = archive.extract(entry)) pkg.preview = std::move(data.unwrap());
            continue;
        }
        if (endsWithNoCase(name, ".plist") && pkg.plist.empty()) {
            if (auto data = archive.extract(entry)) {
                pkg.plist = std::move(data.unwrap());
                pkg.plistName = name;
            }
            continue;
        }
        if (endsWithNoCase(name, ".png") && pkg.sheetPng.empty()) {
            if (auto data = archive.extract(entry)) {
                pkg.sheetPng = std::move(data.unwrap());
                pkg.sheetPngName = name;
            }
        }
        // projectFiles.zip y cualquier extra se ignoran: son fuentes del autor.
    }

    if (pkg.sheetPng.empty() || pkg.plist.empty()) {
        return Err("el .gdicon no trae hoja (.png + .plist)");
    }
    return Ok(std::move(pkg));
}

void GalleryClient::fetchRegistry(RegistryCallback cb) {
    web::WebRequest req;
    req.timeout(std::chrono::seconds(20));

    WebHelper::dispatch(std::move(req), "GET",
                        std::string(kGalleryBaseUrl) + "/icons/registry.json",
                        [cb](web::WebResponse res) mutable {
        if (!res.ok()) {
            cb(Err("La galeria respondio {}", res.code()));
            return;
        }
        auto body = res.string().unwrapOr("");
        if (body.empty() || body.size() > kMaxRegistryBytes) {
            cb(Err("registry.json vacio o demasiado grande"));
            return;
        }
        auto parsed = matjson::parse(body);
        if (!parsed || !parsed.unwrap().isArray()) {
            cb(Err("registry.json no es una lista"));
            return;
        }

        std::vector<std::string> paths;
        for (auto const& entry : parsed.unwrap()) {
            auto path = jStr(entry);
            // "icons/.gdicon" es una entrada fantasma del generador.
            if (path.size() <= std::string_view("icons/.gdicon").size()) continue;
            if (!endsWithNoCase(path, ".gdicon")) continue;
            paths.push_back(std::move(path));
        }
        if (paths.empty()) {
            cb(Err("La galeria no devolvio ningun icono"));
            return;
        }
        cb(Ok(std::move(paths)));
    });
}

void GalleryClient::fetchPackage(GalleryIcon const& icon, PackageCallback cb) {
    web::WebRequest req;
    req.timeout(std::chrono::seconds(25));

    std::string const slug = icon.slug;
    WebHelper::dispatch(std::move(req), "GET", icon.url(),
                        [cb, slug](web::WebResponse res) mutable {
        if (!res.ok()) {
            cb(Err("HTTP {}", res.code()));
            return;
        }
        auto bytes = res.data();
        if (bytes.empty() || bytes.size() > kMaxPackageBytes) {
            cb(Err("descarga vacia o demasiado grande"));
            return;
        }
        cb(GalleryClient::unpack(bytes, slug));
    });
}

}  // namespace paimon::icon_gallery
