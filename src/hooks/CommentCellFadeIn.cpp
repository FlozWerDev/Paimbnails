// Debounce recycled cells so fast scrolling does not restart the fade.

#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../framework/HookConventions.hpp"
#include "../utils/FluidReveal.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <chrono>

using namespace geode::prelude;

class $modify(PaimonCommentFadeIn, CommentCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "CommentCell::loadFromComment");
    }

    struct Fields {
        std::chrono::steady_clock::time_point m_lastFade{};
        bool m_faded = false;
    };

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);

        if (!comment || paimon::isRuntimeShuttingDown()) return;

// The debounce covers a typical recycle interval without delaying initial display.
        auto now = std::chrono::steady_clock::now();
        if (m_fields->m_faded) {
            auto elapsed = std::chrono::duration<float>(now - m_fields->m_lastFade).count();
            if (elapsed < 0.35f) return;
        }
        m_fields->m_lastFade = now;
        m_fields->m_faded = true;

// CommentCell is not CCRGBAProtocol; revealNode fades its RGBA descendants.
        paimon::fluid::revealNode(this, {.fadeDuration = 0.14f});
    }
};
