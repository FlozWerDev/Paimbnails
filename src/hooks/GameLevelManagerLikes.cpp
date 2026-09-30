// likeitem funnels every like/dislike, catching paths the old tracker missed

#include <Geode/Geode.hpp>
#include <Geode/modify/GameLevelManager.hpp>

#include "../features/foryou/services/TasteProfile.hpp"

using namespace geode::prelude;

class $modify(PaimonForYouLikesGameLevelManager, GameLevelManager) {
    $override
    void likeItem(LikeItemType type, int id, bool liked, int parentID) {
        GameLevelManager::likeItem(type, id, liked, parentID);

        // taste model only tracks levels
        if (type != LikeItemType::Level || id <= 0) return;

        auto& profile = paimon::foryou::TasteProfile::get();
        profile.onLevelVote(id, liked);
        profile.save();
    }
};
