#include "../ui/FeedbackPopup.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;

namespace {
constexpr char const* kFeedbackKeybind = "feedback-keybind";
}

$execute {
    KeybindSettingPressedEventV3(Mod::get(), kFeedbackKeybind).listen(
        +[](Keybind const&, bool down, bool repeat, double) -> void {
            if (!down || repeat) return;

            // active gameplay keeps its keys; paused menus are fine.
            auto* pl = PlayLayer::get();
            if (pl && !pl->m_isPaused) return;

            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                paimon::feedback::FeedbackPopup::open();
            });
        }
    ).leak();
}
