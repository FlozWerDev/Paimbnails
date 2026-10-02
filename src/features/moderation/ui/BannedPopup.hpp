#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <string>

class BannedPopup : public PaimonPopup {
protected:
    std::string m_reason;

    bool init(std::string const& reason);
    void onDisableMod(cocos2d::CCObject*);

public:
    static BannedPopup* create(std::string const& reason);
};

namespace paimon::ban {
    // shows the non-dismissable banned popup. safe to call from the main thread.
    void showBannedPopup(std::string const& reason);
}
