#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::quickhub {

bool handleQuickButtonRightClick();
bool activateCustomQuickButton(std::string const& id);

} // namespace paimon::quickhub
