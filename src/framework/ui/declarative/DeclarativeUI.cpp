#include "DeclarativeUI.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::ui::dec {

static bool has(matjson::Value const& v, std::string_view k) { return v.contains(k); }

static float numF(matjson::Value const& v, float def = 0.f) {
    return v.isNumber() ? static_cast<float>(v.asDouble().unwrapOr(def)) : def;
}

static std::string str(matjson::Value const& v, std::string const& def = "") {
    return v.isString() ? v.asString().unwrapOr(def) : def;
}

static GLubyte channel(matjson::Value const& v, GLubyte def) {
    if (!v.isNumber()) return def;
    int n = static_cast<int>(v.asDouble().unwrapOr(def) + 0.5);
    return static_cast<GLubyte>(std::clamp(n, 0, 255));
}

// Color/opacity helpers for the supported concrete types.
static void setColorAny(CCNode* n, ccColor3B c) {
    if (auto* s = typeinfo_cast<CCSprite*>(n)) { s->setColor(c); return; }
    if (auto* l = typeinfo_cast<CCLabelBMFont*>(n)) { l->setColor(c); return; }
    if (auto* sc = typeinfo_cast<CCScale9Sprite*>(n)) { sc->setColor(c); return; }
    if (auto* lc = typeinfo_cast<CCLayerColor*>(n)) { lc->setColor(c); return; }
}

static void setOpacityAny(CCNode* n, GLubyte o) {
    if (auto* s = typeinfo_cast<CCSprite*>(n)) { s->setOpacity(o); return; }
    if (auto* l = typeinfo_cast<CCLabelBMFont*>(n)) { l->setOpacity(o); return; }
    if (auto* sc = typeinfo_cast<CCScale9Sprite*>(n)) { sc->setOpacity(o); return; }
    if (auto* lc = typeinfo_cast<CCLayerColor*>(n)) { lc->setOpacity(o); return; }
}

// Point of a named anchor within an area of size 's'.
static CCPoint anchorIn(std::string const& a, CCSize s) {
    if (a == "center")        return {s.width * 0.5f, s.height * 0.5f};
    if (a == "top-left")      return {0,              s.height};
    if (a == "top-center")    return {s.width * 0.5f, s.height};
    if (a == "top-right")     return {s.width,        s.height};
    if (a == "center-left")   return {0,              s.height * 0.5f};
    if (a == "center-right")  return {s.width,        s.height * 0.5f};
    if (a == "bottom-left")   return {0,              0};
    if (a == "bottom-center") return {s.width * 0.5f, 0};
    if (a == "bottom-right")  return {s.width,        0};
    return {0, 0};
}

void applyAttributes(CCNode* node, matjson::Value const& a, CCNode* parent) {
    if (!node) return;

    if (has(a, "visible"))    node->setVisible(a["visible"].asBool().unwrapOr(true));
    if (has(a, "rotation"))   node->setRotation(numF(a["rotation"]));
    if (has(a, "z-order"))    node->setZOrder(static_cast<int>(a["z-order"].asInt().unwrapOr(0)));

    if (has(a, "scale")) {
        auto const& s = a["scale"];
        if (s.isObject()) {
            if (has(s, "x")) node->setScaleX(numF(s["x"], 1.f));
            if (has(s, "y")) node->setScaleY(numF(s["y"], 1.f));
        } else {
            node->setScale(numF(s, 1.f));
        }
    }

    if (has(a, "anchor-point")) {
        auto const& p = a["anchor-point"];
        node->setAnchorPoint({numF(p["x"], 0.5f), numF(p["y"], 0.5f)});
    }

    if (has(a, "content-size")) {
        auto const& s = a["content-size"];
        node->setContentSize({numF(s["width"]), numF(s["height"])});
    }

    if (has(a, "color")) {
        auto const& c = a["color"];
        setColorAny(node, {channel(c["r"], 255), channel(c["g"], 255), channel(c["b"], 255)});
    }

    if (has(a, "opacity")) setOpacityAny(node, channel(a["opacity"], 255));

    if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(node)) {
        if (has(a, "font")) lbl->setFntFile(str(a["font"], "bigFont.fnt").c_str());
        if (has(a, "text")) lbl->setString(str(a["text"]).c_str());
    }

    if (has(a, "position")) {
        auto const& p = a["position"];
        float x = numF(p["x"]);
        float y = numF(p["y"]);
        if (has(p, "anchor") && p["anchor"].isString()) {
            CCSize area = parent ? parent->getContentSize() : CCDirector::get()->getWinSize();
            CCPoint base = anchorIn(p["anchor"].asString().unwrapOr("bottom-left"), area);
            node->setPosition({base.x + x, base.y + y});
        } else {
            node->setPosition({x, y});
        }
    }
}

Factory& Factory::get() {
    static Factory inst;
    return inst;
}

void Factory::registerType(std::string_view type, Creator creator) {
    m_creators[std::string(type)] = std::move(creator);
}

void Factory::ensureDefaults() {
    if (m_ready) return;
    m_ready = true;

    registerType("CCNode",  [](matjson::Value const&) -> CCNode* { return CCNode::create(); });
    registerType("CCLayer", [](matjson::Value const&) -> CCNode* { return CCLayer::create(); });
    registerType("CCMenu",  [](matjson::Value const&) -> CCNode* {
        auto* m = CCMenu::create();
        if (m) m->setContentSize({0, 0});
        return m;
    });

    registerType("CCLabelBMFont", [](matjson::Value const& a) -> CCNode* {
        return CCLabelBMFont::create(str(a["text"]).c_str(), str(a["font"], "bigFont.fnt").c_str());
    });

    registerType("CCSprite", [](matjson::Value const& a) -> CCNode* {
        if (has(a, "sprite-frame")) {
            auto name = str(a["sprite-frame"]);
            if (!name.empty()) if (auto* s = CCSprite::createWithSpriteFrameName(name.c_str())) return s;
        }
        if (has(a, "sprite")) {
            auto file = str(a["sprite"]);
            if (!file.empty()) if (auto* s = CCSprite::create(file.c_str())) return s;
        }
        return CCSprite::create();
    });

    registerType("CCScale9Sprite", [](matjson::Value const& a) -> CCNode* {
        return CCScale9Sprite::create(str(a["sprite"], "GJ_square01.png").c_str());
    });

    registerType("CCLayerColor", [](matjson::Value const& a) -> CCNode* {
        ccColor4B col{0, 0, 0, 255};
        if (has(a, "color")) {
            auto const& c = a["color"];
            col = {channel(c["r"], 0), channel(c["g"], 0), channel(c["b"], 0), channel(c["a"], 255)};
        }
        float w = 0, h = 0;
        if (has(a, "content-size")) { w = numF(a["content-size"]["width"]); h = numF(a["content-size"]["height"]); }
        return CCLayerColor::create(col, w, h);
    });
}

CCNode* Factory::create(std::string_view type, matjson::Value const& attrs) {
    ensureDefaults();
    auto it = m_creators.find(std::string(type));
    if (it == m_creators.end()) {
        log::warn("[DeclarativeUI] tipo desconocido: '{}'", type);
        return nullptr;
    }
    return it->second(attrs);
}

Spec Spec::fromJson(matjson::Value const& j) {
    Spec s;
    s.type = str(j["type"], "CCNode");
    s.id   = str(j["id"]);
    if (j.contains("attributes")) s.attributes = j["attributes"];
    if (j.contains("children") && j["children"].isArray()) {
        if (auto arr = j["children"].asArray()) {
            for (auto const& c : arr.unwrap()) {
                s.children.push_back(fromJson(c));
            }
        }
    }
    return s;
}

CCNode* build(Spec const& spec, CCNode* parent) {
    auto* node = Factory::get().create(spec.type, spec.attributes);
    if (!node) return nullptr;

    if (!spec.id.empty()) node->setID(spec.id);
    applyAttributes(node, spec.attributes, parent);

    for (auto const& child : spec.children) {
        build(child, node);
    }

    if (parent) parent->addChild(node);
    return node;
}

CCNode* query(CCNode* root, std::string_view path) {
    if (!root) return nullptr;

    CCNode* cur = root;
    bool direct = false;
    std::string token;

    auto step = [&] {
        if (token.empty() || !cur) return;
        cur = direct ? cur->getChildByID(token) : cur->getChildByIDRecursive(token);
        token.clear();
        direct = false;
    };

    for (char ch : path) {
        if (ch == '>') { step(); direct = true; }
        else if (ch == ' ' || ch == '\t') { step(); }
        else token.push_back(ch);
    }
    step();
    return cur;
}

} // namespace paimon::ui::dec
