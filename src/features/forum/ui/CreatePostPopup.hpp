#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/function.hpp>
#include "../services/ForumApi.hpp"
#include <vector>
#include <string>

class CreatePostPopup : public geode::Popup, public FLAlertLayerProtocol {
protected:
    geode::TextInput* m_titleInput = nullptr;
    geode::TextInput* m_descInput = nullptr;
    cocos2d::CCMenu* m_tagMenu = nullptr;
    cocos2d::CCLabelBMFont* m_tagsHint = nullptr;
    cocos2d::CCLabelBMFont* m_cooldownLabel = nullptr;
    geode::TextInput* m_newTagInput = nullptr;

    std::vector<std::string> m_availableTags;
    std::vector<std::string> m_selectedTags;
    geode::CopyableFunction<void(paimon::forum::Post const&)> m_onCreated;

    bool init(std::vector<std::string> availableTags,
              geode::CopyableFunction<void(paimon::forum::Post const&)> onCreated);

    void rebuildTagChips();
    void onToggleTag(cocos2d::CCObject* sender);
    void onAddCustomTag(cocos2d::CCObject*);
    void onSubmit(cocos2d::CCObject*);
    void updateCooldownLabel();
    void FLAlert_Click(FLAlertLayer*, bool);

public:
    static CreatePostPopup* create(
        std::vector<std::string> availableTags,
        geode::CopyableFunction<void(paimon::forum::Post const&)> onCreated
    );
    virtual void update(float dt) override;
};
