#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

namespace paimon::quickhub {

class RadialConfigPopup : public PaimonPopup {
public:
    static RadialConfigPopup* create();

protected:
    bool init() override;

    // working copy
    std::vector<std::string> m_activeIds;
    int m_tab = 0;

    cocos2d::CCNode* m_previewNode = nullptr;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    void rebuildList();

    void rebuildPreview();

    void setTab(int tab);

    void onMoveUp(int idx);
    void onMoveDown(int idx);
    void onRemoveOption(int idx);
    void onAddOption(std::string const& id);
    void onEditCustom(std::string const& id);
    void onDeleteCustom(std::string const& id);
    void onSave(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
};

} // namespace paimon::quickhub
