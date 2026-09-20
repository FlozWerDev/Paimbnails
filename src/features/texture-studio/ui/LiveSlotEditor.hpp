#pragma once

#include "../persist/TextureProject.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

class LiveSlotEditor : public cocos2d::CCLayer {
public:
    static void open(std::string const& id);

protected:
    bool init(std::string const& id);
    void onExit() override;
    void keyBackClicked() override;

private:
    void buildControls(int page);
    void buildPreview();
    void edited(bool regions = false);
    void applyPreview(float = 0.f);
    void refreshStatus(float);
    void previewPicker(float);
    void exportConfig();
    void save();
    void back();

    TextureProject m_project;
    cocos2d::CCNode* m_controls = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    geode::TextInput* m_name = nullptr;
    geode::Ref<cocos2d::CCSprite> m_pickerTarget;
    cocos2d::ccColor3B TextureProject::* m_pickerColor = nullptr;
    int m_page = 0;
    int m_preset = -1;
    bool m_dirty = false;
    bool m_compare = false;
};

}
