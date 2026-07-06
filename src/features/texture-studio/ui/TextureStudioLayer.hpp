#pragma once

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::texture_studio {

class SlotsGridView;

// Full-screen pack manager: grid of pack slots with apply/edit/delete,
// pushed as its own scene (no longer a popup).
class TextureStudioLayer : public cocos2d::CCLayer {
public:
    static TextureStudioLayer* create();
    static cocos2d::CCScene* scene();

    // Push the studio scene with a fade transition.
    static void open();

protected:
    bool init() override;
    void onEnter() override;
    void keyBackClicked() override;

    void onBack(cocos2d::CCObject*);
    void onNewPack(cocos2d::CCObject*);
    void onApplySlot(std::string const& slotId);
    void onEditSlot(std::string const& slotId);
    void onDeleteSlot(std::string const& slotId);
    void onOpenFolder(cocos2d::CCObject*);

    void buildBackground();
    void refreshFooter();

private:
    SlotsGridView*           m_grid       = nullptr;
    cocos2d::CCLabelBMFont*  m_activeLbl  = nullptr;
    bool                     m_enteredOnce = false;
};

}  // namespace paimon::texture_studio
