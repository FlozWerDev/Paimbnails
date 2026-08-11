#pragma once
// One icon of a copied set, blown up: the icon on the left, everything the game
// asks of you before it hands it over on the right.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../IconCopyStore.hpp"

namespace paimon::iconcopy {

class IconDetailPopup : public geode::Popup {
public:
    static IconDetailPopup* create(IconSet const& set, IconType type);

protected:
    bool init(IconSet const& set, IconType type);
};

}  // namespace paimon::iconcopy
