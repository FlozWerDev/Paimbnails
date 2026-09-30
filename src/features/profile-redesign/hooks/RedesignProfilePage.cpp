#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJComment.hpp>
#include "../services/RedesignedProfile.hpp"
#include "../../../core/Settings.hpp"
#include <limits>

using namespace geode::prelude;
class $modify(RedesignProfilePage, ProfilePage) {
    struct Fields {
        float m_watchTime = 0.f;
        Ref<CCArray> m_comments = nullptr;
        bool m_commentsLoaded = false;
        // coalesce rapid rebuild requests (comments + latecomer badges + swr
        // userinfo refresh can all land within a few frames).
        bool m_rebuildQueued = false;
        // sentinel so the first comments paint is never treated as a no-op.
        int m_lastCommentSig = std::numeric_limits<int>::min();
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityPost(
            "ProfilePage::loadPageFromUserInfo", geode::Priority::VeryLate);
    }

    $override
    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;
        return true;
    }

    $override
    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        if (!paimon::settings::profiles::redesignEnabled()) return;

        // immediate first paint so vanilla layout never flashes for a frame.
        doRedesign();
        // latecomers land async in hidden menus; rebuild only when one appears.
        m_fields->m_watchTime = 0.f;
        this->unschedule(schedule_selector(RedesignProfilePage::watchLatecomers));
        this->schedule(schedule_selector(RedesignProfilePage::watchLatecomers), 0.25f);
    }

    // this path can skip loadpagefromuserinfo, so the strip rebuilds here.
    $override
    void updateUserScoreFinished() {
        ProfilePage::updateUserScoreFinished();
        if (!paimon::settings::profiles::redesignEnabled()) return;
        doRedesign();
    }

    $override
    void userInfoChanged(GJUserScore* score) {
        ProfilePage::userInfoChanged(score);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        // can stack with swr getuserinfofinished → loadpagefromuserinfo; coalesce.
        scheduleRedesign();
    }

    $override
    void loadCommentsFinished(CCArray* comments, char const* key) {
        ProfilePage::loadCommentsFinished(comments, key);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        m_fields->m_commentsLoaded = true;
        int count = comments ? static_cast<int>(comments->count()) : 0;
        int commentId0 = 0;
        if (comments && count > 0) {
            auto* copy = CCArray::create();
            copy->addObjectsFromArray(comments);
            m_fields->m_comments = copy;
            if (auto* c0 = typeinfo_cast<GJComment*>(comments->objectAtIndex(0))) {
                commentId0 = c0->m_commentID;
            }
        } else {
            m_fields->m_comments = nullptr;
        }
        // skip a full redesign if we already painted this exact comments set
        // (swr userinfo refresh can re-enter without new comments).
        int const sig = (count << 16) ^ commentId0;
        if (m_fields->m_lastCommentSig == sig) {
            if (this->m_mainLayer &&
                paimon::profile_redesign::needsSettlePass(
                    this->m_mainLayer, this->m_buttonMenu, this->m_ownProfile)) {
                scheduleRedesign();
            }
            return;
        }
        m_fields->m_lastCommentSig = sig;
        geode::log::info("[paim-redesign] loadCommentsFinished count={}", count);
        scheduleRedesign();
    }

    // the fresh list comes back visible; rebuild so it hides behind the cards.
    $override
    void setupCommentsBrowser(CCArray* comments) {
        ProfilePage::setupCommentsBrowser(comments);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        scheduleRedesign();
    }

    $override
    void loadCommentsFailed(char const* key) {
        ProfilePage::loadCommentsFailed(key);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        m_fields->m_commentsLoaded = true;
        m_fields->m_comments = nullptr;
        m_fields->m_lastCommentSig = -1;
        geode::log::info("[paim-redesign] loadCommentsFailed");
        scheduleRedesign();
    }

    void watchLatecomers(float dt) {
        m_fields->m_watchTime += dt;
        if (this->m_mainLayer &&
            paimon::profile_redesign::needsSettlePass(
                this->m_mainLayer, this->m_buttonMenu, this->m_ownProfile)) {
            scheduleRedesign();
        }
        // async buttons (roles, badges, thumbnail count) arrive well within
        // this window; afterwards stop polling entirely.
        if (m_fields->m_watchTime >= 4.f) {
            this->unschedule(schedule_selector(RedesignProfilePage::watchLatecomers));
        }
    }

    void scheduleRedesign() {
        if (!paimon::settings::profiles::redesignEnabled()) return;
        if (m_fields->m_rebuildQueued) return;
        m_fields->m_rebuildQueued = true;
        // one rebuild per frame max — collapses swr + comments + badge storms.
        Ref<ProfilePage> self = this;
        Loader::get()->queueInMainThread([self]() {
            if (!self || !self->getParent()) return;
            auto* page = static_cast<RedesignProfilePage*>(self.data());
            page->m_fields->m_rebuildQueued = false;
            page->doRedesign();
        });
    }

    void doRedesign() {
        if (!paimon::settings::profiles::redesignEnabled()) return;
        if (!this->m_mainLayer) return;
        auto* score = this->m_score;
        if (!score) return;
        paimon::profile_redesign::buildInPlace(
            this->m_mainLayer, this->m_buttonMenu, score, this->m_list,
            this->m_accountID, this->m_ownProfile,
            m_fields->m_comments.data(), m_fields->m_commentsLoaded);
    }
};
