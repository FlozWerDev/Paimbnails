#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::quickhub {

bool handleQuickButtonRightClick();
bool activateCustomQuickButton(std::string const& id);

// True when the saved button can be pressed right now.
bool isCustomQuickButtonReachable(std::string const& id);

} // namespace paimon::quickhub
