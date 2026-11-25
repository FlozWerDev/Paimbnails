#include <Geode/Geode.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../services/RedesignedProfile.hpp"
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

// the redesigned profile styles each post once, right after creating it. A
// later loadFromComment on the same cell (the translator repaints text that
// way) brings back GD's brown bubble and the badge panel, so the style goes
// back on after every other hook has finished.
class $modify(RedesignCommentCell, CommentCell) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "CommentCell::loadFromComment");
    }

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        paimon::profile_redesign::restyleAccountCommentCell(this);
    }
};
