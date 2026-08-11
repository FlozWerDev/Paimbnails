// Feeds the For You model with the user's own like and dislike verdicts.
//
// GameLevelManager::likeItem is the single funnel every like passes through —
// LikeItemLayer, LevelInfoLayer and the comment popups all end up here — so one
// hook catches every path, including the dislike the old tracker never saw.

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
