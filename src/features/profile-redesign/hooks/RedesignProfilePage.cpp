#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJComment.hpp>
#include "../services/RedesignedProfile.hpp"
#include "../../../core/Settings.hpp"

using namespace geode::prelude;// into the bottom row). The profile background system keeps working natively
// because this is the actual ProfilePage, not a separate popup.
class $modify(RedesignProfilePage, ProfilePage) {
    struct Fields {
        int m_settlePasses = 0;
        Ref<CCArray> m_comments = nullptr;
        bool m_commentsLoaded = false;
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

        doRedesign();
        // call, so re-run the redesign a few times to catch the post and the        m_fields->m_settlePasses = 0;
        this->unschedule(schedule_selector(RedesignProfilePage::settleRedesign));
        this->schedule(schedule_selector(RedesignProfilePage::settleRedesign), 0.35f);
    }

    // Own-profile stats are synced to the server here (GD pushes the local
    // GJGameStatsManager values and gets back an updated score). This path does
    // NOT go through loadPageFromUserInfo, so without rebuilding here the
    // redesigned stat strip keeps showing the pre-sync (stale) numbers.
    $override
    void updateUserScoreFinished() {
        ProfilePage::updateUserScoreFinished();
        if (!paimon::settings::profiles::redesignEnabled()) return;
        doRedesign();
    }

    // Fired whenever the cached user score changes (UserInfoDelegate). Rebuild
    // so any stat/icon change is reflected immediately.
    $override
    void userInfoChanged(GJUserScore* score) {
        ProfilePage::userInfoChanged(score);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        doRedesign();
    }

    $override
    void loadCommentsFinished(CCArray* comments, char const* key) {
        ProfilePage::loadCommentsFinished(comments, key);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        m_fields->m_commentsLoaded = true;
        int count = comments ? comments->count() : 0;
        if (comments && count > 0) {
            auto* copy = CCArray::create();
            copy->addObjectsFromArray(comments);
            m_fields->m_comments = copy;
        } else {
            m_fields->m_comments = nullptr;
        }
        geode::log::info("[paim-redesign] loadCommentsFinished count={}", count);
        doRedesign();
    }

    $override
    void loadCommentsFailed(char const* key) {
        ProfilePage::loadCommentsFailed(key);
        if (!paimon::settings::profiles::redesignEnabled()) return;
        // stops saying "Loading...".
        m_fields->m_commentsLoaded = true;
        m_fields->m_comments = nullptr;
        geode::log::info("[paim-redesign] loadCommentsFailed");
        doRedesign();
    }

    void settleRedesign(float) {
        doRedesign();
        if (++m_fields->m_settlePasses >= 5) {
            this->unschedule(schedule_selector(RedesignProfilePage::settleRedesign));
        }
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
