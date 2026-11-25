#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include "../services/ForumApi.hpp"
#include <string>
#include <vector>

class CreatePostPopup : public PaimonPopup {
protected:
    geode::TextInput* m_titleInput = nullptr;
    geode::TextInput* m_levelIdInput = nullptr;
    geode::TextInput* m_descInput = nullptr;

    cocos2d::CCLabelBMFont* m_titleCountLabel = nullptr;
    cocos2d::CCLabelBMFont* m_descCountLabel = nullptr;
    cocos2d::CCLabelBMFont* m_tagsHint = nullptr;
    cocos2d::CCLabelBMFont* m_cooldownLabel = nullptr;

    geode::ScrollLayer* m_tagScroll = nullptr;
    cocos2d::CCMenu* m_tagMenu = nullptr;
    CCMenuItemSpriteExtra* m_submitBtn = nullptr;

    std::vector<std::string> m_availableTags;
    std::vector<std::string> m_selectedTags;
    geode::CopyableFunction<void(paimon::forum::Post const&)> m_onCreated;

    bool m_submitting = false;
    bool m_published = false;
    float m_tagScrollTarget = 0.f;
    bool m_tagScrollTargetSet = false;

    bool init(std::vector<std::string> availableTags,
              geode::CopyableFunction<void(paimon::forum::Post const&)> onCreated);

    void rebuildTagChips();
    void applyTemplate(std::string const& tagName);
    void toggleTagAt(int idx);
    void onToggleTag(cocos2d::CCObject* sender);
    void onAddCustomTag(cocos2d::CCObject*);
    void onClearDraft(cocos2d::CCObject*);
    void onSubmit(cocos2d::CCObject*);
    void updateCharCounters();
    void updateCooldownLabel();
    void saveDraft();

public:
    static CreatePostPopup* create(
        std::vector<std::string> availableTags,
        geode::CopyableFunction<void(paimon::forum::Post const&)> onCreated
    );
    void onExit() override;
    bool scrollWheel(float x, float y) override;
    void update(float dt) override;
};
