#pragma once
#include <Geode/Geode.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>

namespace paimon::globalicon {

// Popup opened from a profile's custom icon: previews the shared "cube" icon
// and offers Download (register in More Icons) or Download-and-use.
class GlobalIconViewPopup : public geode::Popup {
protected:
    int m_accountID = 0;
    std::string m_username;
    GlobalIconSlot m_slot;                 // shared cube icon
    geode::Ref<SimplePlayer> m_preview;
    bool m_busy = false;

    bool init(int accountID, std::string const& username, GlobalIconSlot const& slot);
    void refreshPreview();
    void onDownload(cocos2d::CCObject*);
    void onDownloadUse(cocos2d::CCObject*);

public:
    static GlobalIconViewPopup* create(int accountID, std::string const& username, GlobalIconSlot const& slot);
};

} // namespace paimon::globalicon
