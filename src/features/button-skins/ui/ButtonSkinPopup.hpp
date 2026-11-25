#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../data/ButtonSkin.hpp"
#include "../services/ButtonSkinRender.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace paimon::buttonskin {

// the right-clicked button, captured with its skin taken off.
struct SkinEditTarget {
    geode::WeakRef<cocos2d::CCMenuItem> item;
    quickhub::CustomQuickButton identity;
    std::string signature;
    AssetShape shape;
    float pointScale = 1.f;  // one asset unit in the button's parent points
};

// full editor: paint (image/color/gradient fitted to the asset), original look, shape, sound, scope.
class ButtonSkinPopup : public PaimonPopup {
public:
    static ButtonSkinPopup* create(SkinEditTarget target, std::optional<ButtonSkin> existing);
    static bool isOpen();

protected:
    bool init() override;
    void onExit() override;
    void update(float dt) override;

private:
    SkinEditTarget m_target;
    ButtonSkin m_skin;
    bool m_editing = false;
    int m_tab = 0;
    bool m_previewDirty = true;
    bool m_rebuildQueued = false;
    bool m_keepScroll = false;
    cocos2d::CCNode* m_preview = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_scopeButtons;

    void buildLeftColumn();
    void refreshScopeButtons();
    bool scopeAvailable(SkinScope scope) const;
    void markDirty() { m_previewDirty = true; }
    void rebuildPreview();
    void scheduleRebuild(bool keepScroll);
    void rebuildTab(bool keepScroll);
    std::vector<cocos2d::CCNode*> buildFillTab(float width);
    std::vector<cocos2d::CCNode*> buildOriginalTab(float width);
    std::vector<cocos2d::CCNode*> buildShapeTab(float width);
    std::vector<cocos2d::CCNode*> buildSoundTab(float width);
    std::string soundSummary() const;
    void onChooseImage();
    void importImage(std::filesystem::path const& src);
    void onTestSound();
    void onSave();
    void onRemove();

    static ButtonSkinPopup* s_instance;
};

} // namespace paimon::buttonskin
