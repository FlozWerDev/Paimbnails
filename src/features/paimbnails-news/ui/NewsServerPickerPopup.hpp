#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <functional>
#include <string>
#include <unordered_set>

namespace paimon::news {

// lets the player add or remove news sources (Vanilla and verified Discord providers).
class NewsServerPickerPopup final : public PaimonPopup {
public:
    static NewsServerPickerPopup* create(std::function<void()> onChanged = {});

private:
    bool init(std::function<void()> onChanged);
    void rebuildList();
    cocos2d::CCNode* createSourceRow(
        std::string const& sourceId,
        std::string const& title,
        std::string const& subtitle,
        std::string const& iconUrl,
        bool showAvatar,
        bool enabled,
        int favoritesCount = 0,
        bool favoritedByMe = false,
        bool canFavorite = false
    );
    void onToggleFavorite(std::string const& channelId);

    std::function<void()> m_onChanged;
    std::unordered_set<std::string> m_pendingVotes;
    cocos2d::CCNode* m_panel = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::news
