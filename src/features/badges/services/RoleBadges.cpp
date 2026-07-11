#include "RoleBadges.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <array>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::badges {

namespace {

struct RoleVisual {
    char const* role;
    char const* badgeId;   // historical, mod-prefixed at runtime
    char const* asset;     // packed sprite name (optional, may be absent)
    char const* glyph;     // pill fallback letter
    ccColor3B color;       // pill fallback color
};

constexpr std::array<RoleVisual, 5> kVisuals = {{
    {"admin",  "paimon-admin-badge",     "paim_Admin.png",     "A",   {230,  64,  64}},
    {"mod",    "paimon-moderator-badge", "paim_Moderador.png", "M",   { 74, 134, 255}},
    {"helper", "paimon-helper-badge",    "paim_Helper.png",    "H",   { 58, 200, 130}},
    {"idea",   "paimon-idea-badge",      "paim_Idea.png",      "i",   {255, 196,  64}},
    {"vip",    "paimon-vip-badge",       "paim_Vip.png",       "V",   {196, 108, 255}},
}};

RoleVisual const* visualFor(std::string const& role) {
    for (auto const& v : kVisuals) {
        if (role == v.role) return &v;
    }
    return nullptr;
}

CCNode* buildPill(RoleVisual const& v, float targetHeight) {
    auto label = CCLabelBMFont::create(v.glyph, "bigFont.fnt");
    if (!label) return nullptr;

    float labelTarget = targetHeight * 0.6f;
    float lh = std::max(1.f, label->getContentSize().height);
    label->setScale(labelTarget / lh);

    float pad = targetHeight * 0.5f;
    float w = label->getScaledContentSize().width + pad * 2.f;
    float h = targetHeight;

    auto container = CCNode::create();
    container->setContentSize({w, h});
    container->setAnchorPoint({0.5f, 0.5f});

    auto panel = paimon::SpriteHelper::createColorPanel(w, h, v.color, 255, h * 0.42f);
    if (panel) {
        panel->setPosition({0.f, 0.f});
        container->addChild(panel);
    }

    label->setPosition({w / 2.f, h / 2.f});
    container->addChild(label);
    return container;
}

} // namespace

std::string roleBadgeId(std::string const& roleId) {
    auto const* v = visualFor(roleId);
    char const* base = v ? v->badgeId : "paimon-unknown-badge";
    return Mod::get()->expandSpriteName(base);
}

CCNode* createRoleBadgeNode(std::string const& roleId, float targetHeight) {
    auto const* v = visualFor(roleId);
    if (!v) return nullptr;

    // Prefer real art if it ships with the mod.
    std::string assetName = Mod::get()->expandSpriteName(v->asset);
    if (auto* spr = paimon::SpriteHelper::safeCreate(assetName.c_str())) {
        float sh = std::max(1.f, spr->getContentSize().height);
        spr->setScale(targetHeight / sh);
        return spr;
    }

    return buildPill(*v, targetHeight);
}

void applyRoleBadges(
    CCMenu* menu,
    paimon::roles::UserRoles const& roles,
    CCObject* target,
    SEL_MenuHandler onTap,
    float targetHeight,
    CCNode* insertBefore
) {
    if (!menu) return;

    std::vector<std::string> toShow;
    if (roles.admin)      toShow.push_back("admin");
    else if (roles.mod)   toShow.push_back("mod");
    if (roles.helper)     toShow.push_back("helper");
    if (roles.idea)       toShow.push_back("idea");
    if (roles.vip && !roles.mod && !roles.admin) toShow.push_back("vip");

    bool changed = false;
    for (auto const& role : toShow) {
        std::string id = roleBadgeId(role);
        if (menu->getChildByID(id)) continue;

        auto node = createRoleBadgeNode(role, targetHeight);
        if (!node) continue;

        auto btn = CCMenuItemSpriteExtra::create(node, target, onTap);
        if (!btn) continue;
        btn->setID(id);

        if (insertBefore && insertBefore->getParent() == menu) {
            menu->insertBefore(btn, insertBefore);
        } else {
            menu->addChild(btn);
        }
        changed = true;
    }

    if (changed) menu->updateLayout();
}

void showRoleBadgeInfoPopup(CCNode* sender) {
    if (!sender) return;

    std::string id = sender->getID();
    std::string role;
    for (auto const& v : kVisuals) {
        if (id == Mod::get()->expandSpriteName(v.badgeId)) { role = v.role; break; }
    }
    if (role.empty()) return;

    auto& loc = Localization::get();
    std::string title = loc.getString("badge." + role + ".title");
    std::string desc  = loc.getString("badge." + role + ".desc");

    PopupManager::get().alert(title, desc).showInstant();
}

} // namespace paimon::badges
