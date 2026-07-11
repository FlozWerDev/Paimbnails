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

// True when some relocatable button/badge exists outside the redesign's rd-*
// containers (a latecomer created asynchronously by another feature) or is
// duplicated. Cheap tree walk; used to rebuild only when actually needed.
bool needsSettlePass(
    cocos2d::CCLayer* mainLayer,
    cocos2d::CCNode* buttonMenu,
    bool ownProfile
);

}
