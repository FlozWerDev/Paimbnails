#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "../layers/PaimonSupportLayer.hpp"
#include "../features/transitions/services/TransitionManager.hpp"

using namespace geode::prelude;

// Handler del boton support
class SupportButtonHandler : public CCNode {
public:
    static SupportButtonHandler* create() {
        auto ret = new SupportButtonHandler();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onSupportClicked(CCObject*) {
        TransitionManager::get().pushScene(
            PaimonSupportLayer::scene()
        );
    }
};

$execute {
    // Reemplaza el boton support del popup del mod
    static auto handle = ModPopupUIEvent().listen(
        +[](FLAlertLayer* popup, std::string_view modID, std::optional<Mod*>) -> ListenerResult {
            if (modID != Mod::get()->getID()) {
                return ListenerResult::Propagate;
            }

            // Busca el boton en links-container
            auto linksMenu = popup->querySelector("links-container");
            if (!linksMenu) return ListenerResult::Propagate;

            auto supportBtn = linksMenu->getChildByID("support");
            if (!supportBtn) return ListenerResult::Propagate;

            auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(supportBtn);
            if (!menuItem) return ListenerResult::Propagate;

            // Evita duplicar el handler
            if (menuItem->getChildByID("support-handler"_spr)) {
                return ListenerResult::Propagate;
            }

            // Handler como hijo del boton
            auto handler = SupportButtonHandler::create();
            handler->setID("support-handler"_spr);
            menuItem->addChild(handler);

            // Redirige al handler personalizado
            menuItem->setTarget(handler, menu_selector(SupportButtonHandler::onSupportClicked));
            menuItem->setEnabled(true);

            return ListenerResult::Propagate;
        }
    );
}
