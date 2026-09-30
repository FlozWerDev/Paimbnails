#pragma once

#include "ScrollInputFilter.hpp"
#include <Geode/utils/function.hpp>

namespace paimon::smoothscroll {

using ScrollDispatchFn = geode::CopyableFunction<void(float y, float x)>;

#if defined(GEODE_IS_WINDOWS)
// one wheel notch in dispatcher units; the glfw hook and queueinput must agree.
inline constexpr double kInputUnitsPerStep = 5.0;
#else
inline constexpr double kInputUnitsPerStep = 12.0;
#endif

// smooth scrolling for lists/menus with exponential decay momentum.
class SmoothScrollController {
public:
    static SmoothScrollController& get();

    bool isActive() const;
    bool isReplaying() const { return m_replaying; }
    bool isEditorZoomReplay() const { return m_replaying && m_editorZoomMode; }

    // true = consume the event (no instant scroll reaches the game).
    bool queueInput(float wheelY, float wheelX);
    void tick(float dt, ScrollDispatchFn const& dispatch);

    // signed normalized steps: one notch sums to +/-1 across replay frames.
    float replayedWheelSteps() const;
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
    bool m_editorZoomMode = false;

    float replaySteps(bool negate) const;
    float filteredSteps(float wheelY, float wheelX, bool negate) const;
};

bool shouldBypassSmoothScroll();
bool isEditorZoomGesture();

} // namespace paimon::smoothscroll
