#pragma once

#include "ScrollInputFilter.hpp"
#include <Geode/utils/function.hpp>

class CCScrollLayerExt;
namespace cocos2d {
    class CCMouseDelegate;
}

namespace paimon::smoothscroll {

using ScrollDispatchFn = geode::CopyableFunction<void(float y, float x)>;

#if defined(GEODE_IS_WINDOWS)
// one wheel notch in dispatcher units; the glfw hook and queueinput must agree.
inline constexpr double kInputUnitsPerStep = 5.0;
#else
inline constexpr double kInputUnitsPerStep = 12.0;
#endif

// pixels the content travels per wheel notch at sensitivity 1.0.
inline constexpr double kBasePixelsPerNotch = 20.0;

// smooth scrolling for lists/menus with spring driven momentum.
class SmoothScrollController {
public:
    static SmoothScrollController& get();

    bool isActive() const;
    bool isReplaying() const { return m_replaying; }
    bool isEditorZoomReplay() const { return m_replaying && m_editorZoomMode; }
    // true while the mod owns the scroll travel. The game applies the travel
    // on its own schedule, so this stays on until the momentum ends.
    bool isDrivingScroll() const { return m_driving; }

    // true = consume the event (no instant scroll reaches the game).
    bool queueInput(float wheelY, float wheelX);
    void tick(float dt, ScrollDispatchFn const& dispatch);

    // signed normalized steps: one notch sums to +/-1 across replay frames.
    float replayedZoomSteps() const;
    float filteredWheelSteps(float wheelY, float wheelX) const;
    float filteredZoomSteps(float wheelY, float wheelX) const;

    bool hasMomentum() const;
    void stop();
    void reset();

private:
    ScrollInputFilter m_filter;
    ScrollVector m_replayActions;
    void const* m_scrollTarget = nullptr;
    bool m_replaying = false;
    bool m_driving = false;
    bool m_editorZoomMode = false;

    // pixels the content travels per wheel notch at the current sensitivity.
    float pixelsPerNotch() const;
    float replaySteps(bool negate) const;
    float filteredSteps(float wheelY, float wheelX, bool negate) const;
};

bool shouldBypassSmoothScroll();
bool isEditorZoomGesture();

// A list (TableView, geode::ScrollLayer) registered for the wheel while it is
// not on screen, e.g. the list of a tab that is not shown. Both kinds accept
// the wheel without checking that, so a hidden one registered later would take
// the travel meant for the visible list. nullptr for any other delegate.
CCScrollLayerExt* hiddenScrollList(cocos2d::CCMouseDelegate* delegate);

} // namespace paimon::smoothscroll
