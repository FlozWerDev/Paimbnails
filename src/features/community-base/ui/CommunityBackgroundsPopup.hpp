#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <functional>
#include <string>

namespace paimon::community {

// approved comment backgrounds. with a pick callback the gallery returns the
// chosen one (comment background editor); without it, tapping one opens its
// item popup with ratings and "use for comments".
class CommunityBackgroundsPopup : public PaimonPopup {
public:
    using PickCallback = std::function<void(std::string const& id, std::string const& title)>;
    static CommunityBackgroundsPopup* create(PickCallback onPick = nullptr);

protected:
    bool init(PickCallback onPick);

private:
    PickCallback m_onPick;
    cocos2d::CCMenu* m_grid = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_prev = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    int m_offset = 0;
    bool m_hasMore = false;
    int m_token = 0;

    void load();
    void openItem(matjson::Value const& item);
};

// sets an approved background as the player's comment background.
void useBackgroundForComments(std::string const& id, std::function<void(bool ok)> done = nullptr);

} // namespace paimon::community
