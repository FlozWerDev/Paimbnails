#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "../services/VersusSession.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using namespace paimon::versus;

class $modify(PaimonVersusPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "PauseLayer::customSetup");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        auto& session = VersusSession::get();
        if (!session.inLevel() || session.phase() != Phase::Running) return;

        // no-pause: the menu closes itself again, so the only ways out of the
        // duel are finishing it or forfeiting.
        if (session.hasMutator("no-pause")) {
            PaimonNotify::show(Localization::get().getString("versus.mutator.no-pause").c_str(),
                               NotificationIcon::Warning);
            auto self = Ref<PaimonVersusPauseLayer>(this);
            paimon::scheduleMainThreadDelay(0.f, [self]() {
                if (self->isRunning()) self->onResume(nullptr);
            });
            return;
        }

        auto const winSize = CCDirector::get()->getWinSize();

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("versus-pause-menu"_spr);
        this->addChild(menu, 100);

        auto* face = ButtonSprite::create(
            Localization::get().getString("versus.pause.forfeit").c_str(), 120, true,
            "bigFont.fnt", "GJ_button_06.png", 28.f, 0.5f);
        auto* forfeit = CCMenuItemSpriteExtra::create(
            face, this, menu_selector(PaimonVersusPauseLayer::onPaimonForfeit));
        forfeit->setPosition({winSize.width / 2.f, 34.f});
        menu->addChild(forfeit);

        auto* note = CCLabelBMFont::create(
            Localization::get().getString("versus.pause.note").c_str(), "chatFont.fnt");
        note->setScale(0.42f);
        note->setOpacity(160);
        note->setPosition({winSize.width / 2.f, 14.f});
        this->addChild(note, 100);
    }

    void onPaimonForfeit(CCObject*) {
        // quitting right after is the point: the server already holds the
        // forfeit, so PlayLayer::onExit has nothing left to rule on.
        VersusSession::get().forfeit();
        this->onQuit(nullptr);
    }
};
