#pragma once
#include "../../../ui/PaimonPopup.hpp"
// single text field. renames icons and layers.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>
#include <string>

namespace paimon::icon_maker {

class IconNamePopup : public PaimonPopup {
public:
    using ConfirmCallback = std::function<void(std::string const&)>;

    // the callback runs after the popup closes and never with an empty name.
    static IconNamePopup* create(std::string title, std::string placeholder,
                                 std::string initial, ConfirmCallback onConfirm);

protected:
    bool init(std::string title, std::string placeholder, std::string initial,
              ConfirmCallback onConfirm);

    geode::TextInput* m_input = nullptr;
    ConfirmCallback m_onConfirm;
};

}  // namespace paimon::icon_maker
