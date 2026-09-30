#pragma once
// editor touch surface (pick, move, stretch and rotate layers).

#include "../engine/PieceRenderer.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

class PaimonDrawNode;

namespace paimon::icon_maker {

class EditorCanvas : public cocos2d::CCLayer {
public:
    struct Zone {
        std::string key;
        // editor-owned texture shared by canvas and zone strip thumbnails.
        cocos2d::CCTexture2D* texture = nullptr;
        texture_studio::ImageBuffer composite;  // eyedropper source
        std::vector<PieceRender> pieces;
    };

    // nudges arrive in imagetransform offset units (half-canvas fraction); summed as-is.
    struct Callbacks {
        std::function<void(std::string const& zoneKey, int pieceIndex)> onSelect;
        std::function<void(float dx, float dy)> onMove;
        std::function<void(float factorX, float factorY)> onScale;
        std::function<void(float deltaDeg)> onRotate;
        std::function<void(cocos2d::ccColor3B color)> onPick;
        std::function<void()> onGestureEnd;
        std::function<void(std::string const& text)> onHint;
        std::function<void()> onViewChanged;
    };

    static EditorCanvas* create(float side, Callbacks callbacks);

    void setZones(std::vector<Zone> zones, std::string const& activeKey);
    void setSelection(std::string const& zoneKey, int pieceIndex, bool locked);
    void setActiveZone(std::string const& key);
    void setIsolate(bool isolate);
    void setBackgroundMode(int mode);
    void setGuideVisible(bool visible);

    void setEyedropper(bool on);
    bool eyedropper() const { return m_eyedropper; }

    // zoom anchors at `viewportpoint`, in canvas space.
    void zoomAt(float factor, cocos2d::CCPoint const& viewportPoint);
    void nudgeZoom(float factor);
    void resetView();
    float zoomLevel() const { return m_zoom; }

    // screen point to canvas coords; caller checks the frame.
    cocos2d::CCPoint viewportFromScreen(cocos2d::CCPoint const& screen);

protected:
    enum class Grab { None, Move, Scale, Rotate, Pan };

    bool init(float side, Callbacks callbacks);

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void applyView();
    void redrawOverlay();
    void buildZoomControls();

    cocos2d::CCPoint toCanvas(cocos2d::CCPoint const& viewport) const;
    cocos2d::CCPoint toViewport(cocos2d::CCPoint const& canvas) const;

    PieceRender const* selectedRender() const;
    bool selectionBox(float& left, float& bottom, float& right, float& top) const;
    bool pieceAt(cocos2d::CCPoint const& canvasPoint,
                 std::string& outZone, int& outPiece) const;
    cocos2d::ccColor4B sampleAt(cocos2d::CCPoint const& canvasPoint) const;
    // 0..3 corners, 4 rotate handle; -1 on miss.
    int corneredAt(cocos2d::CCPoint const& viewport) const;
    void endGesture();

    float m_side = 200.f;
    float m_zoom = 1.f;
    cocos2d::CCPoint m_pan{0.f, 0.f};

    std::vector<Zone> m_zones;
    std::vector<cocos2d::CCSprite*> m_sprites;
    std::string m_activeKey;
    std::string m_selZone;
    int m_selPiece = -1;
    bool m_selLocked = false;

    bool m_isolate = false;
    bool m_eyedropper = false;

    cocos2d::CCNode* m_world = nullptr;
    cocos2d::CCLayerColor* m_flatBg = nullptr;
    cocos2d::CCSprite* m_checkerBg = nullptr;
    cocos2d::CCLayerColor* m_guide = nullptr;
    PaimonDrawNode* m_overlay = nullptr;
    cocos2d::CCLabelBMFont* m_zoomLabel = nullptr;

    Grab m_grab = Grab::None;
    float m_moved = 0.f;
    cocos2d::CCPoint m_lastViewport{};
    cocos2d::CCPoint m_startCanvas{};

    cocos2d::CCPoint m_grabCenter{};
    cocos2d::CCPoint m_dragCenter{};
    float m_grabHalfW = 1.f;
    float m_grabHalfH = 1.f;
    float m_accumX = 1.f;
    float m_accumY = 1.f;
    float m_lastAngle = 0.f;
    bool m_snapX = false;
    bool m_snapY = false;

    Callbacks m_cb;
};

}  // namespace paimon::icon_maker
