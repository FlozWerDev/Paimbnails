#pragma once

#include <Geode/Geode.hpp>
#include <functional>

// Search history: tap reruns, X deletes, "Clear" empties.
class SearchHistoryPopup : public geode::Popup {
public:
    // Callback gets the picked entry index into paimon::searchhistory::history.
    static SearchHistoryPopup* create(std::function<void(int)> callback);

protected:
    std::function<void(int)> m_callback;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    bool init(std::function<void(int)> callback);
    void rebuild();
    void onSearchEntry(cocos2d::CCObject*);
    void onRemoveEntry(cocos2d::CCObject*);
    void onClear(cocos2d::CCObject*);
};
