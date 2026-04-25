#include "InfoButton.hpp"
#include <Geode/loader/GameEvent.hpp>

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

    auto* alert = FLAlertLayer::create(title.c_str(), desc.c_str(), "OK");
    if (alert) alert->show();
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


