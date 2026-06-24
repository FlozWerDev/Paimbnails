#pragma once

// real ProfilePage's main layer (so the profile background and every other
#include <string>

namespace cocos2d {
    class CCLayer;
    class CCNode;
    class CCArray;
}
class GJUserScore;
class GJCommentListLayer;
class GJComment;

namespace paimon::profile_redesign {
// asynchronous profile data is ready, preventing a one-frame vanilla flash.
void prepareInPlace(cocos2d::CCLayer* mainLayer);

// `comments` is the account's posts (an array of GJComment, may be null/empty);
// `commentsLoaded` is false until the asynchronous fetch has returned, so the
// left card can show a "Loading..." placeholder. The redesign builds its own
// to draw once scaled into the small card.
void buildInPlace(
    cocos2d::CCLayer* mainLayer,
    cocos2d::CCNode* buttonMenu,
    GJUserScore* score,
    GJCommentListLayer* commentList,
    int accountId,
    bool ownProfile,
    cocos2d::CCArray* comments,
    bool commentsLoaded
);

} // namespace paimon::profile_redesign
