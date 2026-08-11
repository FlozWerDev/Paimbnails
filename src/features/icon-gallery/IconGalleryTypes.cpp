#include "IconGalleryTypes.hpp"

#include "../../utils/Localization.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::icon_gallery {

namespace {

// Comparacion sin distinguir mayusculas: la galeria escribe "UFO" pero
// tambien aparece "Ufo" en envios viejos.
bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        auto lhs = static_cast<unsigned char>(a[i]);
        auto rhs = static_cast<unsigned char>(b[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) return false;
    }
    return true;
}

}  // anonymous namespace

bool iconTypeFromName(std::string_view name, IconType& out) {
    for (auto type : kGamemodes) {
        if (equalsNoCase(name, iconTypeName(type))) {
            out = type;
            return true;
        }
    }
    return false;
}

char const* iconTypeName(IconType type) {
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
        default: return "Cube";
    }
}

std::string iconTypeLabel(IconType type) {
    switch (type) {
        case IconType::Cube: return Localization::get().getString("icon-gallery.type.cube");
        case IconType::Ship: return Localization::get().getString("icon-gallery.type.ship");
        case IconType::Ball: return Localization::get().getString("icon-gallery.type.ball");
        case IconType::Ufo: return Localization::get().getString("icon-gallery.type.ufo");
        case IconType::Wave: return Localization::get().getString("icon-gallery.type.wave");
        case IconType::Robot: return Localization::get().getString("icon-gallery.type.robot");
        case IconType::Spider: return Localization::get().getString("icon-gallery.type.spider");
        case IconType::Swing: return Localization::get().getString("icon-gallery.type.swing");
        case IconType::Jetpack: return Localization::get().getString("icon-gallery.type.jetpack");
        default: return "?";
    }
}

std::string GalleryIcon::displayName() const {
    if (metaLoaded && !name.empty()) return name;
    std::string pretty = slug;
    std::replace(pretty.begin(), pretty.end(), '_', ' ');
    return pretty;
}

std::string GalleryIcon::url() const {
    return std::string(kGalleryBaseUrl) + "/" + path;
}

}  // namespace paimon::icon_gallery
