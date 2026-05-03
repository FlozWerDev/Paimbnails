#include "ProgressBarEditOverlay.hpp"
#include "../services/ProgressBarManager.hpp"
#include "../../fonts/ui/FontPickerPopup.hpp"
#include "../../fonts/FontTag.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
struct DetachedEntry {
    geode::Ref<cocos2d::CCNode> node;   // we own the temporarily-detached node
    cocos2d::CCNode*            parent = nullptr; // PlayLayer parent (do NOT retain)
    int         zOrder = 0;
};
std::vector<DetachedEntry> s_detached;
Ref<ProgressBarEditOverlay> s_activeOverlay;

CCNode* findProgressBar(CCNode* root) {
    return root ? root->getChildByIDRecursive("progress-bar") : nullptr;
}
CCNode* findPercentageLabel(CCNode* root) {
    return root ? root->getChildByIDRecursive("percentage-label") : nullptr;
}
CCPoint worldPos(CCNode* n) {
    if (!n || !n->getParent()) return {0, 0};
    return n->getParent()->convertToWorldSpace(n->getPosition());
}

// Native GD button sprite used as a draggable handle or tap button.
CCNode* makeButtonHandle(const char* label, const char* bg, float btnScale) {
    auto* spr = ButtonSprite::create(label, "goldFont.fnt", bg, 0.7f);
    spr->setScale(btnScale);
    return spr;
}

// Subtle dashed outline around the selected element.
CCNode* makeSelectionOutline(CCRect const& r) {
    auto* node = CCDrawNode::create();
    ccColor4F line = {1.f, 1.f, 1.f, 0.45f};
    node->drawSegment({r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMinY()}, 1.0f, line);
    node->drawSegment({r.getMaxX(), r.getMinY()}, {r.getMaxX(), r.getMaxY()}, 1.0f, line);
    node->drawSegment({r.getMaxX(), r.getMaxY()}, {r.getMinX(), r.getMaxY()}, 1.0f, line);
    node->drawSegment({r.getMinX(), r.getMaxY()}, {r.getMinX(), r.getMinY()}, 1.0f, line);
    return node;
}
float angleDeg(float dx, float dy) {
    float a = std::atan2(dy, dx) * 180.f / float(M_PI);
    if (a < 0) a += 360.f;
    return a;
}
} // namespace

// ─────────────────────────────────────────────────────────────
ProgressBarEditOverlay* ProgressBarEditOverlay::create() {
    auto* ret = new ProgressBarEditOverlay();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

bool ProgressBarEditOverlay::init() {
    if (!CCLayer::init()) return false;
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(winSize);
    this->setID("paimon-progressbar-edit-overlay"_spr);
    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    m_selContainer = CCNode::create();
    m_selContainer->setAnchorPoint({0, 0});
    m_selContainer->setContentSize(winSize);
    this->addChild(m_selContainer, 50);

    buildToolbar();

    // Drive handle repositioning via the scheduler so the handles
    // stay glued to the bar even while the game is paused.
    this->scheduleUpdate();
    return true;
}

void ProgressBarEditOverlay::buildToolbar() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    this->addChild(menu, 100);

    auto makeBtn = [&](char const* label, SEL_MenuHandler cb, float x,
                       char const* bg = "GJ_button_01.png") {
        auto spr = ButtonSprite::create(label, "goldFont.fnt", bg, 0.7f);
        spr->setScale(0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setPosition({x, winSize.height - 22.f});
        menu->addChild(btn);
    };

    float cx = winSize.width / 2.f;
    makeBtn("Done",
        menu_selector(ProgressBarEditOverlay::onDone), cx - 160.f,
        "GJ_button_02.png");          // green = positive
    makeBtn("+ Add",
        menu_selector(ProgressBarEditOverlay::onAddImage), cx - 60.f,
        "GJ_button_01.png");          // blue = create
    makeBtn("Font",
        menu_selector(ProgressBarEditOverlay::onFont), cx + 40.f,
        "GJ_button_01.png");          // blue = neutral
    makeBtn("Reset",
        menu_selector(ProgressBarEditOverlay::onResetPosition), cx + 140.f,
        "GJ_button_05.png");          // red = destructive

    m_hintLabel = CCLabelBMFont::create(
        "Drag body=Move    Green dots=Resize    Center circle=Rotate    Red dot=Delete",
        "chatFont.fnt");
    m_hintLabel->setScale(0.4f);
    m_hintLabel->setAlignment(kCCTextAlignmentCenter);
    m_hintLabel->setPosition({cx, 18.f});
    m_hintLabel->setOpacity(220);
    this->addChild(m_hintLabel, 100);
}

void ProgressBarEditOverlay::clearSelectionUI() {
    if (m_selContainer) m_selContainer->removeAllChildren();
}

// Helper to add a native GD button handle to the selection container.
// tag meanings: 1=Scale  2=Rotate  3=Delete  4=OpMinus  5=OpPlus
static void addSelBtn(CCNode* container, CCPoint w, int tag,
                      const char* label, const char* bg, float scale) {
    auto* btn = makeButtonHandle(label, bg, scale);
    btn->setPosition(w);
    btn->setTag(tag);
    container->addChild(btn);
}

void ProgressBarEditOverlay::rebuildSelectionUI() {
    clearSelectionUI();
    auto* pl = PlayLayer::get();
    if (!pl || m_selectedTarget == Target::None) return;

    auto aabb = [&](CCNode* n) {
        auto bb = n->boundingBox();
        auto bl = n->getParent()->convertToWorldSpace(ccp(bb.getMinX(), bb.getMinY()));
        auto tr = n->getParent()->convertToWorldSpace(ccp(bb.getMaxX(), bb.getMaxY()));
        float x0 = std::min(bl.x, tr.x), x1 = std::max(bl.x, tr.x);
        float y0 = std::min(bl.y, tr.y), y1 = std::max(bl.y, tr.y);
        return CCRect(x0, y0, x1 - x0, y1 - y0);
    };

    auto& cfg = ProgressBarManager::get().config();

    // Pick the selected node.
    CCNode* selNode = nullptr;
    if (m_selectedTarget == Target::Bar) selNode = findProgressBar(pl);
    else if (m_selectedTarget == Target::Label) selNode = findPercentageLabel(pl);
    else if (m_selectedTarget == Target::Decoration)
        selNode = ProgressBarManager::get().getDecorationNode(m_selectedDecoIndex);
    if (!selNode || !selNode->getParent()) return;

    auto r = aabb(selNode);
    m_selRect = r;
    float cx = r.getMidX(), cy = r.getMidY();

    // White outline around selection.
    m_selContainer->addChild(makeSelectionOutline(r));

    // ── Scale handle (bottom-right corner) ──
    addSelBtn(m_selContainer, {r.getMaxX(), r.getMinY()}, 1,
              "S", "GJ_button_01.png", 0.35f);

    // ── Rotate handle (top edge, centred) ──
    addSelBtn(m_selContainer, {cx, r.getMaxY() + 26.f}, 2,
              "R", "GJ_button_02.png", 0.35f);

    // ── Decoration extras ──
    if (m_selectedTarget == Target::Decoration) {
        // Delete (top-right, outside).
        addSelBtn(m_selContainer,
                  {r.getMaxX() + 22.f, r.getMaxY() + 22.f}, 3,
                  "X", "GJ_button_05.png", 0.30f);
    }

    // ── Opacity handles (Bar & Label only) ──
    if (m_selectedTarget == Target::Bar || m_selectedTarget == Target::Label) {
        addSelBtn(m_selContainer,
                  {r.getMinX() - 18.f, r.getMinY()}, 4,
                  "-", "GJ_button_06.png", 0.30f);
        addSelBtn(m_selContainer,
                  {r.getMinX() - 42.f, r.getMinY()}, 5,
                  "+", "GJ_button_02.png", 0.30f);
    }
}

void ProgressBarEditOverlay::update(float) {
    // Rebuild selection UI every frame (unless dragging) so buttons
    // stay glued to the selected element.
    if (m_dragAction == Action::None) rebuildSelectionUI();
}

// ── Touch handling ──────────────────────────────────────────
void ProgressBarEditOverlay::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()
        ->addTargetedDelegate(this, -INT_MAX + 100, true);
}

bool ProgressBarEditOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto pos = touch->getLocation();
    auto& cfg = ProgressBarManager::get().config();

    // ── 1) Hit-test native GD button handles around current selection ──
    if (m_selContainer && m_selectedTarget != Target::None) {
        const float kHit = 24.f;
        if (auto* children = m_selContainer->getChildren()) {
            for (auto* c : CCArrayExt<CCNode*>(children)) {
                if (!c || c->getTag() == 0) continue; // outline has tag 0
                CCPoint w = c->getPosition();
                float dx = pos.x - w.x, dy = pos.y - w.y;
                if (dx * dx + dy * dy > kHit * kHit) continue;

                int tag = c->getTag();
                if (tag == 3) {               // Delete decoration
                    ProgressBarManager::get().removeDecoration(m_selectedDecoIndex);
                    m_selectedTarget = Target::None;
                    m_selectedDecoIndex = -1;
                    rebuildSelectionUI();
                    return true;
                }
                if (tag == 4) {             // Opacity -
                    cfg.opacity = std::clamp(cfg.opacity - 15, 0, 255);
                    ProgressBarManager::get().saveConfig();
                    rebuildSelectionUI();
                    return true;
                }
                if (tag == 5) {             // Opacity +
                    cfg.opacity = std::clamp(cfg.opacity + 15, 0, 255);
                    ProgressBarManager::get().saveConfig();
                    rebuildSelectionUI();
                    return true;
                }
                if (tag == 1) {             // Scale handle
                    m_dragTarget = m_selectedTarget;
                    m_dragAction = Action::ResizeUniform;
                    m_dragDecoIndex = m_selectedDecoIndex;
                    m_touchStart = pos;
                    storeOrigValues();
                    return true;
                }
                if (tag == 2) {             // Rotate handle
                    m_dragTarget = m_selectedTarget;
                    m_dragAction = Action::Rotate;
                    m_dragDecoIndex = m_selectedDecoIndex;
                    m_touchStart = pos;
                    storeOrigValues();
                    return true;
                }
            }
        }
    }

    // ── 2) Hit-test element bodies to SELECT + start Move drag ──
    auto* pl = PlayLayer::get();
    if (!pl) return false;
    auto tryHitBody = [&](CCNode* n, Target t, int decoIdx = -1) {
        if (!n) return false;
        auto bb = n->boundingBox();
        auto bl = n->getParent()->convertToWorldSpace(ccp(bb.getMinX(), bb.getMinY()));
        auto tr = n->getParent()->convertToWorldSpace(ccp(bb.getMaxX(), bb.getMaxY()));
        float x0 = std::min(bl.x, tr.x), x1 = std::max(bl.x, tr.x);
        float y0 = std::min(bl.y, tr.y), y1 = std::max(bl.y, tr.y);
        // Ensure minimum 24x24 hit box around centre so small nodes work.
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        if (x1 - x0 < 24.f) { x0 = cx - 12.f; x1 = cx + 12.f; }
        if (y1 - y0 < 24.f) { y0 = cy - 12.f; y1 = cy + 12.f; }
        CCRect r(x0, y0, x1 - x0, y1 - y0);
        if (!r.containsPoint(pos)) return false;

        // Select this element and start a Move drag.
        m_selectedTarget = t;
        m_selectedDecoIndex = decoIdx;
        rebuildSelectionUI();

        m_dragTarget = t; m_dragAction = Action::Move; m_dragDecoIndex = decoIdx;
        m_touchStart = pos;
        if (t == Target::Bar) {
            if (!cfg.useCustomPosition) {
                cfg.useCustomPosition = true;
                auto w = worldPos(n);
                cfg.posX = w.x; cfg.posY = w.y;
            }
            m_origPos = ccp(cfg.posX, cfg.posY);
        } else if (t == Target::Label) {
            if (!cfg.useCustomLabelPosition) {
                cfg.useCustomLabelPosition = true;
                auto w = worldPos(n);
                cfg.labelPosX = w.x; cfg.labelPosY = w.y;
            }
            m_origPos = ccp(cfg.labelPosX, cfg.labelPosY);
        } else if (t == Target::Decoration && decoIdx >= 0) {
            if (decoIdx >= static_cast<int>(cfg.decorations.size())) return false;
            m_origPos = ccp(cfg.decorations[decoIdx].posX,
                            cfg.decorations[decoIdx].posY);
        }
        m_anchorWorld = m_origPos;
        return true;
    };
    auto& mgr = ProgressBarManager::get();
    auto const& decs = mgr.config().decorations;
    // Check decorations first (top-most), then label, then bar.
    for (int i = static_cast<int>(decs.size()) - 1; i >= 0; --i) {
        if (tryHitBody(mgr.getDecorationNode(i), Target::Decoration, i))
            return true;
    }
    if (tryHitBody(findPercentageLabel(pl), Target::Label)) return true;
    if (tryHitBody(findProgressBar(pl), Target::Bar)) return true;

    // Tapped empty space → deselect.
    m_selectedTarget = Target::None;
    m_selectedDecoIndex = -1;
    rebuildSelectionUI();
    return false;
}

void ProgressBarEditOverlay::storeOrigValues() {
    auto& cfg = ProgressBarManager::get().config();
    auto* pl = PlayLayer::get();
    if (m_dragTarget == Target::Bar) {
        auto* bar = pl ? findProgressBar(pl) : nullptr;
        CCPoint w = worldPos(bar);
        if (!cfg.useCustomPosition) {
            cfg.useCustomPosition = true;
            cfg.posX = w.x; cfg.posY = w.y;
        }
        m_origPos = ccp(cfg.posX, cfg.posY);
        m_anchorWorld = m_origPos;
        m_origScaleLen = cfg.scaleLength;
        m_origScaleThick = cfg.scaleThickness;
        m_origRotation = cfg.userRotation;
    } else if (m_dragTarget == Target::Label) {
        auto* lb = pl ? findPercentageLabel(pl) : nullptr;
        CCPoint w = worldPos(lb);
        if (!cfg.useCustomLabelPosition) {
            cfg.useCustomLabelPosition = true;
            cfg.labelPosX = w.x; cfg.labelPosY = w.y;
        }
        m_origPos = ccp(cfg.labelPosX, cfg.labelPosY);
        m_anchorWorld = m_origPos;
        m_origUniformSc = cfg.percentageScale;
    } else if (m_dragTarget == Target::Decoration) {
        if (m_dragDecoIndex < 0 ||
            m_dragDecoIndex >= static_cast<int>(cfg.decorations.size())) return;
        auto const& d = cfg.decorations[m_dragDecoIndex];
        m_origPos = ccp(d.posX, d.posY);
        m_anchorWorld = m_origPos;
        m_origUniformSc = d.scale;
        m_origRotation = d.rotation;
    }
}

void ProgressBarEditOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_dragAction == Action::None) return;
    auto pos = touch->getLocation();
    auto delta = pos - m_touchStart;
    auto& cfg = ProgressBarManager::get().config();

    auto rotationDelta = [&]() {
        float a0 = angleDeg(m_touchStart.x - m_anchorWorld.x,
                            m_touchStart.y - m_anchorWorld.y);
        float a1 = angleDeg(pos.x - m_anchorWorld.x,
                            pos.y - m_anchorWorld.y);
        return a1 - a0;
    };

    if (m_dragTarget == Target::Bar) {
        switch (m_dragAction) {
            case Action::Move:
                cfg.posX = m_origPos.x + delta.x;
                cfg.posY = m_origPos.y + delta.y;
                break;
            case Action::ResizeUniform: {
                float a = cfg.vertical ? delta.y : delta.x;
                float sc = std::clamp(m_origScaleLen + a / 150.f, 0.1f, 5.f);
                cfg.scaleLength = sc;
                cfg.scaleThickness = sc;
                break;
            }
            case Action::Rotate:
                cfg.userRotation = m_origRotation - rotationDelta();
                break;
            default: break;
        }
    } else if (m_dragTarget == Target::Label) {
        switch (m_dragAction) {
            case Action::Move:
                cfg.labelPosX = m_origPos.x + delta.x;
                cfg.labelPosY = m_origPos.y + delta.y;
                break;
            case Action::ResizeUniform:
                cfg.percentageScale = std::clamp(
                    m_origUniformSc + delta.x / 150.f, 0.2f, 5.f);
                break;
            case Action::Rotate:
                // Percentage label rotation isn't persisted in config yet.
                break;
            default: break;
        }
    } else if (m_dragTarget == Target::Decoration) {
        if (m_dragDecoIndex < 0 ||
            m_dragDecoIndex >= static_cast<int>(cfg.decorations.size())) return;
        auto& d = cfg.decorations[m_dragDecoIndex];
        switch (m_dragAction) {
            case Action::Move:
                d.posX = m_origPos.x + delta.x;
                d.posY = m_origPos.y + delta.y;
                break;
            case Action::ResizeUniform: {
                // Distance-from-anchor ratio feels more natural for scale.
                float startD = std::hypot(m_touchStart.x - m_anchorWorld.x,
                                          m_touchStart.y - m_anchorWorld.y);
                float nowD   = std::hypot(pos.x - m_anchorWorld.x,
                                          pos.y - m_anchorWorld.y);
                if (startD > 1.f) {
                    d.scale = std::clamp(m_origUniformSc * (nowD / startD),
                                         0.05f, 8.f);
                }
                break;
            }
            case Action::Rotate:
                d.rotation = m_origRotation - rotationDelta();
                break;
            default: break;
        }
    }
}

void ProgressBarEditOverlay::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_dragAction != Action::None) ProgressBarManager::get().saveConfig();
    m_dragAction = Action::None;
    m_dragTarget = Target::None;
    m_dragDecoIndex = -1;
}

void ProgressBarEditOverlay::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_dragAction = Action::None;
    m_dragTarget = Target::None;
    m_dragDecoIndex = -1;
}

void ProgressBarEditOverlay::keyBackClicked() {
    ProgressBarEditOverlay::exitEditMode();
}

// ── Toolbar callbacks ───────────────────────────────────────
void ProgressBarEditOverlay::onDone(CCObject*) {
    ProgressBarEditOverlay::exitEditMode();
}

void ProgressBarEditOverlay::onResetPosition(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    cfg.useCustomPosition = false;
    cfg.useCustomLabelPosition = false;
    cfg.scaleLength = 1.f;
    cfg.scaleThickness = 1.f;
    cfg.percentageScale = 1.f;
    cfg.percentageOffsetX = 0.f;
    cfg.percentageOffsetY = 0.f;
    cfg.labelPosX = 0.f; cfg.labelPosY = 0.f;
    cfg.posX = 0.f; cfg.posY = 0.f;
    cfg.userRotation = 0.f;
    ProgressBarManager::get().saveConfig();
    rebuildSelectionUI();
    PaimonNotify::create("Position reset", NotificationIcon::Info)->show();
}

void ProgressBarEditOverlay::onFont(CCObject*) {
    auto* picker = paimon::fonts::FontPickerPopup::create(
        [](std::string const& fontTag) {
            auto res = paimon::fonts::parseFontTag(fontTag);
            std::string fntFile = res.hasTag ? res.fontFile : std::string("bigFont.fnt");
            ProgressBarManager::get().config().percentageFont = fntFile;
            ProgressBarManager::get().saveConfig();
            PaimonNotify::create("Font: " + fntFile, NotificationIcon::Success)->show();
        }
    );
    if (picker) picker->show();
}

void ProgressBarEditOverlay::onAddImage(CCObject*) {
    pt::pickImage([](geode::Result<std::optional<std::filesystem::path>> result) {
        auto opt = std::move(result).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;

        auto imported = paimon::assets::importToBucket(*opt, "progressbar_decorations", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import image", NotificationIcon::Error)->show();
            return;
        }

        BarDecoration d;
        d.path = paimon::assets::normalizePathString(imported.path);
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        d.posX = winSize.width / 2.f;
        d.posY = winSize.height / 2.f;
        d.scale = 1.f;
        d.rotation = 0.f;

        int idx = ProgressBarManager::get().addDecoration(d);
        // Make sure the feature is enabled so the decoration actually
        // renders.
        auto& cfg = ProgressBarManager::get().config();
        if (!cfg.enabled) {
            cfg.enabled = true;
            ProgressBarManager::get().saveConfig();
        }
        PaimonNotify::create(
            fmt::format("Image added (#{})", idx + 1),
            NotificationIcon::Success)->show();
    });
}

// ── Enter / exit ────────────────────────────────────────────
bool ProgressBarEditOverlay::isActive() { return s_activeOverlay != nullptr; }

static void detachNode(CCNode* node) {
    if (!node) return;
    auto* parent = node->getParent();
    if (!parent) return;
    DetachedEntry e;
    e.node = node; e.parent = parent; e.zOrder = node->getZOrder();
    node->removeFromParentAndCleanup(false);
    s_detached.push_back(std::move(e));
}

void ProgressBarEditOverlay::enterEditMode() {
    if (s_activeOverlay) return;
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    s_detached.clear();

    std::vector<CCNode*> toDetach;
    if (auto* children = scene->getChildren()) {
        for (auto* obj : CCArrayExt<CCNode*>(children)) {
            if (!obj) continue;
            if (typeinfo_cast<PlayLayer*>(obj)) continue;
            toDetach.push_back(obj);
        }
    }
    for (auto* n : toDetach) detachNode(n);

    // Also hide pause / popup nodes that happen to live inside the PlayLayer.
    if (auto* pl = PlayLayer::get()) {
        std::vector<CCNode*> plToDetach;
        if (auto* cs = pl->getChildren()) {
            for (auto* obj : CCArrayExt<CCNode*>(cs)) {
                if (!obj) continue;
                std::string name = typeid(*obj).name();
                if (name.find("PauseLayer")   != std::string::npos ||
                    name.find("Popup")        != std::string::npos ||
                    name.find("FLAlertLayer") != std::string::npos) {
                    plToDetach.push_back(obj);
                }
            }
        }
        for (auto* n : plToDetach) detachNode(n);
    }

    s_activeOverlay = ProgressBarEditOverlay::create();
    if (!s_activeOverlay) return;
    scene->addChild(s_activeOverlay.data(), INT_MAX - 1);
}

void ProgressBarEditOverlay::exitEditMode() {
    if (s_activeOverlay) {
        s_activeOverlay->removeFromParent();
        s_activeOverlay = nullptr;
    }
    for (auto it = s_detached.rbegin(); it != s_detached.rend(); ++it) {
        if (it->node && it->parent && !it->node->getParent()) {
            it->parent->addChild(it->node.data(), it->zOrder);
        }
    }
    s_detached.clear();
}
