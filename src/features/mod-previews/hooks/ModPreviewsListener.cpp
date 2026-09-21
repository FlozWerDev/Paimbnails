#include "../services/ModPreviewRepo.hpp"
#include "../ui/ModPreviewGalleryPopup.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <map>
#include <string>
#include <vector>

// Thumbnail ribbon on mod popups whose repo carries previews (see
// THIRD-PARTY-NOTICES.md).

using namespace geode::prelude;
using namespace paimon::mod_previews;

namespace {

constexpr int kProbeMax = 10;
constexpr float kRibbonH = 62.f;
constexpr float kThumbH = 46.f;
constexpr float kCellGap = 6.f;
constexpr float kEdgePad = 6.f;

// Horizontal thumbnail ribbon. Child of "description-container", so its
// visibility follows the active tab automatically.
class PreviewRibbon : public CCNode {
public:
    std::string m_prefix; // ".../previews/preview-" without "<n>.png"
    int m_top = 0;        // highest probe index that finished loading
    CCMenu* m_row = nullptr;
    std::vector<Ref<LazySprite>> m_slots;
    std::map<int, Ref<CCMenuItemSpriteExtra>> m_cells;

    static PreviewRibbon* create(std::string const& prefix, float width) {
        auto ret = new PreviewRibbon();
        if (ret->init(prefix, width)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init(std::string const& prefix, float width) {
        if (!CCNode::init()) return false;
        m_prefix = prefix;
        this->setID("mod-image-ribbon"_spr);
        this->setContentSize({width, kRibbonH});
        this->setAnchorPoint({0.5f, 0.5f});

        auto shade = CCScale9Sprite::create("square02b_001.png");
        shade->setContentSize(this->getContentSize() / 0.5f);
        shade->setScale(0.5f);
        shade->setColor({0, 0, 0});
        shade->setOpacity(140);
        this->addChildAtPosition(shade, Anchor::Center);

        m_row = CCMenu::create();
        m_row->setContentSize(this->getContentSize());
        m_row->ignoreAnchorPointForPosition(false);
        m_row->setAnchorPoint({0.5f, 0.5f});
        this->addChildAtPosition(m_row, Anchor::Center);

        for (int i = 1; i <= kProbeMax; i++) {
            auto slot = LazySprite::create({110, 60}, false);
            m_slots.push_back(slot);
            int probe = i;
            slot->setLoadCallback([this, probe, slot](Result<> res) {
                if (res.isOk()) this->onCell(probe, slot);
            });
            slot->loadFromUrl(m_prefix + std::to_string(i) + ".png");
        }
        return true;
    }

    void onCell(int probe, LazySprite* slot) {
        if (m_cells.contains(probe)) return;
        m_top = std::max(m_top, probe);

        auto pick = CCMenuItemSpriteExtra::create(slot, this, menu_selector(PreviewRibbon::onPick));
        float fit = (pick->getContentHeight() > 0) ? kThumbH / pick->getContentHeight() : 1.f;
        pick->setScale(fit);
        pick->m_baseScale = fit;
        pick->setTag(probe);
        m_cells[probe] = pick;
        refreshRow();
    }

    void refreshRow() {
        m_row->removeAllChildren();
        float x = kEdgePad;
        float const limit = this->getContentWidth() - kEdgePad;
        for (auto& [probe, cell] : m_cells) {
            float w = cell->getContentWidth() * cell->getScaleX();
            if (x + w > limit) break;
            cell->setAnchorPoint({0.f, 0.5f});
            cell->setPosition({x, this->getContentHeight() / 2});
            m_row->addChild(cell);
            x += w + kCellGap;
        }
    }

    void onPick(CCObject* sender) {
        int probe = static_cast<CCNode*>(sender)->getTag();
        if (auto gallery = ModPreviewGalleryPopup::create(probe, std::max(m_top, 1), m_prefix)) {
            gallery->show();
        }
    }
};

void attachRibbon(Ref<FLAlertLayer> popup, std::string prefix) {
    if (!popup) return;
    auto desc = popup->getChildByIDRecursive("description-container");
    if (!desc) return;
    if (desc->getChildByID("mod-image-ribbon"_spr)) return;

    auto ribbon = PreviewRibbon::create(prefix, desc->getContentWidth() - 12.f);
    if (!ribbon) return;
    ribbon->setZOrder(10);
    desc->addChildAtPosition(ribbon, Anchor::Bottom, {0, 10});
}

void handleModPopup(FLAlertLayer* popup) {
    if (!popup) return;
    if (!Mod::get()->getSettingValue<bool>("mod-previews-enable")) return;

    // Event fires multiple times per popup; dedupe with a marker.
    if (popup->getUserObject("mod-images-init"_spr)) return;

    auto siteBtn = popup->getChildByIDRecursive("github");
    if (!siteBtn) return;
    auto urlObj = typeinfo_cast<CCString*>(siteBtn->getUserObject("url"));
    if (!urlObj) return;

    std::string page = urlObj->getCString();
    if (page.empty()) return;

    auto source = resolvePreviewSource(page);
    if (!source.ok) return;

    popup->setUserObject("mod-images-init"_spr, CCBool::create(true));

    // Default-branch probe: most repos use "main", older ones "master".
    Ref<FLAlertLayer> popupRef = popup;
    std::string assetBase = source.assetBase;
    WebHelper::dispatch(web::WebRequest(), "GET", assetBase + "/main/mod.json",
        [popupRef, assetBase](web::WebResponse res) {
            std::string branch = res.ok() ? "main" : "master";
            attachRibbon(popupRef, assetBase + "/" + branch + "/previews/preview-");
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
