#include "InfoButton.hpp"
#include <Geode/loader/GameEvent.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <algorithm>

using namespace cocos2d;
using namespace geode::prelude;

namespace {
Ref<PaimonInfoTarget> s_infoTarget = nullptr;

constexpr float kAlertWidth = 340.f;

// TextArea wraps at 2 * width but the frame fits ~1.33 * width, so 0.6
// keeps a 10% margin below the 0.66 spill point.
constexpr float kTextScale    = 0.6f;
constexpr float kScrollHeight = 170.f;

// The screen is 320 units tall and the title bar plus the OK button eat about
// 105 of them. Text taller than what is left gets a scroller instead of an
// auto-sized popup that would run off the top.
constexpr float kMaxTextHeight = 185.f;

// chatFont.fnt metrics.
constexpr float kAvgAdvance = 7.3f;
constexpr float kLineHeight = 18.f;

// Approximation is enough: it only picks auto-height vs scroller.
float measureTextHeight(std::string const& desc) {
    // Wrapping happens before the scale is applied, so the column count comes
    // from the raw (2 * width) font units and does not depend on kTextScale.
    int const columns = static_cast<int>(2.f * kAlertWidth / kAvgAdvance);
    int total = 0;
    for (size_t pos = 0; pos <= desc.size();) {
        auto end = desc.find('\n', pos);
        if (end == std::string::npos) end = desc.size();

        int visible = 0;
        for (size_t i = pos; i < end; ++i) {
            if (desc[i] == '<') {
                auto close = desc.find('>', i);
                if (close != std::string::npos && close < end) {
                    i = close;
                    continue;
                }
            }
            ++visible;
        }
        total += std::max(1, (visible + columns - 1) / columns);
        pos = end + 1;
    }
    return total * kLineHeight * kTextScale;
}
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

    // Short infos fit the popup; only overflowing texts get the scroller.
    bool const scroll = measureTextHeight(desc) > kMaxTextHeight;

    auto* alert = FLAlertLayer::create(
        nullptr,
        title.c_str(),
        desc,
        "OK", nullptr,
        kAlertWidth,
        scroll,
        scroll ? kScrollHeight : 0.f,   // 0 = fit to the text
        kTextScale
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


