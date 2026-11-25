#pragma once

#include <Geode/Geode.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::gestures {

enum class GestureKind : std::uint8_t {
    None = 0,
    Hold1F,
    Hold2F,
    Hold3F,
    DoubleTap2F,
    DoubleTap3F,
    SwipeUp2F,
    SwipeDown2F,
    SwipeLeft2F,
    SwipeRight2F,
    SwipeUp3F,
    SwipeDown3F,
    SwipeLeft3F,
    SwipeRight3F,
    Drag2FVertical,
    Drag2FHorizontal,
    Drag3FVertical,
    Drag3FHorizontal,
};

enum class GestureActionId : std::uint8_t {
    QuickHub = 0,
    LayoutEditor,
    VolumeMusic,
    VolumeSFX,
    CaptureThumbnail,
    CaptureMenu,
    SettingsPanel,
    PaimonAssistant,
    PaimonFeedback,
    PauseZoomIn,
    PauseZoomOut,
    PauseZoomReset,
    PauseZoomToggleMenu,
};

struct GestureActionMeta {
    GestureActionId id;
    char const* key;
    char const* name;
    char const* description;
    char const* category;
    GestureKind defaultGesture;
    bool supportsContinuousDrag;
};

struct GestureComfortSettings {
    float holdSeconds = 1.25f;
    float moveTolerancePt = 14.0f;
    float swipeSensitivity = 1.0f;
    bool hudFeedback = true;
};

class GestureManager final {
public:
    static GestureManager& get();

    static std::vector<GestureActionMeta> const& actions();
    static std::vector<GestureKind> const& discreteKinds();
    static std::vector<GestureKind> const& allKinds();
    static GestureActionMeta const* findAction(GestureActionId id);
    static GestureActionMeta const* findActionByKey(std::string_view key);

    static char const* kindId(GestureKind kind);
    static char const* kindDisplayName(GestureKind kind);
    static GestureKind kindFromId(std::string_view id);
    static bool isDragKind(GestureKind kind);
    static bool isHoldKind(GestureKind kind);
    static int fingerCountForKind(GestureKind kind);

    GestureKind getBinding(GestureActionId id) const;
    void setBinding(GestureActionId id, GestureKind kind);
    std::optional<GestureActionId> findConflict(GestureActionId exclude, GestureKind kind) const;
    std::optional<GestureActionId> actionForKind(GestureKind kind) const;

    void resetAllBindings();

    GestureComfortSettings comfort() const;
    void setHoldSeconds(float seconds);
    void setMoveTolerancePt(float points);
    void setSwipeSensitivity(float sensitivity);
    void setHudFeedback(bool enabled);

    bool triggerAction(GestureActionId id);

    // Converts raw CCEGLViewProtocol pixel coordinates into Cocos UI point space.
    static cocos2d::CCPoint viewPixelsToPoint(float px, float py);

    // Multi-touch gesture tracking hooks called on mobile after UI claimed-touch checks.
    void onTouchesBegan(int num, int ids[], float xs[], float ys[], double timestamp, bool claimedByUi);
    void onTouchesMoved(int num, int ids[], float xs[], float ys[], double timestamp);
    void onTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp, bool cancelled);

public:
    struct ActivePoint {
        cocos2d::CCPoint startPt;
        cocos2d::CCPoint curPt;
        double startTime = 0.0;
    };

private:
    GestureManager() = default;

    void handleDiscreteGesture(GestureKind kind);
    void resetGestureTracking();

    std::vector<std::pair<int, ActivePoint>> m_active;
    int m_peakFingers = 0;
    bool m_movedPastTap = false;
    bool m_swipeFired = false;
    bool m_pinchActive = false;
    bool m_pinchHidingDisabled = false;
    bool m_blockedByUi = false;
    float m_lastPinchDist = 0.f;
    cocos2d::CCPoint m_lastCentroid{0.f, 0.f};
    cocos2d::CCPoint m_strokeStartCentroid{0.f, 0.f};
    double m_strokeStartTime = 0.0;
    double m_lastTapTime = 0.0;
    int m_lastTapFingers = 0;
    cocos2d::CCPoint m_lastTapCentroid{0.f, 0.f};
};

} // namespace paimon::gestures
