#include "../InfoModule.hpp"
#include "../services/IdBadge.hpp"
#include "../../../framework/HookConventions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/modify/CommentCell.hpp>

using namespace geode::prelude;

namespace {

char const* const kBadgeID = "info-suite-comment-id"_spr;

bool commentIdsEnabled() {
    return paimon::info::subEnabled("info-mod-ids", "info-ids-comments", true);
}

} // namespace

class $modify(PaimonInfoSuiteCommentCell, CommentCell) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "CommentCell::loadFromComment");
    }

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        if (!commentIdsEnabled() || !comment) return;
        if (this->getChildByID(kBadgeID)) return;

        int id = comment->m_commentID;
        if (id <= 0) return;

        auto* badge = paimon::info::makeIdBadge(fmt::format("#{}", id), 0.36f);
        if (!badge) return;

        badge->setID(kBadgeID);
        badge->setAnchorPoint({1.f, 0.f});
        badge->setPosition({this->getContentSize().width - 5.f, 3.f});
        this->addChild(badge, 100);
    }
};
