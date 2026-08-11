#pragma once

// Uncapped "go to page" for the level browser. Vanilla GD clamps its SetIDPopup
// to the pages the server reported; this one lets you type any page, drag a
// scrubber across the known range, and jump to the first or last page.

#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <string>

namespace paimon::info {

class JumpToPagePopup : public geode::Popup {
public:
    // `currentPage` and `pageCount` are 1 based. `pageCount` may be 0 when the
    // server never reported a total, in which case the scrubber is hidden.
    static JumpToPagePopup* create(int currentPage, int pageCount,
                                   std::function<void(int)> onJump);

protected:
    bool init(int currentPage, int pageCount, std::function<void(int)> onJump);
    void onClose(cocos2d::CCObject* sender) override;

    void setPage(int page, bool syncSlider, bool syncInput);
    void refreshLabels();

    void onSlider(cocos2d::CCObject* sender);
    void onFirst(cocos2d::CCObject*);
    void onLast(cocos2d::CCObject*);
    void onConfirm(cocos2d::CCObject*);
    void onInputChanged(std::string const& text);

    std::function<void(int)> m_onJump;
    int m_page = 1;
    int m_pageCount = 0;

    geode::TextInput* m_input = nullptr;
    Slider* m_slider = nullptr;
    cocos2d::CCLabelBMFont* m_rangeLabel = nullptr;
    bool m_syncing = false;
};

} // namespace paimon::info
