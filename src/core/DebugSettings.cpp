#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include "../utils/Debug.hpp"
#include "Settings.hpp"

using namespace geode::prelude;

$execute {
    bool initial = paimon::settings::general::enableDebugLogs();
    Mod::get()->setLoggingEnabled(initial);
    PaimonDebug::setEnabled(initial);

    geode::listenForSettingChanges<bool>("enable-debug-logs", +[](bool value) {
        Mod::get()->setLoggingEnabled(value);
        PaimonDebug::setEnabled(value);
    });
}
