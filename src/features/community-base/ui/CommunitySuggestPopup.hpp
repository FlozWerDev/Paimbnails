#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <string>
#include <vector>

namespace paimon::community {

// sends one of the player's own gradients ("gradients") or the More Icons icon
// they wear ("icons") to the moderation queue: gamemode, name and description.
class CommunitySuggestPopup : public PaimonPopup {
public:
    static CommunitySuggestPopup* create(std::string const& type);
    ~CommunitySuggestPopup() override;

protected:
    bool init(std::string const& type);

private:
    std::string m_type;
    int m_selected = -1;
    bool m_busy = false;
    geode::TextInput* m_title = nullptr;
    geode::TextInput* m_description = nullptr;
    cocos2d::CCNode* m_highlight = nullptr;
    cocos2d::CCLabelBMFont* m_mode = nullptr;
    CCMenuItemSpriteExtra* m_send = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_modes;

    void select(int index);
    void send();
};

// picks a preset from the local paimon shop library and publishes it.
class CommunityPresetPickPopup : public PaimonPopup {
public:
    static CommunityPresetPickPopup* create();

protected:
    bool init() override;

private:
    cocos2d::CCNode* m_list = nullptr;
    CCMenuItemSpriteExtra* m_prev = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    size_t m_page = 0;

    void refresh();
    void publish(std::string const& id);
};

} // namespace paimon::community
