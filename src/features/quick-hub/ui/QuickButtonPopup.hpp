#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../data/QuickHubCategories.hpp"
#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace paimon::quickhub {

// shows the detected "address" as proof of what got saved.
class QuickButtonPopup : public PaimonPopup {
public:
    static QuickButtonPopup* create(CustomQuickButton candidate);
    static bool isOpen();

    void setOnSaved(std::function<void(std::string const&)> callback) {
        m_onSaved = std::move(callback);
    }

protected:
    bool init() override;
    void onExit() override;

private:
    CustomQuickButton m_candidate;
    bool m_editing = false;
    std::function<void(std::string const&)> m_onSaved;

    geode::TextInput* m_nameInput = nullptr;
    cocos2d::CCNode* m_preview = nullptr;
    cocos2d::CCMenu* m_shapeMenu = nullptr;
    cocos2d::CCMenu* m_colorMenu = nullptr;
    cocos2d::CCLabelBMFont* m_imageStatus = nullptr;
    cocos2d::CCLabelBMFont* m_sfxStatus = nullptr;

    void rebuildPreview();
    void rebuildShapeButtons();
    void rebuildColorSwatches();
    void buildTargetInfo();
    void refreshMetaLabels();
    void setShape(RadialButtonShape shape);
    void setIcon(std::string frame);
    void setColor(cocos2d::ccColor3B color);
    void onChangeIcon(cocos2d::CCObject*);
    void onPickImage(cocos2d::CCObject*);
    void onEditSfx(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);

    static QuickButtonPopup* s_instance;
};

} // namespace paimon::quickhub
