#include "../services/ModPreviewRepo.hpp"
#include "../ui/ModPreviewGalleryPopup.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <map>
#include <string>
#include <vector>

// ModPopupUIEvent hook: shows a thumbnail strip on mod popups whose repo has previews.
// Port of Mod-Previews by Alphalaneous.

using namespace geode::prelude;
using namespace paimon::mod_previews;

namespace {

// Horizontal thumbnail strip. Child of "description-container", so its
// visibility follows the active tab automatically.
class ModPreviewStrip : public CCNode {
public:
    std::string m_urlBase;
    int m_loaded = 0;
    CCMenu* m_list = nullptr;
    std::vector<Ref<LazySprite>> m_pending;
    std::map<int, Ref<CCMenuItemSpriteExtra>> m_buttons;

    static ModPreviewStrip* create(std::string const& urlBase, float width) {
        auto ret = new ModPreviewStrip();
        if (ret->init(urlBase, width)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(std::string const& urlBase, float width) {
        if (!CCNode::init()) return false;
        m_urlBase = urlBase;
        this->setID("previews-strip"_spr);
        this->setContentSize({width, 56});
        this->setAnchorPoint({0.5f, 0.5f});

        auto bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize(this->getContentSize() / 0.5f);
        bg->setScale(0.5f);
        bg->setColor({0, 0, 0});
        bg->setOpacity(120);
        this->addChildAtPosition(bg, Anchor::Center);

        m_list = CCMenu::create();
        m_list->setContentSize(this->getContentSize());
        m_list->ignoreAnchorPointForPosition(false);
        m_list->setAnchorPoint({0.5f, 0.5f});
        this->addChildAtPosition(m_list, Anchor::Center);

        for (int i = 1; i <= 10; i++) {
            auto spr = LazySprite::create({100, 54}, false);
            m_pending.push_back(spr);
            int idx = i;
            spr->setLoadCallback([this, idx, spr](Result<> res) {
                if (res.isOk()) this->onLoaded(idx, spr);
            });
            spr->loadFromUrl(m_urlBase + std::to_string(i) + ".png");
        }
        return true;
    }

    void onLoaded(int idx, LazySprite* spr) {
        if (m_buttons.contains(idx)) return;
        m_loaded++;

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ModPreviewStrip::onThumb));
        float scale = (btn->getContentHeight() > 0) ? 50.f / btn->getContentHeight() : 1.f;
        btn->setScale(scale);
        btn->m_baseScale = scale;
        btn->setTag(idx);
        m_buttons[idx] = btn;
        relayout();
    }

    void relayout() {
        m_list->removeAllChildren();
        float x = 4.f;
        float const gap = 3.f;
        float const maxW = this->getContentWidth() - 4.f;
        for (auto& [idx, btn] : m_buttons) {
            float w = btn->getContentWidth() * btn->getScaleX();
            if (x + w > maxW) break;
            btn->setAnchorPoint({0.f, 0.5f});
            btn->setPosition({x, this->getContentHeight() / 2});
            m_list->addChild(btn);
            x += w + gap;
        }
    }

    void onThumb(CCObject* sender) {
        if (auto popup = ModPreviewGalleryPopup::create(static_cast<CCNode*>(sender)->getTag(), m_loaded, m_urlBase)) {
            popup->show();
        }
    }
};

void buildStrip(Ref<FLAlertLayer> popup, std::string urlBase) {
    if (!popup) return;
    auto desc = popup->getChildByIDRecursive("description-container");
    if (!desc) return;
    if (desc->getChildByID("previews-strip"_spr)) return;

    auto strip = ModPreviewStrip::create(urlBase, desc->getContentWidth() - 10.f);
    if (!strip) return;
    strip->setZOrder(10);
    desc->addChildAtPosition(strip, Anchor::Bottom, {0, 6});
}

void handleModPopup(FLAlertLayer* popup) {
    if (!popup) return;
    if (!Mod::get()->getSettingValue<bool>("mod-previews-enable")) return;

    // Event fires multiple times per popup; dedupe with a marker.
    if (popup->getUserObject("previews-init"_spr)) return;

    auto githubBtn = popup->getChildByIDRecursive("github");
    if (!githubBtn) return;
    auto urlObj = typeinfo_cast<CCString*>(githubBtn->getUserObject("url"));
    if (!urlObj) return;

    std::string source = urlObj->getCString();
    if (source.empty()) return;

    auto repo = getRepoData(source);
    if (!repo.valid) return;

    popup->setUserObject("previews-init"_spr, CCBool::create(true));

    Ref<FLAlertLayer> popupRef = popup;
    std::string rawURL = repo.rawURL;
    WebHelper::dispatch(web::WebRequest(), "GET", rawURL + "/main/mod.json",
        [popupRef, rawURL](web::WebResponse res) {
            std::string branch = res.ok() ? "main" : "master";
            buildStrip(popupRef, rawURL + "/" + branch + "/previews/preview-");
        });
}

} // namespace

$execute {
    static auto s_listener = ModPopupUIEvent().listen(
        [](FLAlertLayer* popup, std::string_view, std::optional<Mod*>) {
            handleModPopup(popup);
            return false; // propagate
        });
}
