#include "QuickHubRadial.hpp"
#include "RadialVisuals.hpp"
#include "../services/QuickHubManager.hpp"
#include "../services/QuickHubButtonCapture.hpp"
#include "../data/QuickHubCategories.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include "../../../layers/PaimonHubLayer.hpp"
#include "../../../layers/PaiConfigLayer.hpp"
#include "../../../layers/PaimonSupportLayer.hpp"
#include "../../../features/settings-panel/services/SettingsPanelManager.hpp"
#include "../../../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../../../features/pet/ui/PetConfigPopup.hpp"
#include "../../../features/pet/ui/PaimonShopPopup.hpp"
#include "../../../features/cursor/ui/CursorConfigPopup.hpp"
#include "../../../features/progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../../../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../../../features/custom-slider/ui/CustomSliderPopup.hpp"
#include "../../../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../../../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../../../features/menu-music/ui/MenuMusicPopup.hpp"
#include "../../../features/menu-music/ui/MenuMusicLibraryPopup.hpp"
#include "../../../features/menu-music/ui/MenuMusicPlaylistsPopup.hpp"
#include "../../../features/paidraw/PaiDrawUI.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::quickhub {

QuickHubRadial* QuickHubRadial::s_instance = nullptr;


bool QuickHubRadial::isOpen() {
    return s_instance != nullptr;
}

void QuickHubRadial::openRadial() {
    if (!paimon::modules::isEnabled("paimbnails.quickhub.global")) return;
    if (s_instance) return;

    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto radial = QuickHubRadial::create();
    if (!radial) return;

    scene->addChild(radial, 99998);
    s_instance = radial;

    // Reuse the popup blur effect.
    bool blurApplied = paimon::popupblur::captureAndApply(radial);

    // Keep a dark fallback when blur is disabled.
    if (!blurApplied) {
        auto winSize = CCDirector::get()->getWinSize();
        auto fallback = CCLayerColor::create({8, 10, 18, 0});
        fallback->setContentSize(winSize);
        fallback->setID("paimon-radial-fallback-overlay"_spr);
        radial->addChild(fallback, -1);
        fallback->runAction(CCFadeTo::create(0.25f, 200));
    }
}

void QuickHubRadial::closeRadial() {
    if (!s_instance) return;
    s_instance->animateClose();
}


QuickHubRadial* QuickHubRadial::create() {
    auto ret = new QuickHubRadial();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}


bool QuickHubRadial::init() {
    if (!CCLayer::init()) return false;

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-1000);
    this->setKeypadEnabled(true);

    m_center = CCDirector::get()->getWinSize() / 2.f;

    buildRadialItems();
    buildBackdrop();
    animateOpen();

    this->scheduleUpdate();

    QuickHubManager::get().setRadialOpen(true);

    return true;
}

void QuickHubRadial::onExit() {
    // Clean up blur on an abrupt removal.
    paimon::popupblur::cleanup(this);

    CCLayer::onExit();
    if (s_instance == this) {
        s_instance = nullptr;
    }
    QuickHubManager::get().setRadialOpen(false);
}


void QuickHubRadial::update(float dt) {
#ifdef GEODE_IS_DESKTOP
    // Geode converts the mouse to design-space coordinates, matching item hit tests
    // across window and design resolutions.
    updateHover(getHoveredIndex(geode::cocos::getMousePos()));
#endif
}


void QuickHubRadial::buildRadialItems() {
    auto activeIds = QuickHubManager::get().getActiveOptions();
    auto allOpts = QuickHubManager::get().getAllRadialOptions();
    auto customButtons = QuickHubManager::get().getCustomButtons();

    std::vector<RadialOptionDef const*> activeOptions;
    activeOptions.reserve(activeIds.size());
    for (auto const& id : activeIds) {
        auto found = std::ranges::find(allOpts, id, &RadialOptionDef::id);
        if (found != allOpts.end()) activeOptions.push_back(&*found);
    }

    int count = static_cast<int>(activeOptions.size());
    if (count == 0) return;

    auto geometry = radialGeometryFor(count, CCDirector::get()->getWinSize());
    m_radius = geometry.radius;
    m_badgeSize = geometry.badgeSize;

    for (int i = 0; i < count; i++) {
        auto const* def = activeOptions[i];

        RadialItem item;
        item.id = def->id;
        item.name = def->name;
        item.color = def->color;
        item.angle = radialAngleFor(i, count);
        item.reachable = true;

        auto shape = RadialButtonShape::Circle;
        if (def->custom) {
            auto saved = std::ranges::find(customButtons, def->id, &CustomQuickButton::id);
            if (saved != customButtons.end()) shape = saved->shape;
            item.reachable = isCustomQuickButtonReachable(def->id);
            if (!item.reachable && saved != customButtons.end()) {
                item.hint = fmt::format("En {}", friendlyScreenName(saved->sceneClass));
            }
        }

        float angleRad = item.angle * (static_cast<float>(M_PI) / 180.f);
        item.position = ccp(
            m_center.x + cosf(angleRad) * m_radius,
            m_center.y + sinf(angleRad) * m_radius);

        auto* itemNode = CCNode::create();
        itemNode->setPosition(m_center);
        this->addChild(itemNode, 5);
        item.node = itemNode;

        // La escala del hover vive en un hijo para que no cancele el movimiento
        // de apertura, que corre sobre el contenedor.
        auto badge = makeRadialBadge(*def, shape, m_badgeSize, !item.reachable);
        badge.root->setScale(0.f);
        itemNode->addChild(badge.root);
        item.inner = badge.root;
        item.ring = badge.ring;

        m_items.push_back(item);
    }
}

// Sin plato ni corona: el desenfoque de fondo ya separa la rueda de la escena.
// Solo queda un disco discreto que sostiene el texto del centro.
void QuickHubRadial::buildBackdrop() {
    if (m_items.empty()) return;

    float hubRadius = std::max(34.f, m_radius - m_badgeSize * 0.85f);
    m_deadZone = hubRadius;

    m_hub = CCNode::create();
    m_hub->setPosition(m_center);
    this->addChild(m_hub, 10);

    if (auto* disc = makeCircle(hubRadius, kRadialHubFill)) {
        m_hub->addChild(disc, 0);
    }

    m_nameLabel = CCLabelBMFont::create("Quick Hub", "goldFont.fnt");
    m_nameLabel->setPosition({0.f, 5.f});
    m_nameLabel->limitLabelWidth(hubRadius * 1.6f, 0.45f, 0.14f);
    m_hub->addChild(m_nameLabel, 2);

    m_hintLabel = CCLabelBMFont::create("Esc para cerrar", "chatFont.fnt");
    m_hintLabel->setColor(kRadialHintColor);
    m_hintLabel->setPosition({0.f, -12.f});
    m_hintLabel->limitLabelWidth(hubRadius * 1.6f, 0.32f, 0.12f);
    m_hub->addChild(m_hintLabel, 2);

    m_hub->setScale(0.f);
}


void QuickHubRadial::animateOpen() {
    if (m_hub) {
        m_hub->stopAllActions();
        m_hub->runAction(CCEaseBackOut::create(CCScaleTo::create(0.28f, 1.f)));
    }

    // El desplazamiento va en el contenedor y la escala en el hijo, para que el
    // hover no pueda interrumpir la apertura.
    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        float delay = 0.025f * static_cast<float>(i);

        if (item.node) {
            item.node->stopAllActions();
            item.node->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseBackOut::create(CCMoveTo::create(0.3f, item.position)),
                nullptr
            ));
        }
        if (item.inner) {
            item.inner->stopAllActions();
            item.inner->setScale(0.f);
            item.inner->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.0f)),
                nullptr
            ));
        }
    }
}

void QuickHubRadial::animateClose() {
    paimon::popupblur::cleanupWithFade(this, 0.25f);

    this->unscheduleUpdate();
    if (m_hub) {
        m_hub->stopAllActions();
        m_hub->runAction(CCEaseBackIn::create(CCScaleTo::create(0.18f, 0.f)));
    }

    float maxDelay = 0.f;
    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        float delay = 0.02f * static_cast<float>(i);
        maxDelay = std::max(maxDelay, delay + 0.2f);

        if (item.node) {
            item.node->stopAllActions();
            item.node->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseBackIn::create(CCMoveTo::create(0.2f, m_center)),
                nullptr
            ));
        }
        if (item.inner) {
            item.inner->stopAllActions();
            item.inner->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCScaleTo::create(0.2f, 0.f),
                nullptr
            ));
        }
    }

    this->runAction(CCSequence::create(
        CCDelayTime::create(maxDelay + 0.05f),
        CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

    // Actions run on ccTouchEnded; dragging over items does not activate them.

bool QuickHubRadial::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    return true;
}

void QuickHubRadial::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    auto worldPos = touch->getLocation();
    int hovered = getHoveredIndex(worldPos);
    updateHover(hovered);
}

void QuickHubRadial::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    auto worldPos = touch->getLocation();
    int hovered = getHoveredIndex(worldPos);

    if (hovered >= 0) {
        executeOption(hovered);
    } else {
    // Close when clicking outside every item.
        animateClose();
    }
}

void QuickHubRadial::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
}

void QuickHubRadial::keyBackClicked() {
    animateClose();
}


// Seleccion por sector: importa hacia donde apuntas, no si aciertas el icono.
int QuickHubRadial::getHoveredIndex(CCPoint const& worldPos) {
    int count = static_cast<int>(m_items.size());
    if (count == 0) return -1;

    float dx = worldPos.x - m_center.x;
    float dy = worldPos.y - m_center.y;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < m_deadZone) return -1;                        // centro: cancelar
    if (dist > m_radius + m_badgeSize * 1.4f) return -1;     // lejos: cerrar

    float angleDeg = std::atan2(dy, dx) * (180.f / static_cast<float>(M_PI));
    float step = 360.f / static_cast<float>(count);

    // Los items van de 90 grados hacia atras; convertir a "vueltas horarias".
    float fromTop = std::fmod(90.f - angleDeg + 360.f, 360.f);
    int index = static_cast<int>(std::floor(fromTop / step + 0.5f)) % count;
    return index;
}

void QuickHubRadial::updateHover(int index) {
    if (index == m_hoveredIndex) return;
    m_hoveredIndex = index;

    // La opcion apuntada crece y estrena aro; el resto se queda plano.
    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        if (!item.inner) continue;
        bool isHovered = (static_cast<int>(i) == index);

        item.inner->stopAllActions();
        item.inner->runAction(CCEaseSineOut::create(
            CCScaleTo::create(0.12f, isHovered ? 1.16f : 1.0f)));
        if (item.ring) item.ring->setVisible(isHovered);
    }

    if (!m_nameLabel || !m_hintLabel) return;

    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        auto const& item = m_items[index];
        m_nameLabel->setString(item.name.c_str());
        m_nameLabel->setColor(item.reachable ? item.color : ccColor3B{170, 170, 180});
        m_hintLabel->setString(item.hint.empty() ? "Click para abrir" : item.hint.c_str());
        m_hintLabel->setColor(item.hint.empty() ? kRadialHintColor : ccColor3B{255, 190, 120});
    } else {
        m_nameLabel->setString("Quick Hub");
        m_nameLabel->setColor({255, 255, 255});
        m_hintLabel->setString("Esc para cerrar");
        m_hintLabel->setColor(kRadialHintColor);
    }

    float hubWidth = (m_deadZone - 4.f) * 1.7f;
    m_nameLabel->limitLabelWidth(hubWidth, 0.5f, 0.16f);
    m_hintLabel->limitLabelWidth(hubWidth, 0.34f, 0.14f);
}


void QuickHubRadial::executeOption(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;

    std::string id = m_items[index].id;

    animateClose();

    Loader::get()->queueInMainThread([id]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (id.starts_with("custom:")) {
            activateCustomQuickButton(id);
            return;
        }
        if (id == "settings-general")          { SettingsPanelManager::get().open(0); return; }
        if (id == "settings-thumbnails")       { SettingsPanelManager::get().open(1); return; }
        if (id == "settings-levelinfo")        { SettingsPanelManager::get().open(2); return; }
        if (id == "settings-audio")            { SettingsPanelManager::get().open(3); return; }
        if (id == "settings-backgrounds")      { SettingsPanelManager::get().open(4); return; }
        if (id == "settings-extras")           { SettingsPanelManager::get().open(5); return; }
        if (id == "settings-discord")          { SettingsPanelManager::get().open(6); return; }

        if (id == "general")      { SettingsPanelManager::get().open(0); return; }
        if (id == "thumbnails")   { SettingsPanelManager::get().open(1); return; }
        if (id == "level")        { SettingsPanelManager::get().open(2); return; }
        if (id == "audio")        { SettingsPanelManager::get().open(3); return; }
        if (id == "extras")       { SettingsPanelManager::get().open(5); return; }
        if (id == "quick-toggle") { SettingsPanelManager::get().open(0); return; }
        if (id == "backgrounds")  {
            if (auto scene = PaiConfigLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
            return;
        }
        if (id == "discord")      {
            if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show();
            return;
        }

        if (id == "backgrounds-editor") {
            if (auto scene = PaiConfigLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
            return;
        }
        if (id == "transitions") {
            if (auto popup = TransitionConfigPopup::create()) popup->show();
            return;
        }
        if (id == "discord-config") {
            if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show();
            return;
        }
        if (id == "pet-config") {
            if (auto popup = PetConfigPopup::create()) popup->show();
            return;
        }
        if (id == "cursor-config") {
            if (auto popup = CursorConfigPopup::create()) popup->show();
            return;
        }
        if (id == "slider-config") {
            if (auto popup = paimon::slider::CustomSliderPopup::create()) popup->show();
            return;
        }
        if (id == "progressbar-config") {
            if (auto popup = ProgressBarConfigPopup::create()) popup->show();
            return;
        }
        if (id == "profile-pic-editor") {
            if (auto popup = ProfilePicEditorPopup::create()) popup->show();
            return;
        }

        if (id == "menu-music") {
            if (auto popup = paimon::menumusic::MenuMusicPopup::create()) popup->show();
            return;
        }
        if (id == "menu-music-library") {
            if (auto popup = paimon::menumusic::MenuMusicLibraryPopup::create()) popup->show();
            return;
        }
        if (id == "menu-music-playlists") {
            if (auto popup = paimon::menumusic::MenuMusicPlaylistsPopup::create()) popup->show();
            return;
        }
        if (id == "profile-music") {
            auto* acc = GJAccountManager::sharedState();
            int accountID = acc ? acc->m_accountID : 0;
            if (accountID > 0) {
                if (auto popup = ProfileMusicPopup::create(accountID)) popup->show();
            } else {
                PaimonNotify::create("Necesitas iniciar sesion.", NotificationIcon::Warning)->show();
            }
            return;
        }

        if (id == "pet-shop") {
            if (auto popup = PaimonShopPopup::create()) popup->show();
            return;
        }

        if (id == "hub") {
            if (auto scene = PaimonHubLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
            return;
        }
        if (id == "paidraw") {
            if (auto scene = paidraw::PaiDrawLobbyLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
            return;
        }
        if (id == "support") {
            if (auto scene = PaimonSupportLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
            return;
        }
        if (id == "full-config") {
            if (auto scene = PaiConfigLayer::scene()) {
                CCDirector::get()->pushScene(scene);
            }
            return;
        }

        log::warn("QuickHubRadial: id sin accion mapeada: '{}'", id);
    });
}

}
