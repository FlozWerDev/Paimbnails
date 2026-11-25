#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../OfficialSlots.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace paimon::officialslots::ui {

class SlotManagerPopup : public PaimonPopup {
public:
    static SlotManagerPopup* create(std::function<void()> onChanged = nullptr);

protected:
    bool init(std::function<void()> onChanged);
    void buildList();
    void buildFooter();
    void rebuild();
    cocos2d::CCNode* buildPageRow(std::string const& key, float width);
    void openEditor(std::string const& key = {});
    void onPlay(std::string const& key);
    void onMove(std::string const& key, int delta);
    void onToggle(std::string const& key);
    void onDelete(std::string const& key);
    void onReorder();
    void mutated();

    std::function<void()> m_onChanged;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_summary = nullptr;
    bool m_listBuilt = false;
};

} // namespace paimon::officialslots::ui
