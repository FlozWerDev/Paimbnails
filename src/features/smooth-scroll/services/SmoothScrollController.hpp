#pragma once

#include <Geode/utils/function.hpp>

namespace paimon::smoothscroll {

using ScrollDispatchFn = geode::CopyableFunction<void(float y, float x)>;

// Smooth scrolling for lists/menus with exponential decay momentum.
class SmoothScrollController {
public:
    static SmoothScrollController& get();

    bool isActive() const;
    bool isReplaying() const { return m_replaying; }

    // true = consumir el evento (no pasar scroll instantaneo al juego).
    bool queueInput(float wheelY, float wheelX);
    void tick(float dt, ScrollDispatchFn const& dispatch);

    bool hasMomentum() const;
    void stop();
    void reset();

private:
    double m_momentumY = 0.0;
    double m_momentumX = 0.0;
    bool m_replaying = false;
};

bool shouldBypassSmoothScroll();
bool shouldUseSmoothPauseZoom();

} // namespace paimon::smoothscroll