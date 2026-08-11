#pragma once
// One text field: names a styling when you save it, and renames it later.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>
#include <string>

namespace paimon::iconcopy {

class IconSetNamePopup : public geode::Popup {
public:
    using Callback = std::function<void(std::string const&)>;

    // The callback runs after the popup closes and never with an empty name.
    static IconSetNamePopup* create(std::string title, std::string initial, Callback onConfirm);

protected:
    bool init(std::string const& title, std::string const& initial, Callback onConfirm);
    void onClose(cocos2d::CCObject*) override;

    geode::TextInput* m_input = nullptr;
    Callback m_onConfirm;
};

}  // namespace paimon::iconcopy
