#pragma once

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
void prepareInPlace(cocos2d::CCLayer* mainLayer);

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

// true when a relocatable button/badge sits outside the rd-* containers;
// cheap check to rebuild only when actually needed.
bool needsSettlePass(
    cocos2d::CCLayer* mainLayer,
    cocos2d::CCNode* buttonMenu,
    bool ownProfile
);

}
