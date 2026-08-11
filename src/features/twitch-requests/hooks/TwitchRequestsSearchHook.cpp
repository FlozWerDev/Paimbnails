#include "../TwitchRequestManager.hpp"
#include "../ui/TwitchRequestsLayer.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../framework/compat/SceneLocators.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

// Boton de plantilla del juego (base circular morada) con el contador de la
// cola en la esquina. Vive en la pantalla de busqueda online, junto al resto de
// filtros, que es desde donde se abren los niveles pedidos.
class $modify(PaimonTwitchRequestsSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelSearchLayer::init");
    }

    struct Fields {
        Ref<CCLabelBMFont> m_countLabel;
    };

    $override
    bool init(int type) {
        if (!LevelSearchLayer::init(type)) return false;
        if (!Mod::get()->getSettingValue<bool>("twitch-requests-enabled")) return true;

        CCNode* menu = this->getChildByID("other-filter-menu");
        if (!menu) menu = paimon::compat::LevelBrowserLocator::findSearchMenu(this);
        if (!menu || menu->getChildByID("twitch-requests-button"_spr)) return true;

        char const* iconName = "GJ_chatBtn_001.png";
        if (!paimon::SpriteHelper::safeCreateWithFrameName(iconName)) {
            iconName = "GJ_starsIcon_001.png";
        }
        auto* base = CircleButtonSprite::createWithSpriteFrameName(
            iconName, 1.f, CircleBaseColor::DarkPurple, CircleBaseSize::MediumAlt);
        if (!base) return true;

        // Los botones de esta fila miden ~35px; la base MediumAlt es mas grande.
        float const baseWidth = base->getContentSize().width;
        if (baseWidth > 0.f) base->setScale(36.f / baseWidth);

        auto* button = CCMenuItemExt::createSpriteExtra(base,
            [](CCMenuItemSpriteExtra*) {
                paimon::twitch::TwitchRequestsLayer::open();
            });
        button->setID("twitch-requests-button"_spr);

        auto* count = CCLabelBMFont::create("", "bigFont.fnt");
        count->setScale(.34f);
        count->setColor({255, 220, 80});
        count->setPosition({
            base->getContentSize().width - 6.f,
            base->getContentSize().height - 8.f,
        });
        base->addChild(count, 10);
        m_fields->m_countLabel = count;

        menu->addChild(button);
        menu->updateLayout();

        updateRequestCount(0.f);
        schedule(schedule_selector(PaimonTwitchRequestsSearchLayer::updateRequestCount), .5f);
        return true;
    }

    void updateRequestCount(float) {
        if (!m_fields->m_countLabel) return;
        size_t count = paimon::twitch::TwitchRequestManager::get().requestCount();
        m_fields->m_countLabel->setString(count == 0 ? "" : fmt::format("{}", count).c_str());
    }
};
