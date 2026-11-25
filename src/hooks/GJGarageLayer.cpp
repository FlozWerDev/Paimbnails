#include <Geode/modify/GJGarageLayer.hpp>
#include "../framework/HookConventions.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/colorful-icons/hooks/PaimonIconsGarageGlue.hpp"
#include "../features/garage-hub/GarageButtonHub.hpp"
#include "../features/icon-copy/hooks/IconCopyGarageGlue.hpp"
#include "../features/icon-maker/hooks/IconMakerGarageGlue.hpp"
#include "../features/icon-maker/services/IconApplier.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

class $modify(PaimonGJGarageLayer, GJGarageLayer) {
    struct Fields {
        bool enteredOnce = false;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJGarageLayer::init");
    }

    $override
    bool init() {
        if (!GJGarageLayer::init()) return false;
        LayerBackgroundManager::get().applyBackground(this, "garage");
        // gear button + re-color on config change; icon maker hangs off the same popup
        paimon::icons::garage::onGarageInit(this);
        paimon::iconcopy::garage::onGarageInit(this);
        // accesses hang off the hub; this is the only visible button
        paimon::garage_hub::installHubButton(this);
        return true;
    }

    $override
    void onEnter() {
        GJGarageLayer::onEnter();
        if (m_fields->enteredOnce && !paimon::isRuntimeShuttingDown()) {
            Ref<GJGarageLayer> ref = this;
            Loader::get()->queueInMainThread([ref]() {
                if (paimon::isRuntimeShuttingDown() || !ref || !ref->getParent()) return;
                paimon::icon_maker::garage::refreshGarage(ref);
            });
        }
        m_fields->enteredOnce = true;
    }

    $override
    void playerColorChanged() {
        GJGarageLayer::playerColorChanged();
        paimon::icons::garage::onPlayerColorChanged(this);
        paimon::icon_maker::garage::onPlayerColorChanged(this);
    }

    void onSelectTab(cocos2d::CCObject* sender) {
        GJGarageLayer::onSelectTab(sender);
        paimon::icons::garage::onPlayerColorChanged(this);
    }

    void onSelect(cocos2d::CCObject* sender) {
        paimon::icon_maker::garage::onVanillaIconSelected(this, sender);
        GJGarageLayer::onSelect(sender);
    }

    void onArrow(cocos2d::CCObject* sender) {
        int const totalPages = paimon::icon_maker::garage::totalPagesFor(m_iconType);
        if (totalPages > 0) {
            int delta = 0;
            if (sender == m_leftArrow) delta = -1;
            else if (sender == m_rightArrow) delta = 1;
            else if (sender) delta = sender->getTag();
            int cur = 0;
            if (auto it = m_iconPages.find(m_iconType); it != m_iconPages.end()) {
                cur = it->second;
            }
            int const next = ((cur + delta) % totalPages + totalPages) % totalPages;
            setupPage(next, m_iconType);
            return;
        }
        GJGarageLayer::onArrow(sender);
    }

    void onNavigate(cocos2d::CCObject* sender) {
        int const totalPages = paimon::icon_maker::garage::totalPagesFor(m_iconType);
        if (totalPages > 0 && sender) {
            int const target = ((sender->getTag() % totalPages) + totalPages) % totalPages;
            setupPage(target, m_iconType);
            return;
        }
        GJGarageLayer::onNavigate(sender);
    }

    void setupPage(int page, IconType type) {
        int const totalPages = paimon::icon_maker::garage::totalPagesFor(type);
        int const vanillaPages = paimon::icon_maker::garage::vanillaPagesFor(type);
        bool const extraCustomPage = (totalPages > 0) && (page >= vanillaPages);

        {
            // Keep vanilla icon grid items strictly vanilla while building the page.
            paimon::icon_maker::IconApplier::VanillaScope vanillaGuard;
            GJGarageLayer::setupPage(extraCustomPage ? 0 : page, type);
        }

        if (totalPages > 0) {
            paimon::icon_maker::garage::injectCustomIconsOnPage(
                this, page, type, extraCustomPage);
        }
        paimon::icons::garage::onPlayerColorChanged(this);
    }
};
