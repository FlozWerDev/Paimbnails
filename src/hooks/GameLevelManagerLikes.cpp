// Feeds the For You model: likeItem is the single funnel every like/dislike
// passes through, so one hook catches paths the old tracker never saw.

#include <Geode/Geode.hpp>
#include <Geode/modify/GameLevelManager.hpp>

#include "../features/foryou/services/TasteProfile.hpp"

using namespace geode::prelude;

class $modify(PaimonForYouLikesGameLevelManager, GameLevelManager) {
    $override
    void likeItem(LikeItemType type, int id, bool liked, int parentID) {
        GameLevelManager::likeItem(type, id, liked, parentID);

        // Comment and list votes say nothing about level taste.
        if (type != LikeItemType::Level || id <= 0) return;

        auto& profile = paimon::foryou::TasteProfile::get();
        profile.onLevelVote(id, liked);
        profile.save();
    }
};
