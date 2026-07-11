#include "InfoButton.hpp"
#include <Geode/loader/GameEvent.hpp>
#include <Geode/ui/PopupManager.hpp>

using namespace cocos2d;
using namespace geode::prelude;

namespace {
Ref<PaimonInfoTarget> s_infoTarget = nullptr;
}

void PaimonInfoTarget::onInfo(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item) return;
    auto* dataStr = typeinfo_cast<CCString*>(item->getUserObject());
    if (!dataStr) return;

    std::string raw = dataStr->getCString();
    auto sep = raw.find("\n---\n");
    std::string title = (sep != std::string::npos) ? raw.substr(0, sep) : "Info";
    std::string desc = (sep != std::string::npos) ? raw.substr(sep + 5) : raw;

    // Compact popup: narrower with slightly smaller text so it doesn't fill the
    // screen. width=280, auto scroll, textScale=0.75.
    auto* alert = FLAlertLayer::create(
        nullptr,
        title.c_str(),
        desc,
        "OK", nullptr,
        280.f,      // width
        true,       // scroll if the text is long
        0.f,        // height auto
        0.75f       // text scale
    );
    if (alert) PopupManager::get().manage(alert).showInstant();
}

PaimonInfoTarget* PaimonInfoTarget::create() {
    auto* ret = new PaimonInfoTarget();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

PaimonInfoTarget* PaimonInfoTarget::shared() {
    if (!s_infoTarget) {
        s_infoTarget = PaimonInfoTarget::create();
    }
    return s_infoTarget.data();
}

$on_game(Exiting) {
    if (s_infoTarget) {
        (void)s_infoTarget.take();
    }
}


