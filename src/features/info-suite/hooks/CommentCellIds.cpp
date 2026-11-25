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

        int id = comment->m_commentID;
        if (id <= 0) return;

        paimon::info::attachIdBadge(this, kBadgeID, fmt::format("#{}", id), 0.36f,
            {1.f, 0.f}, {this->getContentSize().width - 5.f, 3.f});
    }
};
