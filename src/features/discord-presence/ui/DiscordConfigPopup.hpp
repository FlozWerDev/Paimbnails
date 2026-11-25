#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class PaimonDrawNode;

namespace paimon::discord {
class DiscordConfigPopup : public PaimonPopup {
public:
    static DiscordConfigPopup* create();
protected:
    bool init() override;
    void onExit() override;
    bool scrollWheel(float x, float y) override;
private:
    void buildPreview(cocos2d::CCPoint origin, cocos2d::CCSize size);
    void buildStatus(cocos2d::CCPoint origin, float width);
    void buildActions(float left, float width);
    void showPage(int page);
    void buildPage(int page);
    cocos2d::CCNode* makePresetRow(float width);
    void refreshPresetState();
    void changed();
    void updatePreview();
    // slot 0 = large image, 1 = small image.
    void refreshPreviewImage(int slot, std::string const& reference);
    void loadPreviewImage(int slot, uint32_t generation, std::string const& url);
    void pollPreview(float);
    void updateSmoothScroll(float);
    void pickImage(bool large);
    void applyPreset(std::string const& preset);
    void importProfile();
    void exportProfile();
    void onResetDefaults(cocos2d::CCObject*);
    void onRefreshPresence(cocos2d::CCObject*);

    geode::ScrollLayer* m_scroll = nullptr;
    float m_scrollWidth = 0.f;

    // discord-style activity card.
    cocos2d::CCNode* m_previewCard = nullptr;
    cocos2d::CCNode* m_previewImage = nullptr;
    cocos2d::CCSprite* m_previewLogo = nullptr;
    cocos2d::CCNode* m_previewPlaceholder = nullptr;
    cocos2d::CCLabelBMFont* m_previewPlaceholderText = nullptr;
    cocos2d::CCNode* m_previewBadge = nullptr;
    cocos2d::CCNode* m_previewBadgeFill = nullptr;
    std::array<cocos2d::CCClippingNode*, 2> m_previewClips{};
    std::array<geode::LazySprite*, 2> m_previewSprites{};
    std::array<std::string, 2> m_previewUrls{};
    std::array<uint32_t, 2> m_previewImageGeneration{};
    std::array<int, 2> m_previewImageState{};
    cocos2d::CCLabelBMFont* m_prevHeader = nullptr;
    cocos2d::CCLabelBMFont* m_prevTag = nullptr;
    cocos2d::CCLabelBMFont* m_prevName = nullptr;
    cocos2d::CCLabelBMFont* m_prevDetails = nullptr;
    cocos2d::CCLabelBMFont* m_prevState = nullptr;
    cocos2d::CCLabelBMFont* m_prevTime = nullptr;
    std::array<cocos2d::CCLabelBMFont*, 2> m_prevHover{};
    std::array<cocos2d::CCNode*, 2> m_prevButtons{};
    std::array<cocos2d::CCLabelBMFont*, 2> m_prevButtonLabels{};
    float m_previewTextTop = 0.f;
    float m_previewHoverTop = 0.f;
    bool m_previewHoverFits = true;

    cocos2d::CCLabelBMFont* m_connection = nullptr;
    PaimonDrawNode* m_statusDot = nullptr;
    std::string m_statusText;
    int m_statusTone = -1;

    std::vector<CCMenuItemSpriteExtra*> m_presetButtons;
    cocos2d::CCLabelBMFont* m_presetLabel = nullptr;

    geode::TextInput* m_largeImageKeyInput = nullptr;
    geode::TextInput* m_smallImageKeyInput = nullptr;
    std::array<uint32_t, 2> m_uploadGeneration{};
    std::array<bool, 2> m_uploading{};
    float m_scrollTargetY = 0.f;
    bool m_scrollTargetSet = false;
    bool m_destroyed = false;
    bool m_pageBuilt = false;
    int m_page = 0;
    int m_textContext = 0;
};
}
