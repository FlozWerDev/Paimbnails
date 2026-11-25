#include "../ui/PaimonAssistantPopup.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

namespace {
constexpr char const* kAssistantKeybind = "paimon-assistant-keybind";
}

$execute {
    KeybindSettingPressedEventV3(Mod::get(), kAssistantKeybind).listen(
        +[](Keybind const&, bool down, bool repeat, double) -> void {
            if (!down || repeat) return;

            // en gameplay activo las teclas son del nivel; en menus/pausa si abre.
            auto* pl = PlayLayer::get();
            if (pl && !pl->m_isPaused) return;

            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                paimon::assistant::PaimonAssistantPopup::toggle();
            });
        }
    ).leak();
}
