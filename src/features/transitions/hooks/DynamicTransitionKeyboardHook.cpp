#include "../services/DynamicTransitionManager.hpp"
#ifndef GEODE_IS_IOS
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif
#include <Geode/modify/CCKeypadDispatcher.hpp>

using namespace geode::prelude;
namespace dynamic = paimon::transitions::dynamic;

namespace {
struct BackInputGuard {
    bool active;
    explicit BackInputGuard(bool back) : active(back) {
        if (active) dynamic::beginBackInput();
    }
    ~BackInputGuard() {
        if (active) dynamic::endBackInput();
    }
};
}

#ifndef GEODE_IS_IOS
class $modify(PaimonDynamicKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool pressed, bool repeat, double timestamp) {
        BackInputGuard guard(key == KEY_Escape && pressed);
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, pressed, repeat, timestamp);
    }
};
#else
$execute {
    KeyboardInputEvent(KEY_Escape).listen(+[](KeyboardInputData& data) {
        if (data.action == KeyboardInputData::Action::Release) return false;
        dynamic::beginBackInput();
        Loader::get()->queueInMainThread([] { dynamic::endBackInput(); });
        return false;
    });
}
#endif

class $modify(PaimonDynamicKeypad, CCKeypadDispatcher) {
    bool dispatchKeypadMSG(ccKeypadMSGType message) {
        BackInputGuard guard(message == kTypeBackClicked);
        return CCKeypadDispatcher::dispatchKeypadMSG(message);
    }
};
