// Custom editor grid color (solid or gradient) and toolbar tint.
//
// Grid color: vanilla DrawGridLayer::draw sets its own line colors internally,
// so a pre-draw ccDrawColor4B is overwritten. Instead we draw a colored grid
// overlay after the vanilla draw, aligned to the current grid size, using the
// same immediate-mode pattern as the trigger-line overlays (proven to align
// with level coordinates). This is the low-risk way to recolor the grid without
// rewriting DrawGridLayer::draw.

#include "../EditorModule.hpp"

#include <Geode/binding/DrawGridLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/DrawGridLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

bool gridOn() { return moduleEnabled("editor-mod-grid-colors"); }
bool barOn() { return moduleEnabled("editor-mod-toolbar-colors"); }

// Parse an "r,g,b" string setting; falls back to def on any parse failure.
ccColor3B parseColor(std::string const& s, ccColor3B def) {
    int comp[3];
    int idx = 0;
    bool hasDigit = false;
    int cur = 0;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            cur = cur * 10 + (ch - '0');
            hasDigit = true;
        } else if (ch == ',') {
            if (idx < 3) comp[idx] = cur;
            ++idx;
            cur = 0;
        }
    }
    if (idx < 3) comp[idx] = cur;
    if (!hasDigit || idx < 2) return def;
    return {
        static_cast<GLubyte>(std::clamp(comp[0], 0, 255)),
        static_cast<GLubyte>(std::clamp(comp[1], 0, 255)),
        static_cast<GLubyte>(std::clamp(comp[2], 0, 255)),
    };
}

ccColor3B gridColor(char const* key, ccColor3B def) {
    return parseColor(moduleSetting<std::string>(key, ""), def);
}

ccColor3B legacyToolbarColor(char const* key, ccColor3B def) {
    return parseColor(Mod::get()->getSavedValue<std::string>(key, ""), def);
}

GLubyte lerpByte(GLubyte a, GLubyte b, float t) {
    return static_cast<GLubyte>(std::lround(a + (static_cast<int>(b) - a) * t));
}

// Cap so extreme zoom-out never floods the GPU with lines (vanilla hides the
// grid when too dense anyway).
constexpr int kMaxGridLines = 1500;

} // namespace

class $modify(PaimonGridColorDraw, DrawGridLayer) {
    void drawCustomGrid() {
        auto* ol = m_objectLayer;
        if (!ol) return;

        auto win = CCDirector::get()->getWinSize();
        CCPoint bl = ol->convertToNodeSpace({0.f, 0.f});
        CCPoint tr = ol->convertToNodeSpace({win.width, win.height});
        float minX = std::min(bl.x, tr.x), maxX = std::max(bl.x, tr.x);
        float minY = std::min(bl.y, tr.y), maxY = std::max(bl.y, tr.y);
        if (maxX <= minX || maxY <= minY) return;

        float gs = m_gridSize > 0.1f ? m_gridSize : 30.f;
        int vCount = static_cast<int>((maxX - minX) / gs) + 2;
        int hCount = static_cast<int>((maxY - minY) / gs) + 2;
        if (vCount + hCount > kMaxGridLines) return;

        ccColor3B colA = gridColor("editor-mod-grid-color-a", {0, 200, 255});
        bool gradient = moduleSetting<bool>("editor-mod-grid-gradient", false);
        ccColor3B colB = gradient ? gridColor("editor-mod-grid-color-b", {255, 60, 200}) : colA;
        auto alpha = static_cast<GLubyte>(std::clamp<int64_t>(
            moduleSetting<int64_t>("editor-mod-grid-alpha", 90), 0, 255));
        auto width = static_cast<float>(std::clamp<double>(
            moduleSetting<double>("editor-mod-grid-line-width", 1.0), 0.5, 4.0));

        auto colorAt = [&](float t) -> ccColor4B {
            if (!gradient) return {colA.r, colA.g, colA.b, alpha};
            t = std::clamp(t, 0.f, 1.f);
            return {lerpByte(colA.r, colB.r, t), lerpByte(colA.g, colB.g, t),
                    lerpByte(colA.b, colB.b, t), alpha};
        };

        float spanX = maxX - minX, spanY = maxY - minY;
        glLineWidth(width);

        float startX = std::floor(minX / gs) * gs;
        for (float x = startX; x <= maxX; x += gs) {
            auto c = colorAt((x - minX) / spanX);
            ccDrawColor4B(c.r, c.g, c.b, c.a);
            ccDrawLine({x, minY}, {x, maxY});
        }

        float startY = std::floor(minY / gs) * gs;
        for (float y = startY; y <= maxY; y += gs) {
            auto c = colorAt((y - minY) / spanY);
            ccDrawColor4B(c.r, c.g, c.b, c.a);
            ccDrawLine({minX, y}, {maxX, y});
        }
    }

    $override
    void draw() {
        DrawGridLayer::draw();
        if (gridOn()) drawCustomGrid();
    }
};

class $modify(PaimonToolbarColorUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void tintToolbar() {
        if (!barOn()) return;
        auto col = legacyToolbarColor("paim-toolbar-color", {40, 40, 55});
        for (char const* id : {
                 "toolbar-bg", "build-tabs-menu", "editor-buttons-menu",
                 "undo-menu", "playback-menu", "settings-menu"
             }) {
            if (auto* n = this->getChildByID(id)) {
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(n)) {
                    rgba->setColor(col);
                }
                for (auto* ch : CCArrayExt<CCNode*>(n->getChildren())) {
                    if (auto* s9 = typeinfo_cast<CCScale9Sprite*>(ch)) {
                        s9->setColor(col);
                    } else if (auto* sp = typeinfo_cast<CCSprite*>(ch)) {
                        if (sp->getContentSize().width > 80.f) sp->setColor(col);
                    }
                }
            }
        }
        auto win = CCDirector::get()->getWinSize();
        for (auto* ch : CCArrayExt<CCNode*>(this->getChildren())) {
            if (!ch) continue;
            auto* s9 = typeinfo_cast<CCScale9Sprite*>(ch);
            if (!s9) continue;
            auto p = ch->getPosition();
            if (p.y < win.height * 0.35f && ch->getContentSize().width > win.width * 0.4f) {
                s9->setColor(col);
            }
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (barOn()) {
            Loader::get()->queueInMainThread([self = Ref(this)] {
                if (self) static_cast<PaimonToolbarColorUI*>(self.data())->tintToolbar();
            });
        }
        if (Mod::get()->getSavedValue<std::string>("paim-toolbar-color", "").empty()) {
            Mod::get()->setSavedValue<std::string>("paim-toolbar-color", "35,35,50");
        }
        return true;
    }
};
