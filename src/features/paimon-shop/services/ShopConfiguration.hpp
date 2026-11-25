#pragma once
#include "../data/ShopPreset.hpp"

namespace paimon::shop {
matjson::Value captureConfiguration(Category category);
geode::Result<> applyConfiguration(Category category, matjson::Value const& configuration);
geode::Result<> restoreConfiguration();
bool hasConfigurationBackup();
std::string describeConfiguration(Preset const& preset);
} // namespace paimon::shop
