// Fades in comment cells as they load. CommentCell is recycled on scroll
// (loadFromComment fires per cell entering view), so a per-cell debounce
// avoids re-triggering the fade during fast scrolling.

#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../framework/HookConventions.hpp"
#include "../utils/FluidReveal.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <chrono>

using namespace geode::prelude;

class $modify(PaimonCommentFadeIn, CommentCell) {
    static void onModify(auto& self) {
        // Run after node-ids and other setup (incl. the badge hook) have populated the cell.
        paimon::hooks::afterNodeIdsOrLate(self, "CommentCell::loadFromComment");
    }

    struct Fields {
        std::chrono::steady_clock::time_point m_lastFade{};
        bool m_faded = false;
    };

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);

        if (!comment || paimon::isRuntimeShuttingDown()) return;

        // Per-cell debounce: 0.35s exceeds the typical recycle interval during
        // flick-scroll but is imperceptible when opening the list.
        auto now = std::chrono::steady_clock::now();
        if (m_fields->m_faded) {
            auto elapsed = std::chrono::duration<float>(now - m_fields->m_lastFade).count();
            if (elapsed < 0.35f) return;
        }
        m_fields->m_lastFade = now;
        m_fields->m_faded = true;

        // CommentCell (TableViewCell) isn't CCRGBAProtocol; revealNode recurses into RGBA descendants.
        paimon::fluid::revealNode(this, {.fadeDuration = 0.14f});
    }
};
