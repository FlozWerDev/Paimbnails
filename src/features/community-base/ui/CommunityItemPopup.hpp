#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <functional>
#include <string>

namespace paimon::community {

// one public paimon base item: large preview, likes, dislikes and downloads,
// the viewer's vote, and the actions of its type (save or apply a preset, take
// a gradient or an icon, use a comment background).
class CommunityItemPopup : public PaimonPopup {
public:
    // `onChanged` runs when the popup closes after a vote, a download or a delete.
    static CommunityItemPopup* create(matjson::Value item, std::function<void()> onChanged = nullptr);
    void onClose(cocos2d::CCObject* sender) override;

protected:
    bool init(matjson::Value item, std::function<void()> onChanged);

private:
    matjson::Value m_item;
    std::string m_type;
    std::string m_id;
    std::function<void()> m_onChanged;
    bool m_changed = false;
    bool m_busy = false;
    bool m_voting = false;
    bool m_own = false;
    int m_likes = 0;
    int m_dislikes = 0;
    int m_downloads = 0;
    int m_myVote = 0;
    cocos2d::CCNode* m_counters = nullptr;
    CCMenuItemSpriteExtra* m_like = nullptr;
    CCMenuItemSpriteExtra* m_dislike = nullptr;

    void refreshRating();
    void vote(int value);
    void readCounts(matjson::Value const& json);
    // counts the download once the action worked.
    void counted();
    void savePreset(bool apply);
    void takeGradient(bool apply);
    void takeIcon(bool use);
    void useBackground();
    void remove();
};

} // namespace paimon::community
