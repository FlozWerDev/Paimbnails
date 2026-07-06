#include "CollabPopups.hpp"

#include "../../utils/DynamicPopupRegistry.hpp"
#include "CollabEmotes.hpp"
#include "CollabManager.hpp"
#include "CollabVoice.hpp"
#include "../emotes/ui/EmoteButton.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <cctype>
#include <random>

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
    FLAlertLayer::create("Collab Editor", message, "OK")->show();
}

std::string randomRoomCode() {
    static constexpr char const* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 31);
    std::string out = "PAIM-";
    for (int i = 0; i < 6; ++i) {
        if (i == 3) out.push_back('-');
        out.push_back(kAlphabet[dist(gen)]);
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

} // namespace

bool isUnlocked() {
    return Mod::get()->getSavedValue<bool>("collab-unlocked", false);
}

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
// Gate popup
// ---------------------------------------------------------------------------

CollabGatePopup* CollabGatePopup::create() {
    auto* ret = new CollabGatePopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CollabGatePopup::init() {
    if (!Popup::init(320.f, 190.f)) return false;
    paimon::markDynamicPopup(this);
    setID("collab-gate"_spr);
    setTitle("Collab Editor");

    auto* card = makeCard(270.f, 86.f);
    m_mainLayer->addChildAtPosition(card, Anchor::Center, {0.f, 8.f});

    auto* info = makeCaption("Codigo de acceso");
    m_mainLayer->addChildAtPosition(info, Anchor::Center, {0.f, 34.f});

    m_codeInput = TextInput::create(200.f, "Codigo", "chatFont.fnt");
    m_codeInput->setMaxCharCount(32);
    m_mainLayer->addChildAtPosition(m_codeInput, Anchor::Center, {0.f, 8.f});

    auto* hint = makeHint("Pide el codigo de acceso para desbloquear el collab");
    m_mainLayer->addChildAtPosition(hint, Anchor::Center, {0.f, -18.f});

    auto* menu = CCMenu::create();
    menu->addChild(makeButton(this, menu_selector(CollabGatePopup::onConfirm), "Desbloquear"));
    menu->setLayout(RowLayout::create());
    menu->updateLayout();
    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, {0.f, 30.f});

    return true;
}

void CollabGatePopup::onConfirm(CCObject*) {
    std::string code = trim(m_codeInput ? std::string(m_codeInput->getString()) : "");
    if (code != kAccessCode) {
        showAlert("Codigo incorrecto.");
        return;
    }
    Mod::get()->setSavedValue<bool>("collab-unlocked", true);
    onClose(nullptr);
    if (auto* popup = CollabRoomPopup::create()) popup->show();
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
    if (!Popup::init(340.f, 240.f)) return false;
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
    m_name = defaultDisplayName();

    rebuild();
    this->schedule(schedule_selector(CollabRoomPopup::refresh), 0.25f);
    return true;
}

void CollabRoomPopup::captureInputs() {
    if (m_nameInput) m_name = trim(std::string(m_nameInput->getString()));
    if (m_codeInput) m_joinCode = trim(std::string(m_codeInput->getString()));
}

void CollabRoomPopup::rebuild() {
    captureInputs();
    m_content->removeAllChildrenWithCleanup(true);
    m_codeInput = nullptr;
    m_nameInput = nullptr;
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
    m_content->addChildAtPosition(makeCard(304.f, 100.f), Anchor::Center, {0.f, 8.f});

    if (!m_joinTab) {
        m_content->addChildAtPosition(makeCaption("Codigo de tu sala"), Anchor::Center, {0.f, 44.f});

        m_codeLabel = CCLabelBMFont::create(m_createCode.c_str(), "bigFont.fnt");
        m_codeLabel->setScale(0.6f);
        m_codeLabel->setColor({255, 210, 90});
        m_content->addChildAtPosition(m_codeLabel, Anchor::Center, {0.f, 22.f});

        auto* codeMenu = CCMenu::create();
        codeMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onGenerate), "Otro", "GJ_button_05.png", 0.42f));
        codeMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onCopy), "Copiar", "GJ_button_02.png", 0.42f));
        codeMenu->setLayout(RowLayout::create()->setGap(6.f));
        codeMenu->updateLayout();
        m_content->addChildAtPosition(codeMenu, Anchor::Center, {0.f, -6.f});

        auto* hint = makeHint("Comparte este codigo con tus amigos para que se unan");
        hint->limitLabelWidth(290.f, 0.45f, 0.2f);
        m_content->addChildAtPosition(hint, Anchor::Center, {0.f, -30.f});
    } else {
        m_content->addChildAtPosition(makeCaption("Codigo del host"), Anchor::Center, {0.f, 44.f});

        m_codeInput = TextInput::create(185.f, "PAIM-XX-XXX", "bigFont.fnt");
        m_codeInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ");
        m_codeInput->setMaxCharCount(24);
        m_codeInput->setString(m_joinCode);
        m_codeInput->setScale(0.8f);
        m_content->addChildAtPosition(m_codeInput, Anchor::Center, {-35.f, 16.f});

        auto* pasteMenu = CCMenu::create();
        pasteMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onPaste), "Pegar", "GJ_button_05.png", 0.42f));
        pasteMenu->setLayout(RowLayout::create());
        pasteMenu->updateLayout();
        m_content->addChildAtPosition(pasteMenu, Anchor::Center, {88.f, 16.f});

        auto* hint = makeHint("Pega el codigo que te compartio el host de la sala");
        hint->limitLabelWidth(290.f, 0.45f, 0.2f);
        m_content->addChildAtPosition(hint, Anchor::Center, {0.f, -30.f});
    }

    // --- Name + the one big action button -----------------------------------
    auto* nameLabel = CCLabelBMFont::create("Tu nombre", "chatFont.fnt");
    nameLabel->setScale(0.5f);
    nameLabel->setAnchorPoint({1.f, 0.5f});
    m_content->addChildAtPosition(nameLabel, Anchor::Bottom, {-40.f, 66.f});

    m_nameInput = TextInput::create(150.f, "Nombre", "chatFont.fnt");
    m_nameInput->setMaxCharCount(24);
    m_nameInput->setString(m_name);
    m_nameInput->setScale(0.75f);
    m_content->addChildAtPosition(m_nameInput, Anchor::Bottom, {30.f, 66.f});

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

    m_content->addChildAtPosition(makeCard(304.f, 96.f), Anchor::Center, {0.f, 6.f});

    m_content->addChildAtPosition(makeCaption("Codigo de la sala"), Anchor::Center, {-60.f, 42.f});

    m_codeLabel = CCLabelBMFont::create(mgr.roomCode().c_str(), "bigFont.fnt");
    m_codeLabel->setScale(0.5f);
    m_codeLabel->setColor({255, 210, 90});
    m_content->addChildAtPosition(m_codeLabel, Anchor::Center, {-40.f, 20.f});

    auto* copyMenu = CCMenu::create();
    copyMenu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onCopy), "Copiar", "GJ_button_02.png", 0.42f));
    copyMenu->setLayout(RowLayout::create());
    copyMenu->updateLayout();
    m_content->addChildAtPosition(copyMenu, Anchor::Center, {95.f, 24.f});

    m_peersLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_peersLabel->setScale(0.5f);
    m_content->addChildAtPosition(m_peersLabel, Anchor::Center, {0.f, -6.f});

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(0.45f);
    m_statusLabel->setColor({165, 165, 178});
    m_content->addChildAtPosition(m_statusLabel, Anchor::Center, {0.f, -26.f});

    auto* menu = CCMenu::create();
    if (mgr.isHost()) {
        menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onInvite), "Invitar", "GJ_button_01.png", 0.52f));
        menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onHostOptions), "Permisos", "GJ_button_04.png", 0.52f));
    }
    menu->addChild(makeButton(this, menu_selector(CollabRoomPopup::onLeave),
        mgr.isHost() ? "Cerrar sala" : "Salir", "GJ_button_06.png", 0.52f));
    menu->setLayout(RowLayout::create()->setGap(8.f));
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
            m_peersLabel->setString(fmt::format("Editores ({}): {}", peers.size(), names).c_str());
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
    std::string name = m_name.empty() ? defaultDisplayName() : m_name;

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
    if (!Popup::init(300.f, 210.f)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Permisos de la sala");
    m_permissions = CollabManager::get().permissions();

    auto* hint = makeHint("Elige que pueden tocar los demas editores");
    m_mainLayer->addChildAtPosition(hint, Anchor::Top, {0.f, -38.f});

    m_mainLayer->addChildAtPosition(makeCard(264.f, 100.f), Anchor::Center, {0.f, 2.f});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(menu);

    auto addRow = [&](float yOff, int tag, char const* text, ButtonSprite** out) {
        auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
        label->setScale(0.55f);
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

    addRow(30.f, 1, "Cambiar la musica", &m_songSpr);
    addRow(0.f, 2, "Abrir Options", &m_optionsSpr);
    addRow(-30.f, 3, "Editar ajustes del nivel", &m_settingsSpr);

    auto* note = makeHint("Colocar y editar objetos siempre esta permitido");
    m_mainLayer->addChildAtPosition(note, Anchor::Bottom, {0.f, 22.f});

    refresh();
    return true;
}

void HostOptionsPopup::onToggle(CCObject* sender) {
    switch (sender ? sender->getTag() : 0) {
        case 1: m_permissions.allowSong = !m_permissions.allowSong; break;
        case 2: m_permissions.allowOptions = !m_permissions.allowOptions; break;
        case 3: m_permissions.allowLevelSettings = !m_permissions.allowLevelSettings; break;
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
    if (!Popup::init(340.f, 260.f)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Invitar amigos");

    constexpr float kScrollW = 300.f, kScrollH = 175.f;
    constexpr float kScrollX = 20.f, kScrollY = 30.f;

    auto* bg = CCLayerColor::create({0, 0, 0, 90}, kScrollW, kScrollH);
    bg->setPosition({kScrollX, kScrollY});
    m_mainLayer->addChild(bg);

    m_scroll = ScrollLayer::create({kScrollW, kScrollH});
    m_scroll->setPosition({kScrollX, kScrollY});
    m_mainLayer->addChild(m_scroll);

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setScale(0.6f);
    m_info->setPosition({kScrollX + kScrollW / 2.f, kScrollY + kScrollH / 2.f});
    m_mainLayer->addChild(m_info, 10);

    loadFriends();
    return true;
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
    if (!m_scroll) return;
    m_names.clear();
    m_scroll->m_contentLayer->removeAllChildren();

    std::vector<GJUserScore*> friends;
    if (scores) {
        for (auto* s : CCArrayExt<GJUserScore*>(scores)) {
            if (s && s->m_accountID > 0) friends.push_back(s);
        }
    }

    constexpr float kRowH = 34.f;
    constexpr float kWidth = 300.f;
    constexpr float kScrollH = 175.f;

    if (friends.empty()) {
        setInfo("No tienes amigos en tu lista de GD.");
        m_scroll->m_contentLayer->setContentSize({kWidth, kScrollH});
        m_scroll->moveToTop();
        return;
    }
    setInfo("");

    float total = kRowH * friends.size();
    if (total < kScrollH) total = kScrollH;
    m_scroll->m_contentLayer->setContentSize({kWidth, total});

    int i = 0;
    for (auto* s : friends) {
        int acc = s->m_accountID;
        std::string name = s->m_userName;
        m_names[acc] = name;

        float y = total - kRowH * (i + 1);

        auto* row = CCNode::create();
        row->setContentSize({kWidth, kRowH});
        row->setPosition({0.f, y});

        if (i % 2 == 0) {
            auto* stripe = CCLayerColor::create({255, 255, 255, 20}, kWidth, kRowH);
            row->addChild(stripe);
        }

        auto* label = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.7f);
        label->limitLabelWidth(200.f, 0.7f, 0.2f);
        label->setPosition({12.f, kRowH / 2.f});
        row->addChild(label);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        auto* btn = makeButton(this, menu_selector(CollabInvitePopup::onInvite), "Invitar", "GJ_button_02.png", 0.4f);
        btn->setTag(acc);
        btn->setPosition({kWidth - 40.f, kRowH / 2.f});
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
    // Accepting an invite implicitly unlocks the feature for the joiner.
    Mod::get()->setSavedValue<bool>("collab-unlocked", true);
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
    if (!Popup::init(380.f, 260.f)) return false;
    paimon::markDynamicPopup(this);
    setID("collab-chat"_spr);
    setTitle("Chat de sala");

    m_messages = CCNode::create();
    m_messages->setAnchorPoint({0.f, 0.f});
    m_mainLayer->addChildAtPosition(m_messages, Anchor::BottomLeft, {20.f, 78.f});

    m_input = TextInput::create(210.f, "Mensaje", "chatFont.fnt");
    m_input->setMaxCharCount(200);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Bottom, {-52.f, 34.f});

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
    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, {118.f, 34.f});

    refresh();
    this->schedule(schedule_selector(CollabChatPopup::refresh), 0.3f);
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

    if (m_micSprite) {
        bool on = CollabVoice::get().micEnabled();
        m_micSprite->updateBGImage(on ? "GJ_button_01.png" : "GJ_button_06.png");
    }

    if (mgr.chatRevision() == m_lastRevision) return;
    m_lastRevision = mgr.chatRevision();

    m_messages->removeAllChildren();
    auto history = mgr.recentChat(11);
    constexpr float kLineHeight = 16.f;
    int count = static_cast<int>(history.size());
    for (int i = 0; i < count; ++i) {
        auto* line = buildChatLine(history[i], 0.55f);
        line->setPosition({0.f, (count - 1 - i) * kLineHeight});
        m_messages->addChild(line);
    }
}

} // namespace paimon::collab
