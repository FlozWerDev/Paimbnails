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

        // recycled cells refire on fast scroll; debounce the fade
        auto now = std::chrono::steady_clock::now();
        if (m_fields->m_faded &&
            std::chrono::duration<float>(now - m_fields->m_lastFade).count() < 0.35f) return;
        m_fields->m_lastFade = now;
        m_fields->m_faded = true;

        // CommentCell lacks CCRGBAProtocol; revealNode fades its RGBA children
        paimon::fluid::revealNode(this, {.fadeDuration = 0.14f});
    }
};
