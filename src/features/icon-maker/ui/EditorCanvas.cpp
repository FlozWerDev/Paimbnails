#include "EditorCanvas.hpp"

#include "IconMakerUI.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// below this a gesture can still settle as a tap.
constexpr float kTapSlop = 5.f;

constexpr float kHandleHit = 12.f;
constexpr float kHandleDot = 4.2f;
constexpr float kRotateGap = 18.f;
constexpr float kSnapCanvas = 3.5f;
constexpr float kMinZoom = 0.5f;
constexpr float kMaxZoom = 8.f;

constexpr ccColor4F kBoxColor  {0.42f, 0.80f, 1.00f, 0.95f};
constexpr ccColor4F kLockColor {0.62f, 0.66f, 0.74f, 0.85f};
constexpr ccColor4F kSnapColor {1.00f, 0.42f, 0.82f, 0.80f};

}  // namespace

EditorCanvas* EditorCanvas::create(float side, Callbacks callbacks) {
    auto* ret = new EditorCanvas();
    if (ret->init(side, std::move(callbacks))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool EditorCanvas::init(float side, Callbacks callbacks) {
    if (!CCLayer::init()) return false;
    m_side = side;
    m_cb = std::move(callbacks);

    setContentSize({side, side});
    setAnchorPoint({0.5f, 0.5f});
    ignoreAnchorPointForPosition(false);

    m_flatBg = CCLayerColor::create(ccc4(18, 26, 52, 255));
    m_flatBg->setContentSize({side, side});
    addChild(m_flatBg, -4);

    if (auto* checker = mkui::checkerTexture()) {
        m_checkerBg = CCSprite::createWithTexture(checker);
        if (m_checkerBg) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT};
            checker->setTexParameters(&params);
            m_checkerBg->setTextureRect({0.f, 0.f, side, side});
            m_checkerBg->setAnchorPoint({0.f, 0.f});
            m_checkerBg->setPosition({0.f, 0.f});
            m_checkerBg->setVisible(false);
            addChild(m_checkerBg, -3);
        }
    }

    // drawing is clipped to the frame, else zooming spills over side panels.
    auto* stencil = CCLayerColor::create({255, 255, 255, 255});
    stencil->setContentSize({side, side});
    auto* clip = CCClippingNode::create(stencil);
    clip->setAlphaThreshold(0.05f);
    clip->setContentSize({side, side});
    addChild(clip, 1);

    m_world = CCNode::create();
    m_world->setContentSize({side, side});
    clip->addChild(m_world);

    // art inside this box renders at the size the game uses.
    m_guide = CCLayerColor::create(ccc4(255, 255, 255, 16));
    float const guideSide = side * 0.5f;
    m_guide->setContentSize({guideSide, guideSide});
    m_guide->setPosition({(side - guideSide) / 2.f, (side - guideSide) / 2.f});
    m_world->addChild(m_guide, -1);

    m_overlay = PaimonDrawNode::create();
    if (m_overlay) addChild(m_overlay, 6);

    buildZoomControls();

    setTouchEnabled(true);
    setTouchMode(kCCTouchesOneByOne);
    applyView();
    return true;
}

void EditorCanvas::buildZoomControls() {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    addChild(menu, 7);

    float x = 6.f;
    constexpr float kBtnH = 15.f;
    float const cy = 6.f + kBtnH / 2.f;

    auto addButton = [&](char const* text, float width, std::function<void()> action) {
        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({width, kBtnH});

        if (auto* plate = paimon::SpriteHelper::createColorPanel(
                width, kBtnH, {8, 14, 32}, 190, 3.f)) {
            plate->setAnchorPoint({0.f, 0.f});
            holder->addChild(plate, -1);
        }
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setAnchorPoint({0.5f, 0.5f});
        label->limitLabelWidth(width - 5.f, 0.34f, 0.12f);
        label->setPosition({width / 2.f, kBtnH / 2.f});
        holder->addChild(label);

        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [action](CCMenuItemSpriteExtra*) { if (action) action(); });
        btn->setPosition({x + width / 2.f, cy});
        menu->addChild(btn);
        x += width + 3.f;
        return label;
    };

    addButton("-", 16.f, [this] { this->nudgeZoom(1.f / 1.25f); });

    auto* zoomHolder = CCNode::create();
    zoomHolder->setAnchorPoint({0.5f, 0.5f});
    zoomHolder->setContentSize({34.f, kBtnH});
    if (auto* plate = paimon::SpriteHelper::createColorPanel(
            34.f, kBtnH, {8, 14, 32}, 190, 3.f)) {
        plate->setAnchorPoint({0.f, 0.f});
        zoomHolder->addChild(plate, -1);
    }
    m_zoomLabel = CCLabelBMFont::create("100%", "bigFont.fnt");
    m_zoomLabel->setAnchorPoint({0.5f, 0.5f});
    m_zoomLabel->setScale(0.3f);
    m_zoomLabel->setPosition({17.f, kBtnH / 2.f});
    zoomHolder->addChild(m_zoomLabel);
    zoomHolder->setPosition({x + 17.f, cy});
    addChild(zoomHolder, 7);
    x += 37.f;

    addButton("+", 16.f, [this] { this->nudgeZoom(1.25f); });
    addButton("Ajustar", 40.f, [this] { this->resetView(); });
}

void EditorCanvas::setZones(std::vector<Zone> zones, std::string const& activeKey) {
    for (auto* sprite : m_sprites) {
        if (sprite) sprite->removeFromParent();
    }
    m_sprites.clear();
    m_zones = std::move(zones);

    for (auto const& zone : m_zones) {
        CCSprite* sprite = zone.texture
            ? CCSprite::createWithTexture(zone.texture) : nullptr;
        if (sprite) {
            sprite->setPosition({m_side / 2.f, m_side / 2.f});
            float const longest = std::max(sprite->getContentSize().width,
                                           sprite->getContentSize().height);
            if (longest > 0.f) sprite->setScale(m_side / longest);
            m_world->addChild(sprite, 1);
        }
        m_sprites.push_back(sprite);
    }
    setActiveZone(activeKey);
}

void EditorCanvas::setActiveZone(std::string const& key) {
    m_activeKey = key;
    for (std::size_t i = 0; i < m_sprites.size(); ++i) {
        if (!m_sprites[i]) continue;
        bool const active = m_zones[i].key == key;
        m_sprites[i]->setVisible(!m_isolate || active);
        m_sprites[i]->setOpacity(active ? 255 : 96);
    }
    redrawOverlay();
}

void EditorCanvas::setSelection(std::string const& zoneKey, int pieceIndex, bool locked) {
    m_selZone = zoneKey;
    m_selPiece = pieceIndex;
    m_selLocked = locked;
    redrawOverlay();
}

void EditorCanvas::setIsolate(bool isolate) {
    m_isolate = isolate;
    setActiveZone(m_activeKey);
}

void EditorCanvas::setBackgroundMode(int mode) {
    if (m_flatBg) {
        m_flatBg->setVisible(mode != 2);
        m_flatBg->setColor(mode == 1 ? ccColor3B{226, 229, 238} : ccColor3B{18, 26, 52});
    }
    if (m_checkerBg) m_checkerBg->setVisible(mode == 2);
}

void EditorCanvas::setGuideVisible(bool visible) {
    if (m_guide) m_guide->setVisible(visible);
}

void EditorCanvas::setEyedropper(bool on) {
    m_eyedropper = on;
    redrawOverlay();
}

void EditorCanvas::applyView() {
    if (!m_world) return;
    float const c = m_side / 2.f;
    m_world->setScale(m_zoom);
    m_world->setPosition({c - c * m_zoom + m_pan.x, c - c * m_zoom + m_pan.y});

    if (m_zoomLabel) {
        m_zoomLabel->setString(
            fmt::format("{}%", static_cast<int>(std::lround(m_zoom * 100.f))).c_str());
        m_zoomLabel->limitLabelWidth(30.f, 0.3f, 0.14f);
    }
    redrawOverlay();
    if (m_cb.onViewChanged) m_cb.onViewChanged();
}

CCPoint EditorCanvas::toCanvas(CCPoint const& viewport) const {
    float const c = m_side / 2.f;
    return {(viewport.x - c - m_pan.x) / m_zoom + c,
            (viewport.y - c - m_pan.y) / m_zoom + c};
}

CCPoint EditorCanvas::toViewport(CCPoint const& canvas) const {
    float const c = m_side / 2.f;
    return {(canvas.x - c) * m_zoom + c + m_pan.x,
            (canvas.y - c) * m_zoom + c + m_pan.y};
}

void EditorCanvas::zoomAt(float factor, CCPoint const& viewportPoint) {
    auto const anchor = toCanvas(viewportPoint);
    float const target = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    if (std::fabs(target - m_zoom) < 1e-4f) return;
    m_zoom = target;

    float const c = m_side / 2.f;
    m_pan = CCPoint{viewportPoint.x - (anchor.x - c) * m_zoom - c,
                    viewportPoint.y - (anchor.y - c) * m_zoom - c};

    float const limit = m_side * m_zoom * 0.5f;
    m_pan.x = std::clamp(m_pan.x, -limit, limit);
    m_pan.y = std::clamp(m_pan.y, -limit, limit);
    applyView();
}

void EditorCanvas::nudgeZoom(float factor) {
    zoomAt(factor, {m_side / 2.f, m_side / 2.f});
}

void EditorCanvas::resetView() {
    m_zoom = 1.f;
    m_pan = CCPoint{0.f, 0.f};
    applyView();
}

CCPoint EditorCanvas::viewportFromScreen(CCPoint const& screen) {
    return convertToNodeSpace(screen);
}

PieceRender const* EditorCanvas::selectedRender() const {
    if (m_selPiece < 0) return nullptr;
    for (auto const& zone : m_zones) {
        if (zone.key != m_selZone) continue;
        for (auto const& piece : zone.pieces) {
            if (piece.index == m_selPiece) return &piece;
        }
    }
    return nullptr;
}

bool EditorCanvas::selectionBox(float& left, float& bottom,
                                float& right, float& top) const {
    auto const* render = selectedRender();
    if (!render || !render->hasBounds || render->canvasSize <= 0) return false;

    float const k = m_side / static_cast<float>(render->canvasSize);

    left   = static_cast<float>(render->boundsX) * k;
    right  = static_cast<float>(render->boundsX + render->boundsW) * k;
    // buffer rows run top-down, node bottom-up.
    top    = m_side - static_cast<float>(render->boundsY) * k;
    bottom = m_side - static_cast<float>(render->boundsY + render->boundsH) * k;
    return true;
}

int EditorCanvas::corneredAt(CCPoint const& viewport) const {
    float l, b, r, t;
    if (!selectionBox(l, b, r, t)) return -1;

    CCPoint const corners[4] = {
        toViewport({l, b}), toViewport({r, b}),
        toViewport({r, t}), toViewport({l, t}),
    };
    for (int i = 0; i < 4; ++i) {
        if (ccpDistance(viewport, corners[i]) <= kHandleHit) return i;
    }

    CCPoint const topCenter = toViewport({(l + r) / 2.f, t});
    CCPoint const rotate{topCenter.x, topCenter.y + kRotateGap};
    if (ccpDistance(viewport, rotate) <= kHandleHit) return 4;
    return -1;
}

void EditorCanvas::redrawOverlay() {
    if (!m_overlay) return;
    m_overlay->clear();
    if (m_eyedropper) return;

    float l, b, r, t;
    if (!selectionBox(l, b, r, t)) return;

    // mid-drag the box tracks the finger without waiting for repaint.
    if (m_grab == Grab::Move) {
        float const dx = m_dragCenter.x - m_grabCenter.x;
        float const dy = m_dragCenter.y - m_grabCenter.y;
        l += dx; r += dx; b += dy; t += dy;
    }

    auto const color = m_selLocked ? kLockColor : kBoxColor;
    CCPoint const bl = toViewport({l, b});
    CCPoint const br = toViewport({r, b});
    CCPoint const tr = toViewport({r, t});
    CCPoint const tl = toViewport({l, t});

    m_overlay->drawSegment(bl, br, 0.6f, color);
    m_overlay->drawSegment(br, tr, 0.6f, color);
    m_overlay->drawSegment(tr, tl, 0.6f, color);
    m_overlay->drawSegment(tl, bl, 0.6f, color);

    if (m_snapX) {
        float const cx = toViewport({m_side / 2.f, 0.f}).x;
        m_overlay->drawSegment({cx, 0.f}, {cx, m_side}, 0.5f, kSnapColor);
    }
    if (m_snapY) {
        float const cy = toViewport({0.f, m_side / 2.f}).y;
        m_overlay->drawSegment({0.f, cy}, {m_side, cy}, 0.5f, kSnapColor);
    }

    if (m_selLocked) return;

    for (auto const& corner : {bl, br, tr, tl}) {
        m_overlay->drawDot(corner, kHandleDot, color);
    }

    CCPoint const topCenter{(tl.x + tr.x) / 2.f, (tl.y + tr.y) / 2.f};
    CCPoint const rotate{topCenter.x, topCenter.y + kRotateGap};
    m_overlay->drawSegment(topCenter, rotate, 0.5f, color);
    m_overlay->drawDot(rotate, kHandleDot + 0.6f, color);
}

bool EditorCanvas::pieceAt(CCPoint const& canvasPoint,
                           std::string& outZone, int& outPiece) const {
    for (int z = static_cast<int>(m_zones.size()) - 1; z >= 0; --z) {
        auto const& zone = m_zones[static_cast<std::size_t>(z)];
        if (m_isolate && zone.key != m_activeKey) continue;

        for (int i = static_cast<int>(zone.pieces.size()) - 1; i >= 0; --i) {
            auto const& piece = zone.pieces[static_cast<std::size_t>(i)];
            if (!piece.visible || piece.mask.empty() || piece.maskSize <= 0) continue;

            int const px = static_cast<int>(
                canvasPoint.x / m_side * static_cast<float>(piece.maskSize));
            int const py = static_cast<int>(
                (1.f - canvasPoint.y / m_side) * static_cast<float>(piece.maskSize));
            if (px < 0 || py < 0 || px >= piece.maskSize || py >= piece.maskSize) continue;

            if (piece.mask[static_cast<std::size_t>(py) * piece.maskSize + px] > 40) {
                outZone = zone.key;
                outPiece = piece.index;
                return true;
            }
        }
    }
    return false;
}

ccColor4B EditorCanvas::sampleAt(CCPoint const& canvasPoint) const {
    for (int z = static_cast<int>(m_zones.size()) - 1; z >= 0; --z) {
        auto const& zone = m_zones[static_cast<std::size_t>(z)];
        if (m_isolate && zone.key != m_activeKey) continue;
        auto const& image = zone.composite;
        if (image.empty()) continue;

        int const px = static_cast<int>(
            canvasPoint.x / m_side * static_cast<float>(image.width()));
        int const py = static_cast<int>(
            (1.f - canvasPoint.y / m_side) * static_cast<float>(image.height()));
        if (px < 0 || py < 0 || px >= image.width() || py >= image.height()) continue;

        auto const pixel = image.at(px, py);
        if (pixel.a > 24) return {pixel.r, pixel.g, pixel.b, pixel.a};
    }
    return {0, 0, 0, 0};
}

bool EditorCanvas::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto const local = convertTouchToNodeSpace(touch);
    if (local.x < 0.f || local.y < 0.f || local.x > m_side || local.y > m_side) {
        return false;
    }

    m_lastViewport = local;
    m_startCanvas = toCanvas(local);
    m_moved = 0.f;
    m_snapX = false;
    m_snapY = false;
    m_grab = Grab::None;

    if (m_eyedropper) return true;

    float l, b, r, t;
    bool const hasBox = selectionBox(l, b, r, t);

    if (hasBox && !m_selLocked) {
        int const handle = corneredAt(local);
        if (handle == 4) {
            m_grab = Grab::Rotate;
            m_grabCenter = CCPoint{(l + r) / 2.f, (b + t) / 2.f};
            m_lastAngle = std::atan2(m_startCanvas.y - m_grabCenter.y,
                                     m_startCanvas.x - m_grabCenter.x) * 180.f / kPi;
            return true;
        }
        if (handle >= 0) {
            m_grab = Grab::Scale;
            m_grabCenter = CCPoint{(l + r) / 2.f, (b + t) / 2.f};
            m_grabHalfW = std::max(2.f, (r - l) / 2.f);
            m_grabHalfH = std::max(2.f, (t - b) / 2.f);
            m_accumX = 1.f;
            m_accumY = 1.f;
            return true;
        }
    }

    std::string zone;
    int piece = -1;
    if (pieceAt(m_startCanvas, zone, piece)) {
        if ((zone != m_selZone || piece != m_selPiece) && m_cb.onSelect) {
            m_cb.onSelect(zone, piece);
        }
        if (!m_selLocked) {
            m_grab = Grab::Move;
            m_grabCenter = selectionBox(l, b, r, t)
                ? CCPoint{(l + r) / 2.f, (b + t) / 2.f}
                : m_startCanvas;
            m_dragCenter = m_grabCenter;
        } else if (m_cb.onHint) {
            m_cb.onHint("Esta capa esta bloqueada.");
        }
        return true;
    }

    // inside the box but over a transparent gap: still a move.
    if (hasBox && !m_selLocked &&
        m_startCanvas.x >= l && m_startCanvas.x <= r &&
        m_startCanvas.y >= b && m_startCanvas.y <= t) {
        m_grab = Grab::Move;
        m_grabCenter = CCPoint{(l + r) / 2.f, (b + t) / 2.f};
        m_dragCenter = m_grabCenter;
        return true;
    }

    m_grab = Grab::Pan;
    return true;
}

void EditorCanvas::ccTouchMoved(CCTouch* touch, CCEvent*) {
    auto const local = convertTouchToNodeSpace(touch);
    CCPoint const delta{local.x - m_lastViewport.x, local.y - m_lastViewport.y};
    m_moved += std::fabs(delta.x) + std::fabs(delta.y);
    m_lastViewport = local;

    if (m_eyedropper || m_moved <= kTapSlop) return;
    auto const canvasPoint = toCanvas(local);

    switch (m_grab) {
        case Grab::Pan: {
            float const limit = m_side * m_zoom * 0.5f;
            m_pan.x = std::clamp(m_pan.x + delta.x, -limit, limit);
            m_pan.y = std::clamp(m_pan.y + delta.y, -limit, limit);
            applyView();
            break;
        }
        case Grab::Move: {
            float const c = m_side / 2.f;
            float const guideHalf = m_side * 0.25f;
            CCPoint want{m_grabCenter.x + (canvasPoint.x - m_startCanvas.x),
                         m_grabCenter.y + (canvasPoint.y - m_startCanvas.y)};

            m_snapX = false;
            m_snapY = false;
            for (float target : {c, c - guideHalf, c + guideHalf}) {
                if (std::fabs(want.x - target) < kSnapCanvas) {
                    want.x = target;
                    m_snapX = std::fabs(target - c) < 0.01f;
                    break;
                }
            }
            for (float target : {c, c - guideHalf, c + guideHalf}) {
                if (std::fabs(want.y - target) < kSnapCanvas) {
                    want.y = target;
                    m_snapY = std::fabs(target - c) < 0.01f;
                    break;
                }
            }

            CCPoint const step{want.x - m_dragCenter.x, want.y - m_dragCenter.y};
            m_dragCenter = want;
            if (m_cb.onMove) m_cb.onMove(step.x / c, step.y / c);
            redrawOverlay();
            break;
        }
        case Grab::Scale: {
            float const wantX = std::max(0.05f,
                std::fabs(canvasPoint.x - m_grabCenter.x) / m_grabHalfW);
            float const wantY = std::max(0.05f,
                std::fabs(canvasPoint.y - m_grabCenter.y) / m_grabHalfH);
            float const stepX = wantX / m_accumX;
            float const stepY = wantY / m_accumY;
            m_accumX = wantX;
            m_accumY = wantY;
            if (m_cb.onScale) m_cb.onScale(stepX, stepY);
            break;
        }
        case Grab::Rotate: {
            float const angle = std::atan2(canvasPoint.y - m_grabCenter.y,
                                           canvasPoint.x - m_grabCenter.x) * 180.f / kPi;
            // atan2 grows CCW, GD rotation the other way.
            float step = m_lastAngle - angle;
            while (step > 180.f) step -= 360.f;
            while (step < -180.f) step += 360.f;
            m_lastAngle = angle;
            if (m_cb.onRotate) m_cb.onRotate(step);
            break;
        }
        default:
            break;
    }
}

void EditorCanvas::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_eyedropper) {
        auto const color = sampleAt(toCanvas(m_lastViewport));
        if (color.a > 24) {
            if (m_cb.onPick) m_cb.onPick({color.r, color.g, color.b});
        } else if (m_cb.onHint) {
            m_cb.onHint("Ahi no hay nada de que copiar el color.");
        }
    }
    endGesture();
}

void EditorCanvas::ccTouchCancelled(CCTouch*, CCEvent*) {
    endGesture();
}

void EditorCanvas::endGesture() {
    bool const wasDragging = m_grab != Grab::None && m_moved > kTapSlop;
    m_grab = Grab::None;
    m_snapX = false;
    m_snapY = false;
    redrawOverlay();
    if (wasDragging && m_cb.onGestureEnd) m_cb.onGestureEnd();
}

}  // namespace paimon::icon_maker
