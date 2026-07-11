#include "CollabPopups.hpp"

#include "../../utils/DynamicPopupRegistry.hpp"
#include "CollabEmotes.hpp"
#include "CollabManager.hpp"
#include "CollabVoice.hpp"
#include "../../utils/AccountVerifier.hpp"
#include "../emotes/ui/EmoteButton.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/random.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::collab {
namespace {

std::string trim(std::string value) {
    geode::utils::string::trimIP(value);
    return value;
}

// Room codes are case-insensitive and separator-agnostic. The displayed code
// groups characters with hyphens (PAIM-AB-CDE) but the hyphen glyph isn't in
// every input font, so strip anything non-alphanumeric and uppercase before
// matching. This way typing with or without the dashes resolves to the same room.
std::string normRoomCode(std::string value) {
    std::string out;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(c);
    }
    return geode::utils::string::toUpper(std::move(out));
}

CCMenuItemSpriteExtra* makeButton(CCObject* target, SEL_MenuHandler selector, char const* text, char const* bg = "GJ_button_01.png", float scale = 0.7f) {
    auto* spr = ButtonSprite::create(text, "goldFont.fnt", bg, scale);
    return CCMenuItemSpriteExtra::create(spr, target, selector);
}

void showAlert(std::string const& message) {
    auto popup = PopupManager::get().alert("Collab Editor", message);
    popup.setPriority(true);
    popup.showQueue();
}

std::string randomRoomCode() {
    // 12 base32 characters = 60 bits of entropy. This makes room-code
    // guessing impractical even if the public join endpoint is probed.
    // Exclude I/L/O/U to avoid ambiguous room codes.
    auto code = geode::utils::random::generateString(12, "0123456789ABCDEFGHJKMNPQRSTVWXYZ");
    std::string out = "PAIM-";
    for (size_t i = 0; i < code.size(); ++i) {
        if (i > 0 && i % 4 == 0) out.push_back('-');
        out.push_back(code[i]);
    }
    return out;
}

// Dark card that groups the "one thing to do" of each view.
CCScale9Sprite* makeCard(float width, float height) {
    auto* card = CCScale9Sprite::create("square02b_001.png");
    card->setContentSize({width, height});
    card->setColor({0, 0, 0});
    card->setOpacity(75);
    return card;
}

CCLabelBMFont* makeCaption(char const* text) {
    auto* label = CCLabelBMFont::create(text, "goldFont.fnt");
    label->setScale(0.45f);
    return label;
}

CCLabelBMFont* makeHint(char const* text) {
    auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setScale(0.45f);
    label->setColor({165, 165, 178});
    return label;
}

IconType peerIconType(int rawType) {
    switch (rawType) {
        case static_cast<int>(IconType::Cube): return IconType::Cube;
        case static_cast<int>(IconType::Ship): return IconType::Ship;
        case static_cast<int>(IconType::Ball): return IconType::Ball;
        case static_cast<int>(IconType::Ufo): return IconType::Ufo;
        case static_cast<int>(IconType::Wave): return IconType::Wave;
        case static_cast<int>(IconType::Robot): return IconType::Robot;
        case static_cast<int>(IconType::Spider): return IconType::Spider;
        case static_cast<int>(IconType::Swing): return IconType::Swing;
        case static_cast<int>(IconType::Jetpack): return IconType::Jetpack;
        default: return IconType::Cube;
    }
}

std::string peerSignature(std::vector<PeerInfo> const& peers) {
    std::string signature;
    signature.reserve(peers.size() * 48);
    for (auto const& peer : peers) {
        auto const& a = peer.appearance;
        signature += fmt::format(
            "{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{};",
            peer.clientId, peer.isHost, peer.username, a.accountID, a.iconID,
            a.iconType, a.color1, a.color2, a.glowColor, a.glowEnabled, a.hasIcon
        );
    }
    return signature;
}

CCNode* makePeerAvatar(PeerInfo const& peer, ccColor3B fallbackColor, float kSize = 34.f) {
    auto* root = CCNode::create();
    root->setContentSize({kSize, kSize});
    root->setAnchorPoint({0.5f, 0.5f});

    auto* ring = CCDrawNode::create();
    ring->drawDot({kSize / 2.f, kSize / 2.f}, kSize / 2.f,
        ccColor4F{fallbackColor.r / 255.f, fallbackColor.g / 255.f, fallbackColor.b / 255.f, 1.f});
    ring->drawDot({kSize / 2.f, kSize / 2.f}, kSize / 2.f - 2.f,
        ccColor4F{0.06f, 0.07f, 0.11f, 1.f});
    root->addChild(ring);

    int iconID = peer.appearance.hasIcon ? std::max(1, peer.appearance.iconID) : 1;
    if (auto* player = SimplePlayer::create(iconID)) {
        IconType type = peer.appearance.hasIcon ? peerIconType(peer.appearance.iconType) : IconType::Cube;
        if (type != IconType::Cube) player->updatePlayerFrame(iconID, type);

        ccColor3B primary = fallbackColor;
        ccColor3B secondary = {35, 38, 52};
        if (peer.appearance.hasIcon) {
            if (auto* gm = GameManager::get()) {
                primary = gm->colorForIdx(std::clamp(peer.appearance.color1, 0, 1000));
                secondary = gm->colorForIdx(std::clamp(peer.appearance.color2, 0, 1000));
                if (peer.appearance.glowEnabled) {
                    player->setGlowOutline(gm->colorForIdx(std::clamp(peer.appearance.glowColor, 0, 1000)));
                } else {
                    player->disableGlowOutline();
                }
            }
        } else {
            player->disableGlowOutline();
        }
        player->setColor(primary);
        player->setSecondColor(secondary);

        float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
        if (maxDim > 0.f) player->setScale((kSize - 11.f) / maxDim);
        player->setPosition({kSize / 2.f, kSize / 2.f});
        root->addChild(player, 1);
    }

    return root;
}

} // namespace

std::string defaultDisplayName() {
    std::string fromSetting = trim(Mod::get()->getSettingValue<std::string>("collab-username"));
    if (!fromSetting.empty()) return fromSetting;
    if (auto* gm = GameManager::get()) {
        std::string name = gm->m_playerName;
        if (!name.empty()) return name;
    }
    return "editor";
}

void closeSessionPopups() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    std::vector<Ref<FLAlertLayer>> victims;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child) continue;
        auto const& id = child->getID();
        if (id == "collab-room"_spr || id == "collab-gate"_spr) {
            if (auto* alert = typeinfo_cast<FLAlertLayer*>(child)) victims.emplace_back(alert);
        }
    }
    // keyBackClicked routes through Popup::onClose, which unregisters touch
    // and keyboard handlers properly (a plain removeFromParent would not).
    for (auto const& alert : victims) alert->keyBackClicked();
}

// ---------------------------------------------------------------------------
// Room popup
// ---------------------------------------------------------------------------

CollabRoomPopup* CollabRoomPopup::create(GJGameLevel* hostLevel) {
    auto* ret = new CollabRoomPopup();
    if (ret && ret->init(hostLevel)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabRoomPopup::init(GJGameLevel* hostLevel) {
    if (!Popup::init(340.f, 210.f)) return false;
    m_hostLevel = hostLevel;
    paimon::markDynamicPopup(this);
    setID("collab-room"_spr);
    setTitle("Collab Editor");

    m_content = CCNode::create();
    m_content->setContentSize(m_mainLayer->getContentSize());
    m_content->setAnchorPoint({0.5f, 0.5f});
    m_mainLayer->addChildAtPosition(m_content, Anchor::Center);

    auto& mgr = CollabManager::get();
    m_createCode = (mgr.connected() && mgr.isHost()) ? mgr.roomCode() : randomRoomCode();

    rebuild();
    this->schedule(schedule_selector(CollabRoomPopup::refresh), 0.25f);
    return true;
}

void CollabRoomPopup::captureInputs() {
    if (m_codeInput) m_joinCode = trim(std::string(m_codeInput->getString()));
}

void CollabRoomPopup::rebuild() {
    captureInputs();
    m_content->removeAllChildrenWithCleanup(true);
    m_codeInput = nullptr;
    m_codeLabel = nullptr;
    m_statusLabel = nullptr;
    m_peersLabel = nullptr;

    switch (CollabManager::get().state()) {
        case ConnState::Connected:
            m_view = View::Connected;
            buildConnectedView();
            break;
        case ConnState::Connecting:
            m_view = View::Connecting;
            buildConnectingView();
            break;
        default:
            m_view = View::Setup;
            buildSetupView();
            break;
    }
}

void CollabRoomPopup::scheduleRebuild() {
    // Rebuilding destroys the button whose callback we're inside of; defer a
    // frame so cocos never touches a freed menu item.
    Ref<CollabRoomPopup> self = this;
    queueInMainThread([self]() {
        if (self->getParent()) self->rebuild();
    });
}

void CollabRoomPopup::buildSetupView() {
    // --- Tabs: the two things you can do, side by side ----------------------
    auto* tabs = CCMenu::create();
    auto* createSpr = ButtonSprite::create("Crear sala", "bigFont.fnt",
        m_joinTab ? "GJ_button_04.png" : "GJ_button_01.png", 0.6f);
    createSpr->setScale(0.62f);
    auto* joinSpr = ButtonSprite::create("Unirse", "bigFont.fnt",
        m_joinTab ? "GJ_button_01.png" : "GJ_button_04.png", 0.6f);
    joinSpr->setScale(0.62f);
    tabs->addChild(CCMenuItemSpriteExtra::create(createSpr, this, menu_selector(CollabRoomPopup::onTabCreate)));
    tabs->addChild(CCMenuItemSpriteExtra::create(joinSpr, this, menu_selector(CollabRoomPopup::onTabJoin)));
    tabs->setLayout(RowLayout::create()->setGap(8.f));
    tabs->updateLayout();
    m_content->addChildAtPosition(tabs, Anchor::Top, {0.f, -40.f});

    // --- Card with the tab's single task ------------------------------------
    m_content->addChildAtPosition(makeCard(304.f, 100.f), Anchor::Center, {0.f, -2.f});

    if (!m_joinTab) {
        m_content->addChildAtPosition(makeCaption("Codigo de tu sala"), Anchor::Center, {0.f, 34.f});

        m_codeLabel = CCLabelBMFont::create(m_createCode.c_str(), "bigFont.fnt");
        m_codeLabel->setScale(0.6f);
        m_codeLabel->setColor({255, 210, 90});
        m_content->addChildAtPosition(m_codeLabel, Anchor::Center, {0.f, 12.f});

        auto* codeMenu = CCMenu::create();
        codeMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onGenerate), "Otro", "GJ_button_05.png", 0.42f));
        codeMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onCopy), "Copiar", "GJ_button_02.png", 0.42f));
        codeMenu->setLayout(RowLayout::create()->setGap(6.f));
        codeMenu->updateLayout();
        m_content->addChildAtPosition(codeMenu, Anchor::Center, {0.f, -16.f});

        auto* hint = makeHint("Comparte este codigo con tus amigos para que se unan");
        hint->limitLabelWidth(290.f, 0.45f, 0.2f);
        m_content->addChildAtPosition(hint, Anchor::Center, {0.f, -40.f});
    } else {
        m_content->addChildAtPosition(makeCaption("Codigo del host"), Anchor::Center, {0.f, 34.f});

        m_codeInput = TextInput::create(185.f, "PAIM-XXXX-XXXX-XXXX", "bigFont.fnt");
        m_codeInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ");
        m_codeInput->setMaxCharCount(24);
        m_codeInput->setString(m_joinCode);
        m_codeInput->setScale(0.8f);
        m_content->addChildAtPosition(m_codeInput, Anchor::Center, {-35.f, 6.f});

        auto* pasteMenu = CCMenu::create();
        pasteMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onPaste), "Pegar", "GJ_button_05.png", 0.42f));
        pasteMenu->setLayout(RowLayout::create());
        pasteMenu->updateLayout();
        m_content->addChildAtPosition(pasteMenu, Anchor::Center, {88.f, 6.f});

        auto* hint = makeHint("Pega el codigo que te compartio el host de la sala");
        hint->limitLabelWidth(290.f, 0.45f, 0.2f);
        m_content->addChildAtPosition(hint, Anchor::Center, {0.f, -40.f});
    }

    // --- The one big action button; the display name is the GD account name -
    auto* actionMenu = CCMenu::create();
    auto* actionSpr = ButtonSprite::create(
        m_joinTab ? "Unirse a la sala" : "Crear la sala",
        "goldFont.fnt", m_joinTab ? "GJ_button_02.png" : "GJ_button_01.png", 0.9f);
    actionSpr->setScale(0.78f);
    actionMenu->addChild(CCMenuItemSpriteExtra::create(actionSpr, this, menu_selector(CollabRoomPopup::onAction)));
    actionMenu->setLayout(RowLayout::create());
    actionMenu->updateLayout();
    m_content->addChildAtPosition(actionMenu, Anchor::Bottom, {0.f, 30.f});
}

void CollabRoomPopup::buildConnectingView() {
    auto* spinner = LoadingSpinner::create(46.f);
    m_content->addChildAtPosition(spinner, Anchor::Center, {0.f, 30.f});

    m_statusLabel = CCLabelBMFont::create(CollabManager::get().status().c_str(), "chatFont.fnt");
    m_statusLabel->setScale(0.55f);
    m_statusLabel->limitLabelWidth(300.f, 0.55f, 0.2f);
    m_content->addChildAtPosition(m_statusLabel, Anchor::Center, {0.f, -16.f});

    auto* menu = CCMenu::create();
    menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onCancel), "Cancelar", "GJ_button_06.png", 0.6f));
    menu->setLayout(RowLayout::create());
    menu->updateLayout();
    m_content->addChildAtPosition(menu, Anchor::Bottom, {0.f, 30.f});
}

void CollabRoomPopup::buildConnectedView() {
    auto& mgr = CollabManager::get();

    auto* header = CCLabelBMFont::create(
        mgr.isHost() ? "Sala activa (eres el host)" : "Conectado a la sala", "goldFont.fnt");
    header->setScale(0.5f);
    header->setColor({120, 255, 140});
    m_content->addChildAtPosition(header, Anchor::Top, {8.f, -40.f});

    auto* dot = CCDrawNode::create();
    dot->drawDot({0.f, 0.f}, 4.5f, ccColor4F{0.35f, 0.95f, 0.45f, 1.f});
    float halfHeader = header->getContentSize().width * header->getScale() / 2.f;
    m_content->addChildAtPosition(dot, Anchor::Top, {8.f - halfHeader - 10.f, -40.f});

    m_content->addChildAtPosition(makeCard(304.f, 96.f), Anchor::Center, {0.f, 0.f});

    m_content->addChildAtPosition(makeCaption("Codigo de la sala"), Anchor::Center, {-60.f, 36.f});

    m_codeLabel = CCLabelBMFont::create(mgr.roomCode().c_str(), "bigFont.fnt");
    m_codeLabel->setScale(0.5f);
    m_codeLabel->setColor({255, 210, 90});
    m_content->addChildAtPosition(m_codeLabel, Anchor::Center, {-40.f, 14.f});

    auto* copyMenu = CCMenu::create();
    copyMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onCopy), "Copiar", "GJ_button_02.png", 0.42f));
    copyMenu->setLayout(RowLayout::create());
    copyMenu->updateLayout();
    m_content->addChildAtPosition(copyMenu, Anchor::Center, {95.f, 18.f});

    m_peersLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_peersLabel->setScale(0.5f);
    m_content->addChildAtPosition(m_peersLabel, Anchor::Center, {0.f, -12.f});

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(0.45f);
    m_statusLabel->setColor({165, 165, 178});
    m_content->addChildAtPosition(m_statusLabel, Anchor::Center, {0.f, -32.f});

    auto* menu = CCMenu::create();
    menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onPeers), "Editores", "GJ_button_05.png", 0.52f));
    if (mgr.isHost()) {
        menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onInvite), "Invitar", "GJ_button_01.png", 0.52f));
        menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onHostOptions), "Permisos", "GJ_button_04.png", 0.52f));
    }
    menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onLeave),
        mgr.isHost() ? "Cerrar sala" : "Salir", "GJ_button_06.png", 0.52f));
    menu->setLayout(RowLayout::create()->setGap(6.f));
    menu->updateLayout();
    m_content->addChildAtPosition(menu, Anchor::Bottom, {0.f, 30.f});

    refresh();
}

void CollabRoomPopup::refresh(float) {
    auto& mgr = CollabManager::get();

    View want = View::Setup;
    if (mgr.state() == ConnState::Connected) want = View::Connected;
    else if (mgr.state() == ConnState::Connecting) want = View::Connecting;
    if (want != m_view) {
        rebuild();
        return;
    }

    if (m_view == View::Connecting && m_statusLabel) {
        m_statusLabel->setString(mgr.status().c_str());
        m_statusLabel->limitLabelWidth(300.f, 0.55f, 0.2f);
    }

    if (m_view == View::Connected) {
        if (m_peersLabel) {
            std::string names;
            auto peers = mgr.peers();
            for (auto const& peer : peers) {
                if (!names.empty()) names += ",  ";
                names += peer.username;
                if (peer.clientId == mgr.clientId()) names += " (tu)";
                else if (peer.isHost) names += " (host)";
            }
            std::string mode = mgr.isViewOnly() ? "  ·  solo lectura" : "";
            m_peersLabel->setString(fmt::format("Editores ({}): {}{}", peers.size(), names, mode).c_str());
            m_peersLabel->limitLabelWidth(285.f, 0.5f, 0.2f);
        }
        if (m_statusLabel) {
            m_statusLabel->setString(mgr.status().c_str());
            m_statusLabel->limitLabelWidth(285.f, 0.45f, 0.2f);
        }
    }
}

void CollabRoomPopup::onTabCreate(CCObject*) {
    if (!m_joinTab) return;
    captureInputs();
    m_joinTab = false;
    scheduleRebuild();
}

void CollabRoomPopup::onTabJoin(CCObject*) {
    if (m_joinTab) return;
    captureInputs();
    m_joinTab = true;
    scheduleRebuild();
}

void CollabRoomPopup::onGenerate(CCObject*) {
    m_createCode = randomRoomCode();
    if (m_codeLabel) m_codeLabel->setString(m_createCode.c_str());
}

void CollabRoomPopup::onCopy(CCObject*) {
    auto& mgr = CollabManager::get();
    std::string code = mgr.connected() ? mgr.roomCode() : m_createCode;
    if (code.empty()) return;
    geode::utils::clipboard::write(code);
    Notification::create("Codigo copiado", NotificationIcon::Success)->show();
}

void CollabRoomPopup::onPaste(CCObject*) {
    std::string clip = trim(geode::utils::clipboard::read());
    if (clip.empty() || clip.size() > 64 || normRoomCode(clip).empty()) {
        Notification::create("No hay un codigo en el portapapeles", NotificationIcon::Warning)->show();
        return;
    }
    m_joinCode = clip;
    if (m_codeInput) m_codeInput->setString(clip);
}

void CollabRoomPopup::onAction(CCObject*) {
    captureInputs();
    std::string name = defaultDisplayName();

    if (m_joinTab) {
        std::string room = normRoomCode(m_joinCode);
        if (room.empty()) {
            showAlert("Pega el codigo de la sala que te compartio el host.");
            return;
        }
        CollabManager::get().connect(room, name, ConnectMode::Join);
    } else {
        std::string room = normRoomCode(m_createCode);
        if (room.empty()) {
            m_createCode = randomRoomCode();
            room = normRoomCode(m_createCode);
        }
        CollabManager::get().connect(room, name, ConnectMode::Create, m_hostLevel);
    }
    scheduleRebuild(); // show the connecting view right away
}

void CollabRoomPopup::onCancel(CCObject*) {
    CollabManager::get().disconnect();
    scheduleRebuild();
}

void CollabRoomPopup::onLeave(CCObject*) {
    auto& mgr = CollabManager::get();
    if (mgr.isHost() && mgr.connected()) {
        mgr.closeRoom();
    } else {
        mgr.disconnect();
    }
    scheduleRebuild();
}

void CollabRoomPopup::onHostOptions(CCObject*) {
    if (!CollabManager::get().isHost()) return;
    if (auto* popup = HostOptionsPopup::create()) popup->show();
}

void CollabRoomPopup::onInvite(CCObject*) {
    auto& mgr = CollabManager::get();
    if (!mgr.connected() || !mgr.isHost()) {
        showAlert("Crea una sala como host para poder invitar.");
        return;
    }
    if (auto* popup = CollabInvitePopup::create()) popup->show();
}

void CollabRoomPopup::onPeers(CCObject*) {
    if (!CollabManager::get().connected()) return;
    if (auto* popup = CollabPeersPopup::create()) popup->show();
}

// ---------------------------------------------------------------------------
// Host options popup
// ---------------------------------------------------------------------------

HostOptionsPopup* HostOptionsPopup::create() {
    auto* ret = new HostOptionsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool HostOptionsPopup::init() {
    if (!Popup::init(300.f, 255.f)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Permisos de la sala");
    m_permissions = CollabManager::get().permissions();

    auto* hint = makeHint("Elige que pueden tocar los demas");
    m_mainLayer->addChildAtPosition(hint, Anchor::Top, {0.f, -38.f});

    m_mainLayer->addChildAtPosition(makeCard(264.f, 140.f), Anchor::Center, {0.f, 8.f});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(menu);

    auto addRow = [&](float yOff, int tag, char const* text, ButtonSprite** out) {
        auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
        label->setScale(0.52f);
        label->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(label, Anchor::Center, {-118.f, yOff});

        auto* spr = ButtonSprite::create("NO", "bigFont.fnt", "GJ_button_06.png", 0.8f);
        spr->setScale(0.5f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HostOptionsPopup::onToggle));
        btn->setTag(tag);
        auto size = m_mainLayer->getContentSize();
        btn->setPosition({size.width / 2.f + 95.f, size.height / 2.f + yOff});
        menu->addChild(btn);
        *out = spr;
    };

    addRow(42.f, 4, "Solo lectura (viewers)", &m_viewOnlySpr);
    addRow(14.f, 1, "Cambiar la musica", &m_songSpr);
    addRow(-14.f, 2, "Abrir Options", &m_optionsSpr);
    addRow(-42.f, 3, "Editar ajustes del nivel", &m_settingsSpr);

    auto* note = makeHint("Solo lectura: miran el nivel y editan en local,\nsin compartir. Playtest resetea sus cambios.");
    m_mainLayer->addChildAtPosition(note, Anchor::Bottom, {0.f, 28.f});

    refresh();
    return true;
}

void HostOptionsPopup::onToggle(CCObject* sender) {
    switch (sender ? sender->getTag() : 0) {
        case 1: m_permissions.allowSong = !m_permissions.allowSong; break;
        case 2: m_permissions.allowOptions = !m_permissions.allowOptions; break;
        case 3: m_permissions.allowLevelSettings = !m_permissions.allowLevelSettings; break;
        case 4: m_permissions.viewOnly = !m_permissions.viewOnly; break;
        default: break;
    }
    CollabManager::get().setHostPermissions(m_permissions);
    refresh();
}

void HostOptionsPopup::refresh() {
    auto apply = [](ButtonSprite* spr, bool on) {
        if (!spr) return;
        spr->setString(on ? "SI" : "NO");
        spr->updateBGImage(on ? "GJ_button_01.png" : "GJ_button_06.png");
    };
    apply(m_songSpr, m_permissions.allowSong);
    apply(m_optionsSpr, m_permissions.allowOptions);
    apply(m_settingsSpr, m_permissions.allowLevelSettings);
    apply(m_viewOnlySpr, m_permissions.viewOnly);
}

// ---------------------------------------------------------------------------
// Peers popup (list + host kick)
// ---------------------------------------------------------------------------

CollabPeersPopup* CollabPeersPopup::create() {
    auto* ret = new CollabPeersPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabPeersPopup::init() {
    if (!Popup::init(360.f, 276.f)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Editores en la sala");

    constexpr float kScrollW = 320.f, kScrollH = 172.f;
    constexpr float kScrollX = 20.f, kScrollY = 44.f;

    m_mainLayer->addChildAtPosition(makeCard(kScrollW + 8.f, kScrollH + 8.f),
        Anchor::Center, {0.f, -8.f});

    m_scroll = ScrollLayer::create({kScrollW, kScrollH});
    m_scroll->setPosition({kScrollX, kScrollY});
    m_mainLayer->addChild(m_scroll);

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setScale(0.5f);
    m_info->setColor({160, 160, 175});
    m_info->setPosition({kScrollX + kScrollW / 2.f, kScrollY + kScrollH / 2.f});
    m_mainLayer->addChild(m_info, 2);

    auto* header = makeHint("Toca un nombre para ver su perfil");
    header->setScale(0.48f);
    m_mainLayer->addChildAtPosition(header, Anchor::Top, {0.f, -38.f});

    auto* hint = makeHint(CollabManager::get().isHost()
        ? "Toca Expulsar para echar a un editor"
        : (CollabManager::get().isViewOnly()
            ? "Estas en solo lectura — tus edits no se comparten"
            : "Lista de quien esta conectado"));
    m_mainLayer->addChildAtPosition(hint, Anchor::Bottom, {0.f, 16.f});

    rebuildList();
    schedule(schedule_selector(CollabPeersPopup::refresh), 0.5f);
    return true;
}

void CollabPeersPopup::refresh(float) {
    auto& mgr = CollabManager::get();
    if (!mgr.connected()) {
        onClose(nullptr);
        return;
    }
    if (peerSignature(mgr.peers()) != m_lastPeerSignature) rebuildList();
}

void CollabPeersPopup::rebuildList() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto& mgr = CollabManager::get();
    auto peers = mgr.peers();

    // Stable order: host first, then by clientId.
    std::sort(peers.begin(), peers.end(), [](PeerInfo const& a, PeerInfo const& b) {
        if (a.isHost != b.isHost) return a.isHost > b.isHost;
        return a.clientId < b.clientId;
    });
    m_lastPeerSignature = peerSignature(peers);

    if (peers.empty()) {
        if (m_info) {
            m_info->setString("Nadie en la sala");
            m_info->setVisible(true);
        }
        m_scroll->m_contentLayer->setContentSize(m_scroll->getContentSize());
        return;
    }
    if (m_info) m_info->setVisible(false);

    constexpr float kWidth = 320.f;
    constexpr float kRowH = 52.f;
    constexpr float kPad = 5.f;
    float total = static_cast<float>(peers.size()) * (kRowH + kPad) + kPad;
    float viewH = m_scroll->getContentSize().height;
    float contentH = std::max(total, viewH);
    m_scroll->m_contentLayer->setContentSize({kWidth, contentH});

    float y = contentH - kPad - kRowH;
    for (auto const& peer : peers) {
        bool isSelf = peer.clientId == mgr.clientId();

        // Our own row always draws the icon we have equipped right now; the
        // server copy can lag behind an icon change made mid-session.
        PeerInfo shown = peer;
        if (isSelf) shown.appearance = CollabManager::localAppearance();

        auto color = peerColor(peer.clientId);

        auto* row = CCNode::create();
        row->setContentSize({kWidth, kRowH});
        row->setAnchorPoint({0.f, 0.f});
        row->setPosition({0.f, y});

        auto* bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({kWidth - 8.f, kRowH - 4.f});
        bg->setColor({18, 20, 29});
        bg->setOpacity(210);
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({4.f, 2.f});
        row->addChild(bg);

        auto* accent = CCLayerColor::create({color.r, color.g, color.b, 255}, 3.f, kRowH - 12.f);
        accent->setPosition({6.f, 6.f});
        row->addChild(accent, 1);

        bool canKick = mgr.isHost() && !peer.isHost && !isSelf;
        float identityW = canKick ? kWidth - 92.f : kWidth - 8.f;

        // The identity block is drawn statically on the row; the tap zone is a
        // separate invisible node so the press animation can never shift or
        // scale the visible layout.
        auto* identity = CCNode::create();
        identity->setContentSize({identityW, kRowH});
        identity->setAnchorPoint({0.f, 0.f});
        identity->setPosition({4.f, 0.f});

        if (auto* avatar = makePeerAvatar(shown, color, 40.f)) {
            avatar->setPosition({29.f, kRowH / 2.f});
            identity->addChild(avatar);
        }

        std::string label = peer.username.empty() ? fmt::format("Editor #{}", peer.clientId) : peer.username;
        auto* name = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
        name->setScale(0.46f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setColor({245, 245, 250});
        name->setPosition({56.f, kRowH / 2.f + 8.f});
        float nameMaxW = identityW - 66.f - (peer.isHost ? 44.f : 0.f);
        name->limitLabelWidth(nameMaxW, 0.46f, 0.2f);
        identity->addChild(name);

        if (peer.isHost) {
            auto* badge = CCLabelBMFont::create("HOST", "goldFont.fnt");
            badge->setScale(0.4f);
            badge->setAnchorPoint({0.f, 0.5f});
            badge->setPosition({
                56.f + name->getScaledContentSize().width + 6.f,
                kRowH / 2.f + 8.f
            });
            identity->addChild(badge);
        }

        std::string role;
        if (isSelf) role = "Tu cuenta — toca para ver tu perfil";
        else if (peer.appearance.accountID > 0) role = "Toca para ver su perfil";
        else role = "Perfil no disponible";
        auto* roleLabel = CCLabelBMFont::create(role.c_str(), "chatFont.fnt");
        roleLabel->setScale(0.42f);
        roleLabel->setAnchorPoint({0.f, 0.5f});
        roleLabel->setColor({160, 160, 175});
        roleLabel->setPosition({56.f, kRowH / 2.f - 8.f});
        roleLabel->limitLabelWidth(identityW - 66.f, 0.42f, 0.2f);
        identity->addChild(roleLabel);

        // Small chevron on the far side of the identity zone as a "tappable"
        // affordance, only when there is a profile to open.
        if (peer.appearance.accountID > 0 || isSelf) {
            if (auto* arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
                arrow->setFlipX(true);
                arrow->setScale(0.45f);
                arrow->setOpacity(120);
                arrow->setPosition({identityW - 16.f, kRowH / 2.f});
                identity->addChild(arrow);
            }
        }

        row->addChild(identity, 2);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(row->getContentSize());

        auto* hitZone = CCNode::create();
        hitZone->setContentSize({identityW, kRowH});
        auto* profile = CCMenuItemSpriteExtra::create(hitZone, this,
            menu_selector(CollabPeersPopup::onProfile));
        profile->m_animationEnabled = false;
        profile->setTag(peer.clientId);
        profile->setPosition({4.f + identityW / 2.f, kRowH / 2.f});
        menu->addChild(profile);

        if (canKick) {
            auto* btn = makeButton(this, menu_selector(CollabPeersPopup::onKick), "Expulsar", "GJ_button_06.png", 0.43f);
            btn->setTag(peer.clientId);
            btn->setPosition({kWidth - 48.f, kRowH / 2.f});
            menu->addChild(btn);
        }
        row->addChild(menu, 3);

        m_scroll->m_contentLayer->addChild(row);
        y -= kRowH + kPad;
    }
    m_scroll->moveToTop();
}

void CollabPeersPopup::onProfile(CCObject* sender) {
    int clientId = sender ? sender->getTag() : 0;
    if (clientId <= 0) return;

    auto peers = CollabManager::get().peers();
    auto it = std::find_if(peers.begin(), peers.end(), [clientId](PeerInfo const& peer) {
        return peer.clientId == clientId;
    });
    if (it == peers.end()) return;

    int accountID = it->appearance.accountID;
    // Our own row can open the profile even if the server round-trip hasn't
    // echoed our accountID back yet.
    if (accountID <= 0 && clientId == CollabManager::get().clientId()) {
        accountID = AccountVerifier::get().getAccountID();
    }
    if (accountID <= 0) {
        Notification::create("Este editor no compartio una cuenta de GD", NotificationIcon::Warning)->show();
        return;
    }

    bool ownProfile = accountID == AccountVerifier::get().getAccountID();
    if (auto* page = ProfilePage::create(accountID, ownProfile)) {
        page->show();
    }
}

void CollabPeersPopup::onKick(CCObject* sender) {
    int id = sender ? sender->getTag() : 0;
    if (id <= 0) return;
    auto& mgr = CollabManager::get();
    if (!mgr.isHost()) return;
    std::string name = mgr.peerName(id);
    mgr.kickPeer(id);
    Notification::create(
        fmt::format("Expulsando a {}", name.empty() ? fmt::format("#{}", id) : name),
        NotificationIcon::Info
    )->show();
    // List refreshes on next peer broadcast; rebuild optimistically.
    rebuildList();
}

// ---------------------------------------------------------------------------
// Invite popup (host picks friends to invite)
// ---------------------------------------------------------------------------

CollabInvitePopup* CollabInvitePopup::create() {
    auto* ret = new CollabInvitePopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

CollabInvitePopup::~CollabInvitePopup() {
    // Drop ourselves as the delegate so a late GD response never calls a freed
    // popup (GameLevelManager holds a raw pointer).
    if (auto* glm = GameLevelManager::get()) {
        if (glm->m_userListDelegate == this) glm->m_userListDelegate = nullptr;
    }
}

bool CollabInvitePopup::init() {
    constexpr float kPopupW = 380.f, kPopupH = 280.f;
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Invitar Amigos");

    constexpr float kScrollW = 330.f, kScrollH = 152.f;
    constexpr float kScrollX = (kPopupW - kScrollW) / 2.f;
    constexpr float kScrollY = 36.f;

    // Search bar: clickable magnifier + live-filter text input ------------
    const float searchY = kPopupH - 52.f;

    auto* searchMenu = CCMenu::create();
    searchMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(searchMenu, 5);

    auto* lupa = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png");
    auto* lupaBtn = CCMenuItemSpriteExtra::create(lupa, this, menu_selector(CollabInvitePopup::onFocusSearch));
    lupaBtn->m_baseScale = 0.62f;
    lupaBtn->setScale(0.62f);
    lupaBtn->setPosition({kScrollX + 14.f, searchY});
    searchMenu->addChild(lupaBtn);

    m_search = TextInput::create(kScrollW - 42.f, "Buscar amigo...", "chatFont.fnt");
    m_search->setMaxCharCount(20);
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setPosition({kScrollX + 32.f + (kScrollW - 42.f) / 2.f, searchY});
    m_search->setCallback([this](std::string const&) { rebuildRows(); });
    m_mainLayer->addChild(m_search, 5);

    // Friend list ----------------------------------------------------------
    auto* bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({kScrollW + 10.f, kScrollH + 10.f});
    bg->setColor({0, 0, 0});
    bg->setOpacity(105);
    bg->setPosition({kScrollX + kScrollW / 2.f, kScrollY + kScrollH / 2.f});
    m_mainLayer->addChild(bg);

    m_scroll = ScrollLayer::create({kScrollW, kScrollH});
    m_scroll->setPosition({kScrollX, kScrollY});
    m_mainLayer->addChild(m_scroll);

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setScale(0.6f);
    m_info->setPosition({kScrollX + kScrollW / 2.f, kScrollY + kScrollH / 2.f});
    m_mainLayer->addChild(m_info, 10);

    m_count = CCLabelBMFont::create("", "goldFont.fnt");
    m_count->setScale(0.42f);
    m_count->setPosition({kPopupW / 2.f, 20.f});
    m_mainLayer->addChild(m_count, 10);

    loadFriends();
    return true;
}

void CollabInvitePopup::onFocusSearch(CCObject*) {
    if (m_search) m_search->focus();
}

void CollabInvitePopup::setInfo(std::string const& text) {
    if (!m_info) return;
    m_info->setString(text.c_str());
    m_info->setVisible(!text.empty());
}

void CollabInvitePopup::loadFriends() {
    auto* glm = GameLevelManager::get();
    if (!glm) {
        setInfo("No disponible");
        return;
    }
    setInfo("Cargando amigos...");
    glm->m_userListDelegate = this;
    glm->getUserList(UserListType::Friends);
}

void CollabInvitePopup::getUserListFinished(CCArray* scores, UserListType) {
    buildList(scores);
}

void CollabInvitePopup::getUserListFailed(UserListType, GJErrorCode) {
    setInfo("No se pudieron cargar tus amigos.");
}

void CollabInvitePopup::buildList(CCArray* scores) {
    m_names.clear();
    m_friends.clear();

    if (scores) {
        for (auto* s : CCArrayExt<GJUserScore*>(scores)) {
            if (!s || s->m_accountID <= 0) continue;

            FriendEntry e;
            e.accountID = s->m_accountID;
            e.name = s->m_userName;
            e.nameLower = e.name;
            std::transform(e.nameLower.begin(), e.nameLower.end(), e.nameLower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            e.iconType = static_cast<int>(s->m_iconType);
            switch (s->m_iconType) {
                case IconType::Ship: e.iconID = s->m_playerShip; break;
                case IconType::Ball: e.iconID = s->m_playerBall; break;
                case IconType::Ufo: e.iconID = s->m_playerUfo; break;
                case IconType::Wave: e.iconID = s->m_playerWave; break;
                case IconType::Robot: e.iconID = s->m_playerRobot; break;
                case IconType::Spider: e.iconID = s->m_playerSpider; break;
                case IconType::Swing: e.iconID = s->m_playerSwing; break;
                case IconType::Jetpack: e.iconID = s->m_playerJetpack; break;
                default: e.iconID = s->m_playerCube; break;
            }
            if (e.iconID <= 0) e.iconID = 1;
            e.color1 = s->m_color1;
            e.color2 = s->m_color2;
            e.color3 = s->m_color3;
            e.glow = s->m_glowEnabled;

            m_names[e.accountID] = e.name;
            m_friends.push_back(std::move(e));
        }
    }

    std::sort(m_friends.begin(), m_friends.end(),
        [](FriendEntry const& a, FriendEntry const& b) { return a.nameLower < b.nameLower; });

    rebuildRows();
}

void CollabInvitePopup::rebuildRows() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    constexpr float kRowH = 36.f;
    constexpr float kWidth = 330.f;
    constexpr float kScrollH = 152.f;

    std::string filter = m_search ? m_search->getString() : "";
    std::transform(filter.begin(), filter.end(), filter.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<FriendEntry const*> shown;
    shown.reserve(m_friends.size());
    for (auto const& e : m_friends) {
        if (filter.empty() || e.nameLower.find(filter) != std::string::npos) {
            shown.push_back(&e);
        }
    }

    if (m_count) {
        if (m_friends.empty()) m_count->setString("");
        else if (filter.empty()) m_count->setString(fmt::format("{} amigos", m_friends.size()).c_str());
        else m_count->setString(fmt::format("{} de {} amigos", shown.size(), m_friends.size()).c_str());
    }

    if (shown.empty()) {
        setInfo(m_friends.empty()
            ? "No tienes amigos en tu lista de GD."
            : "Sin resultados para tu busqueda.");
        m_scroll->m_contentLayer->setContentSize({kWidth, kScrollH});
        m_scroll->moveToTop();
        return;
    }
    setInfo("");

    auto* gm = GameManager::get();

    float total = kRowH * shown.size();
    if (total < kScrollH) total = kScrollH;
    m_scroll->m_contentLayer->setContentSize({kWidth, total});

    int i = 0;
    for (auto const* e : shown) {
        float y = total - kRowH * (i + 1);

        auto* row = CCNode::create();
        row->setContentSize({kWidth, kRowH});
        row->setPosition({0.f, y});

        auto* stripe = CCLayerColor::create(
            {255, 255, 255, static_cast<GLubyte>(i % 2 == 0 ? 26 : 10)}, kWidth, kRowH - 2.f);
        stripe->setPositionY(1.f);
        row->addChild(stripe);

        // Avatar with the friend's real icon and colors.
        if (auto* player = SimplePlayer::create(e->iconID)) {
            IconType type = peerIconType(e->iconType);
            if (type != IconType::Cube) player->updatePlayerFrame(e->iconID, type);
            if (gm) {
                player->setColor(gm->colorForIdx(std::clamp(e->color1, 0, 1000)));
                player->setSecondColor(gm->colorForIdx(std::clamp(e->color2, 0, 1000)));
                if (e->glow) player->setGlowOutline(gm->colorForIdx(std::clamp(e->color3, 0, 1000)));
                else player->disableGlowOutline();
            }
            float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
            if (maxDim > 0.f) player->setScale((kRowH - 12.f) / maxDim);
            player->setPosition({22.f, kRowH / 2.f});
            row->addChild(player, 1);
        }

        auto* label = CCLabelBMFont::create(e->name.c_str(), "chatFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.75f);
        label->limitLabelWidth(190.f, 0.75f, 0.2f);
        label->setPosition({44.f, kRowH / 2.f});
        row->addChild(label);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        auto* btn = makeButton(this, menu_selector(CollabInvitePopup::onInvite), "Invitar", "GJ_button_02.png", 0.4f);
        btn->setTag(e->accountID);
        btn->setPosition({kWidth - 42.f, kRowH / 2.f});
        menu->addChild(btn);
        row->addChild(menu);

        m_scroll->m_contentLayer->addChild(row);
        ++i;
    }
    m_scroll->moveToTop();
}

void CollabInvitePopup::onInvite(CCObject* sender) {
    int acc = sender ? sender->getTag() : 0;
    if (acc <= 0) return;
    std::string name = m_names.count(acc) ? m_names[acc] : "";

    CollabManager::get().inviteUser(acc, name, [name](bool ok, bool online, std::string const& message) {
        auto icon = (ok && online) ? NotificationIcon::Success : NotificationIcon::Warning;
        std::string text = message;
        if (ok && online) text = fmt::format("Invitacion enviada a {}", name.empty() ? "el usuario" : name);
        else if (ok && !online) text = fmt::format("{} no esta en linea", name.empty() ? "El usuario" : name);
        Notification::create(text, icon)->show();
    });
}

// ---------------------------------------------------------------------------
// Incoming invite prompt
// ---------------------------------------------------------------------------

CollabInvitePromptPopup* CollabInvitePromptPopup::create(std::string const& room, std::string const& fromName) {
    auto* ret = new CollabInvitePromptPopup();
    if (ret && ret->init(room, fromName)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabInvitePromptPopup::init(std::string const& room, std::string const& fromName) {
    if (!Popup::init(340.f, 190.f)) return false;
    paimon::markDynamicPopup(this);
    m_room = room;
    setTitle("Invitacion a colaborar");

    std::string who = fromName.empty() ? "Alguien" : fromName;
    auto* info = CCLabelBMFont::create(
        fmt::format("{} te invito a su sala colaborativa.", who).c_str(), "chatFont.fnt");
    info->setScale(0.65f);
    info->limitLabelWidth(300.f, 0.65f, 0.2f);
    m_mainLayer->addChildAtPosition(info, Anchor::Center, {0.f, 22.f});

    auto* codeLabel = CCLabelBMFont::create(fmt::format("Sala: {}", room).c_str(), "goldFont.fnt");
    codeLabel->setScale(0.6f);
    m_mainLayer->addChildAtPosition(codeLabel, Anchor::Center, {0.f, -6.f});

    auto* menu = CCMenu::create();
    menu->addChild(makeButton(this, menu_selector(CollabInvitePromptPopup::onReject), "Rechazar", "GJ_button_06.png", 0.7f));
    menu->addChild(makeButton(this, menu_selector(CollabInvitePromptPopup::onAccept), "Aceptar", "GJ_button_01.png", 0.7f));
    menu->setLayout(RowLayout::create()->setGap(14.f));
    menu->updateLayout();
    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, {0.f, 34.f});
    return true;
}

void CollabInvitePromptPopup::onAccept(CCObject*) {
    std::string room = m_room;
    onClose(nullptr);
    CollabManager::get().connect(room, defaultDisplayName(), ConnectMode::Join);
}

void CollabInvitePromptPopup::onReject(CCObject*) {
    onClose(nullptr);
}

// ---------------------------------------------------------------------------
// Chat popup
// ---------------------------------------------------------------------------

CollabChatPopup* CollabChatPopup::create() {
    auto* ret = new CollabChatPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabChatPopup::init() {
    if (!Popup::init(400.f, 280.f)) return false;
    paimon::markDynamicPopup(this);
    setID("collab-chat"_spr);
    setTitle("Chat de sala");

    // --- Header: connection dot + room code + editor count ------------------
    m_statusDot = CCDrawNode::create();
    m_mainLayer->addChild(m_statusDot, 5); // positioned next to the label on refresh
    m_headerLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_headerLabel->setScale(0.45f);
    m_mainLayer->addChildAtPosition(m_headerLabel, Anchor::Top, {0.f, -38.f});

    // --- Message history: dark card + scrollable list -----------------------
    constexpr float kChatW = 360.f, kChatH = 148.f;
    m_mainLayer->addChildAtPosition(makeCard(kChatW + 8.f, kChatH + 8.f), Anchor::Center, {0.f, 16.f});

    m_scroll = ScrollLayer::create({kChatW, kChatH});
    m_scroll->setPosition({(m_mainLayer->getContentWidth() - kChatW) / 2.f, 82.f});
    m_mainLayer->addChild(m_scroll, 1);

    m_emptyLabel = makeHint("Aun no hay mensajes. Saluda!");
    m_emptyLabel->setVisible(false);
    m_mainLayer->addChildAtPosition(m_emptyLabel, Anchor::Center, {0.f, 16.f});
    m_emptyLabel->setZOrder(5);

    // --- Who's talking + local mic level bar ---------------------------------
    m_speakingLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_speakingLabel->setScale(0.4f);
    m_speakingLabel->setColor({140, 255, 140});
    m_mainLayer->addChildAtPosition(m_speakingLabel, Anchor::Bottom, {-30.f, 62.f});

    // Thin bar above the mic button showing your own level while you talk.
    m_micBarTrack = CCLayerColor::create({0, 0, 0, 130}, 60.f, 4.f);
    m_micBarTrack->ignoreAnchorPointForPosition(false);
    m_micBarTrack->setAnchorPoint({0.f, 0.5f});
    m_micBarTrack->setPosition({322.f, 62.f});
    m_micBarTrack->setVisible(false);
    m_mainLayer->addChild(m_micBarTrack, 1);

    m_micBarFill = CCLayerColor::create({140, 255, 140, 255}, 60.f, 4.f);
    m_micBarFill->ignoreAnchorPointForPosition(false);
    m_micBarFill->setAnchorPoint({0.f, 0.5f});
    m_micBarFill->setPosition({322.f, 62.f});
    m_micBarFill->setScaleX(0.f);
    m_micBarFill->setVisible(false);
    m_mainLayer->addChild(m_micBarFill, 2);

    // --- Input row ------------------------------------------------------------
    m_input = TextInput::create(210.f, "Mensaje", "chatFont.fnt");
    m_input->setMaxCharCount(200);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Bottom, {-65.f, 32.f});

    auto* menu = CCMenu::create();

    // Emote picker button (reuses the mod's emote system, wired to the input).
    paimon::emotes::EmoteInputContext ctx;
    geode::Ref<cocos2d::CCNode> inputRef = m_input;
    ctx.getText = [inputRef]() -> std::string {
        auto* ti = static_cast<geode::TextInput*>(inputRef.data());
        return ti ? std::string(ti->getString()) : std::string();
    };
    ctx.setText = [inputRef](std::string const& t) {
        if (auto* ti = static_cast<geode::TextInput*>(inputRef.data())) ti->setString(t);
    };
    ctx.charLimit = 200;
    if (auto* emoteBtn = paimon::emotes::EmoteButton::create(ctx)) {
        emoteBtn->setScale(0.9f);
        menu->addChild(emoteBtn);
    }

    auto* sendBtn = makeButton(this, menu_selector(CollabChatPopup::onSend), "Enviar", "GJ_button_01.png", 0.5f);
    menu->addChild(sendBtn);

    m_micSprite = ButtonSprite::create("Mic", "bigFont.fnt", "GJ_button_06.png", 0.5f);
    m_micSprite->setScale(0.5f);
    menu->addChild(CCMenuItemSpriteExtra::create(m_micSprite, this, menu_selector(CollabChatPopup::onMic)));

    menu->setLayout(RowLayout::create()->setGap(8.f));
    menu->updateLayout();
    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, {122.f, 32.f});

    refresh();
    this->schedule(schedule_selector(CollabChatPopup::refresh), 0.15f);
    // Per-frame so the mic bar rises and falls smoothly.
    this->schedule(schedule_selector(CollabChatPopup::tickVoice));
    return true;
}

void CollabChatPopup::onSend(CCObject*) {
    if (!m_input) return;
    std::string text = trim(std::string(m_input->getString()));
    if (text.empty()) return;
    if (!CollabManager::get().connected()) {
        showAlert("No estas conectado a ninguna sala.");
        return;
    }
    CollabManager::get().sendChat(text);
    m_input->setString("");
}

void CollabChatPopup::onMic(CCObject*) {
    if (!Mod::get()->getSettingValue<bool>("collab-voice")) {
        Notification::create("El chat de voz esta desactivado en los ajustes", NotificationIcon::Warning)->show();
        return;
    }
    auto& voice = CollabVoice::get();
    voice.setMicEnabled(!voice.micEnabled());
}

void CollabChatPopup::refresh(float) {
    auto& mgr = CollabManager::get();
    auto& voice = CollabVoice::get();

    // Header: green dot + room info when connected, red dot otherwise.
    bool connected = mgr.connected();
    int connState = connected ? 1 : 0;
    if (m_statusDot && connState != m_connShown) {
        m_connShown = connState;
        m_statusDot->clear();
        m_statusDot->drawDot({0.f, 0.f}, 4.f,
            connected ? ccColor4F{0.35f, 0.95f, 0.45f, 1.f} : ccColor4F{0.95f, 0.4f, 0.4f, 1.f});
    }
    if (m_headerLabel) {
        std::string header;
        if (connected) {
            int n = mgr.peerCount();
            header = fmt::format("Sala {}  -  {} {}", mgr.roomCode(), n, n == 1 ? "editor" : "editores");
        } else {
            header = "Sin conexion a ninguna sala";
        }
        m_headerLabel->setString(header.c_str());
        m_headerLabel->limitLabelWidth(300.f, 0.45f, 0.2f);
        if (m_statusDot) {
            float half = m_headerLabel->getContentSize().width * m_headerLabel->getScale() / 2.f;
            auto center = m_headerLabel->getPosition();
            m_statusDot->setPosition({center.x - half - 9.f, center.y});
        }
    }

    // Who's talking right now (you + peers).
    if (m_speakingLabel) {
        std::string speaking;
        if (voice.transmitting()) speaking = "Tu";
        for (auto const& s : voice.speakingNow()) {
            if (!speaking.empty()) speaking += ", ";
            speaking += s.name;
        }
        m_speakingLabel->setString(speaking.empty() ? "" : fmt::format("Hablando: {}", speaking).c_str());
        m_speakingLabel->limitLabelWidth(240.f, 0.4f, 0.15f);
    }

    if (m_micSprite) {
        bool on = voice.micEnabled();
        m_micSprite->updateBGImage(on ? "GJ_button_01.png" : "GJ_button_06.png");
    }

    if (mgr.chatRevision() == m_lastRevision) return;
    m_lastRevision = mgr.chatRevision();
    rebuildMessages();
}

void CollabChatPopup::rebuildMessages() {
    if (!m_scroll) return;
    auto history = CollabManager::get().recentChat(60);
    if (m_emptyLabel) m_emptyLabel->setVisible(history.empty());

    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    constexpr float kPadX = 8.f, kPadY = 6.f, kGapY = 3.f;
    float const width = m_scroll->getContentSize().width;
    float const viewH = m_scroll->getContentSize().height;

    struct Row {
        CCNode* node;
        float height;
    };
    std::vector<Row> rows;
    rows.reserve(history.size());
    float total = kPadY * 2.f;
    for (auto const& msg : history) {
        auto* node = buildChatLine(msg, 0.5f);
        if (!node) continue;
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
            label->limitLabelWidth(width - kPadX * 2.f, 0.5f, 0.2f);
        }
        float h = std::max(node->getContentSize().height * node->getScaleY(), 11.f);
        rows.push_back({node, h});
        total += h + kGapY;
    }
    if (!rows.empty()) total -= kGapY;

    float contentH = std::max(total, viewH);
    content->setContentSize({width, contentH});

    // Oldest at the top, newest at the bottom (chat order).
    float y = contentH - kPadY;
    for (auto const& row : rows) {
        y -= row.height;
        row.node->setPosition({kPadX, y});
        content->addChild(row.node);
        y -= kGapY;
    }

    // Pin the view to the newest message.
    content->setPosition({0.f, 0.f});
}

void CollabChatPopup::tickVoice(float dt) {
    auto& voice = CollabVoice::get();
    bool micOn = voice.micEnabled();
    if (m_micBarTrack) m_micBarTrack->setVisible(micOn);
    if (m_micBarFill) m_micBarFill->setVisible(micOn);
    if (!micOn) {
        m_micShown = 0.f;
        return;
    }
    // Fast attack, slower release, same feel as the overlay bars.
    float target = voice.localLevel();
    float k = target > m_micShown ? 16.f : 6.f;
    m_micShown += (target - m_micShown) * std::min(1.f, k * dt);
    if (m_micBarFill) m_micBarFill->setScaleX(std::clamp(m_micShown, 0.f, 1.f));
}

} // namespace paimon::collab
