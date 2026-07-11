#include "SettingsMigration.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

$execute {
    paimon::settings::migrateToSavedValues();
}
