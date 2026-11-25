#pragma once

#include "../persist/TextureProject.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>

namespace paimon::texture_studio {

// texture Live editor: palette controls on the left, a live board on the
// right. Everything on screen (including this editor's own buttons) is
// painted by LiveSlotRuntime while the editor is open.
class LiveSlotEditor : public cocos2d::CCLayer {
public:
    static void open(std::string const& id);

protected:
    using cocos2d::CCLayer::init;
    bool init(std::string const& id);
    void onExit() override;
    void keyBackClicked() override;

private:
    enum class Page { Colors = 0, Presets, Adjust, Scope, Count };
    enum class Board { Buttons = 0, Panels, Controls, Count };
    using ColorMember = cocos2d::ccColor3B TextureProject::*;
    using FlagMember = bool TextureProject::*;

    void buildHeader();
    void buildFooter();
    void buildTabs();
    void buildPage(bool keepScroll);
    float buildColorsPage(cocos2d::CCMenu* menu, float w);
    float buildPresetsPage(cocos2d::CCMenu* menu, float w);
    float buildAdjustPage(cocos2d::CCMenu* menu, float w);
    float buildScopePage(cocos2d::CCMenu* menu, float w);
    void buildBoard();
    void buildBoardTabs();
    void buildBoardContent();
    // rebuilds run on the next frame: callbacks must not delete their own button.
    void deferRebuild(bool page, bool keepScroll, bool tabs = false, bool board = false);
    void flushRebuild();
    void openPicker(ColorMember member, FlagMember autoFlag);
    void edited(bool regions = false);
    void applyPreview(float = 0.f);
    void refreshStatus(float);
    void previewPicker(float);
    void onBoardSlider(cocos2d::CCObject*) {}
    void exportConfig();
    void importConfig();
    void swapColors();
    void makeComplementary();
    void randomizePalette();
    void resetAll();
    void save();
    void back();
    void close();
    cocos2d::CCSprite* makeSwatch(cocos2d::ccColor3B color, cocos2d::CCSize size);

    TextureProject m_project;
    // swatches use a private white texture: the live engine never repaints it.
    geode::Ref<cocos2d::CCTexture2D> m_solid;
    cocos2d::CCNode* m_controls = nullptr;
    geode::ScrollLayer* m_controlsScroll = nullptr;
    geode::Scrollbar* m_controlsBar = nullptr;
    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCMenu* m_boardTabMenu = nullptr;
    cocos2d::CCNode* m_board = nullptr;
    cocos2d::CCNode* m_boardContent = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCSprite* m_progressTrack = nullptr;
    cocos2d::CCSprite* m_progressFill = nullptr;
    cocos2d::CCLabelBMFont* m_compareLabel = nullptr;
    geode::TextInput* m_name = nullptr;
    geode::Ref<cocos2d::CCSprite> m_pickerTarget;
    geode::WeakRef<geode::ColorPickPopup> m_picker;
    ColorMember m_pickerColor = nullptr;
    FlagMember m_pickerAuto = nullptr;
    cocos2d::CCRect m_left;
    cocos2d::CCRect m_right;
    float m_progressWidth = 0.f;
    Page m_page = Page::Colors;
    Board m_boardMode = Board::Buttons;
    bool m_dirty = false;
    bool m_compare = false;
    struct PendingRebuild {
        bool queued = false;
        bool page = false;
        bool keepScroll = true;
        bool tabs = false;
        bool board = false;
    };
    PendingRebuild m_rebuild;
};

}
