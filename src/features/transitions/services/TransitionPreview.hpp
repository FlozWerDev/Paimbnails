#pragma once
#include "TransitionEvaluation.hpp"

namespace paimon::transitions {
struct TransitionPreviewPane {
    TransitionVisual visual;
    float widthScale = 1.f;
};

// guide mode changes presentation only. The saved commands and runtime sampler
// retain the exact positions, opacities and scene cut used by final playback.
class TransitionPreview {
public:
    TransitionPreview(TransitionPoint screen, TransitionPoint viewport, bool guides)
        : m_screen(screen), m_viewport(viewport), m_guides(guides) {}

    TransitionPreviewPane scene(TransitionVisual visual, bool sceneB) const {
        float scale = m_viewport.x / m_screen.x;
        if (m_guides) {
            visual.position.x = m_viewport.x * (sceneB ? .75f : .25f) +
                (visual.position.x - m_screen.x / 2.f) * scale * .5f;
            visual.position.y *= scale;
            visual.opacity = std::max(visual.opacity, 90.f);
            return {visual, .5f};
        }
        visual.position.x *= scale; visual.position.y *= scale;
        return {visual, 1.f};
    }
    TransitionVisual object(TransitionVisual visual) const {
        float scale = m_viewport.x / m_screen.x;
        visual.position.x *= scale; visual.position.y *= scale;
        if (m_guides) visual.opacity = std::min(visual.opacity, 110.f);
        return visual;
    }
private:
    TransitionPoint m_screen, m_viewport;
    bool m_guides;
};
} // namespace paimon::transitions
