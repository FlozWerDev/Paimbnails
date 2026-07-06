#pragma once
#include <Geode/Geode.hpp>
#include <string>

class BannedPopup : public geode::Popup {
protected:
    std::string m_reason;

    bool init(std::string const& reason);
    void onDisableMod(cocos2d::CCObject*);

public:
    static BannedPopup* create(std::string const& reason);
};

namespace paimon::ban {
    // Shows the non-dismissable banned popup. Safe to call from the main thread.
    void showBannedPopup(std::string const& reason);
}
