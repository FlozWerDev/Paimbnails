#include "ModlyUIHelpers.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::compat_mods {

namespace {
    // HSV -> RGB with saturation and value fixed, which is all avatarColor needs.
    ccColor3B hueToColor(float hue) {
        float s = 0.55f, v = 0.78f;
        float c = v * s;
        float x = c * (1.f - std::fabs(std::fmod(hue / 60.f, 2.f) - 1.f));
        float m = v - c;
        float r = 0.f, g = 0.f, b = 0.f;
        if (hue < 60.f)       { r = c; g = x; }
        else if (hue < 120.f) { r = x; g = c; }
        else if (hue < 180.f) { g = c; b = x; }
        else if (hue < 240.f) { g = x; b = c; }
        else if (hue < 300.f) { r = x; b = c; }
        else                  { r = c; b = x; }
        return {
            static_cast<GLubyte>((r + m) * 255.f),
            static_cast<GLubyte>((g + m) * 255.f),
            static_cast<GLubyte>((b + m) * 255.f)
        };
    }
} // namespace

ccColor3B avatarColor(std::string const& name) {
    unsigned int hash = 0;
    for (unsigned char ch : name) hash = ch + ((hash << 5) - hash);
    return hueToColor(static_cast<float>(hash % 360u));
}

void fitLabelWidth(CCLabelBMFont* label, float maxWidth) {
    if (!label || maxWidth <= 0.f) return;
    float width = label->getContentSize().width * label->getScale();
    if (width > maxWidth) label->setScale(label->getScale() * (maxWidth / width));
}

CCNode* createImageSlot(std::string const& url, float width, float height,
                        float radius, ccColor4B placeholder) {
    auto clipper = CCClippingNode::create();
    clipper->setContentSize({width, height});
    clipper->setAnchorPoint({0.5f, 0.5f});
    clipper->setStencil(paimon::SpriteHelper::createRoundedRectStencil(width, height, radius));

    auto bg = CCLayerColor::create(placeholder);
    bg->setContentSize({width, height});
    clipper->addChild(bg, 0);

    if (url.empty()) return clipper;

    auto sprite = LazySprite::create({width, height}, false);
    sprite->setPosition({width / 2.f, height / 2.f});
    sprite->setOpacity(0);
    clipper->addChild(sprite, 1);

    Ref<LazySprite> safeSprite = sprite;
    sprite->setLoadCallback([safeSprite, width, height](Result<> res) {
        if (!res.isOk() || !safeSprite || !safeSprite->getParent()) return;
        auto size = safeSprite->getContentSize();
        if (size.width <= 0.f || size.height <= 0.f) return;
        // Cover the slot, letting the stencil crop the overflow.
        safeSprite->setScale(std::max(width / size.width, height / size.height));
        safeSprite->runAction(CCFadeIn::create(0.25f));
    });
    sprite->loadFromUrl(url);

    return clipper;
}

CCNode* createAvatar(std::string const& url, bool hasImage,
                     std::string const& name, float size, float radius) {
    if (radius <= 0.f) radius = size / 2.f;

    if (hasImage && !url.empty()) {
        return createImageSlot(url, size, size, radius, {20, 22, 32, 255});
    }

    auto container = CCNode::create();
    container->setContentSize({size, size});
    container->setAnchorPoint({0.5f, 0.5f});

    auto color = avatarColor(name);
    auto disc = paimon::SpriteHelper::createRoundedRect(
        size, size, radius,
        {color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f});
    if (disc) {
        disc->setPosition({0.f, 0.f});
        container->addChild(disc, 0);
    }

    std::string initial(1, name.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
    auto letter = CCLabelBMFont::create(initial.c_str(), "bigFont.fnt");
    letter->setPosition({size / 2.f, size / 2.f});
    letter->setScale(size / 60.f);
    container->addChild(letter, 1);

    return container;
}

std::optional<ccColor3B> rankBadgeColor(ModlyUser const& user) {
    if (user.rank == "rojo") return ccColor3B{235, 78, 78};
    if (user.rank == "verde") return ccColor3B{68, 200, 120};
    if (user.verified) return ccColor3B{70, 150, 245};
    return std::nullopt;
}

CCNode* createRankSeal(ModlyUser const& user, float size) {
    auto color = rankBadgeColor(user);
    if (!color) return nullptr;

    auto seal = CCNode::create();
    seal->setContentSize({size, size});
    seal->setAnchorPoint({0.f, 0.5f});

    // bigFont.fnt has no check glyph, so the tick comes from a GD sprite frame
    // and a tinted disc stands in if that frame is ever missing.
    if (auto* check = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png")) {
        check->setColor(*color);
        float scale = size / std::max(check->getContentSize().width, check->getContentSize().height);
        check->setScale(scale);
        check->setPosition({size / 2.f, size / 2.f});
        seal->addChild(check);
        return seal;
    }

    auto dot = paimon::SpriteHelper::createRoundedRect(
        size * 0.7f, size * 0.7f, size * 0.35f,
        {color->r / 255.f, color->g / 255.f, color->b / 255.f, 1.f});
    if (dot) {
        dot->setPosition({size * 0.15f, size * 0.15f});
        seal->addChild(dot);
    }
    return seal;
}

std::string translateTag(std::string const& tag) {
    if (Localization::get().getLanguage() != Localization::Language::ENGLISH) return tag;

    // Keys are the Spanish values Modly stores; the accented ones are matched
    // as raw UTF-8 bytes because that is what arrives from the server.
    static std::unordered_map<std::string, std::string> const translations = {
        {"Desarrollador", "Developer"},
        {"Dise\xC3\xB1" "ador", "Designer"},
        {"Tester", "Tester"},
        {"Creador de niveles", "Level creator"},
        {"Espa\xC3\xB1ol", "Spanish"},
        {"Ingl\xC3\xA9s", "English"},
        {"Portugu\xC3\xA9s", "Portuguese"},
        {"Franc\xC3\xA9s", "French"},
        {"Alem\xC3\xA1n", "German"},
        {"Italiano", "Italian"},
        {"Usa IA", "Uses AI"},
    };

    auto it = translations.find(tag);
    return it == translations.end() ? tag : it->second;
}

CCNode* createPill(std::string const& text, ccColor3B color, float scale) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);

    float padding = 7.f;
    float width = label->getContentSize().width * scale + padding * 2.f;
    float height = label->getContentSize().height * scale + 5.f;

    auto pill = CCNode::create();
    pill->setContentSize({width, height});
    pill->setAnchorPoint({0.f, 0.5f});

    auto bg = paimon::SpriteHelper::createRoundedRect(
        width, height, height / 2.f,
        {color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.85f});
    if (bg) {
        bg->setPosition({0.f, 0.f});
        pill->addChild(bg, 0);
    }

    label->setPosition({width / 2.f, height / 2.f});
    pill->addChild(label, 1);
    return pill;
}

} // namespace paimon::compat_mods
