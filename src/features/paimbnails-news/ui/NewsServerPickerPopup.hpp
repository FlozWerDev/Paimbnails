#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <functional>
#include <string>

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
        bool enabled
    );

    std::function<void()> m_onChanged;
    cocos2d::CCNode* m_panel = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::news
