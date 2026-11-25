#include "ProfileInfoPopup.hpp"
#include "InfoBlocks.hpp"
#include "../../badges/services/RoleService.hpp"
#include "../../progression/data/ProgressionStats.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/utils/general.hpp>
#include <algorithm>
#include <cstdlib>

using namespace geode::prelude;
using namespace paimon::info::blocks;

namespace paimon::info {

namespace {

constexpr float kPopupW = 380.f;
constexpr float kPopupH = 290.f;
constexpr float kListW = 352.f;
constexpr float kListTop = 246.f;
constexpr float kListBottom = 52.f;
constexpr float kRowMinH = 20.f;
constexpr float kSectionH = 26.f;
constexpr float kLabelX = 10.f;
constexpr float kValueX = 148.f;
constexpr float kLabelW = kValueX - kLabelX - 8.f;
constexpr float kValueW = kListW - kValueX - 14.f;
constexpr float kTextScale = 0.52f;
constexpr size_t kMaxOldNames = 8;

// "54,872", like the game prints stats.
std::string grouped(int64_t value) {
    auto digits = std::to_string(std::llabs(value));
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(at, ",");
    return value < 0 ? "-" + digits : digits;
}

std::string flat(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

std::string idText(int id, char const* none) {
    return id > 0 ? fmt::format("#{}", id) : none;
}

char const* iconTypeName(IconType type) {
    switch (type) {
        case IconType::Cube: return "Cube";
        case IconType::Ship: return "Ship";
        case IconType::Ball: return "Ball";
        case IconType::Ufo: return "UFO";
        case IconType::Wave: return "Wave";
        case IconType::Robot: return "Robot";
        case IconType::Spider: return "Spider";
        case IconType::Swing: return "Swing";
        case IconType::Jetpack: return "Jetpack";
        default: return "Icono";
    }
}

std::string modBadgeName(int badge) {
    switch (badge) {
        case 0: return "Ninguna";
        case 1: return "Moderador";
        case 2: return "Moderador Elder";
        case 3: return "Moderador de leaderboard";
        default: return fmt::format("Desconocida ({})", badge);
    }
}

// message and comment history privacy share the same three values.
std::string audienceName(int state) {
    switch (state) {
        case 0: return "Todos";
        case 1: return "Solo amigos";
        case 2: return "Nadie";
        default: return fmt::format("Desconocido ({})", state);
    }
}

std::string friendStateName(int state) {
    switch (state) {
        case 0: return "No son amigos";
        case 1: return "Son amigos";
        case 3: return "Le enviaste una solicitud";
        case 4: return "Te envio una solicitud";
        default: return fmt::format("Desconocido ({})", state);
    }
}

std::string difficultyText(progression::DifficultyBreakdown const& info) {
    return fmt::format("Auto {}   Easy {}   Normal {}\nHard {}   Harder {}   Insane {}",
        info.autos, info.easy, info.normal, info.hard, info.harder, info.insane);
}

std::string extrasText(progression::DifficultyBreakdown const& info) {
    return fmt::format("Diarios {}   Gauntlets {}   Map packs {}",
        info.daily, info.gauntlet, info.mapPacks);
}

std::string rolesText(paimon::roles::UserRoles const& roles) {
    std::string text;
    auto add = [&](bool on, char const* name) {
        if (!on) return;
        if (!text.empty()) text += ", ";
        text += name;
    };
    add(roles.admin, "Admin");
    add(roles.mod, "Mod");
    add(roles.vip, "VIP");
    add(roles.helper, "Helper");
    add(roles.idea, "Idea");
    return text;
}

} // namespace

ProfileInfoPopup* ProfileInfoPopup::create(GJUserScore* score, std::string presence) {
    auto ret = new ProfileInfoPopup();
    if (ret && ret->init(score, std::move(presence))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileInfoPopup::init(GJUserScore* score, std::string presence) {
    if (!score) return false;
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_userID = score->m_userID;
    m_accountID = score->m_accountID;
    m_userName = score->m_userName;
    collectRows(score, presence);
    if (!gdhistory::reachable()) m_historyState = HistoryState::Unavailable;

    this->setTitle(m_userName.empty() ? std::string("Perfil") : m_userName, "goldFont.fnt", 0.7f, 22.f);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;
    float const viewH = kListTop - kListBottom;

    m_listBg = paimon::ui::makeInset({kListW + 6.f, viewH + 6.f}, 70);
    m_listBg->setPosition({cx - (kListW + 6.f) / 2.f, kListBottom - 3.f});
    m_mainLayer->addChild(m_listBg, 2);

    m_scroll = ScrollLayer::create({kListW, viewH});
    if (m_scroll) m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({cx - kListW / 2.f, kListBottom});
    m_mainLayer->addChild(m_scroll, 3);

    auto menu = CCMenu::create();
    menu->setPosition({cx, 26.f});
    menu->setContentSize({kPopupW - 40.f, 30.f});
    menu->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu, 4);

    menu->addChild(paimon::ui::makeButton("Copiar IDs",
        [this] { this->onCopyIDs(); }, paimon::ui::Btn::Gray, 0.f, 0.6f, "bigFont.fnt"));
    menu->addChild(paimon::ui::makeButton("Copiar todo",
        [this] { this->onCopyAll(); }, paimon::ui::Btn::Cyan, 0.f, 0.6f, "bigFont.fnt"));
    menu->updateLayout();

    rebuildRows();
    if (gdhistory::available()) requestHistory(false);
    return true;
}

void ProfileInfoPopup::collectRows(GJUserScore* score, std::string const& presence) {
    auto section = [](std::vector<Row>& out, char const* title) {
        out.push_back({RowKind::Section, title, {}, false});
    };
    auto field = [](std::vector<Row>& out, char const* label, std::string value, bool accent = false) {
        out.push_back({RowKind::Field, label, std::move(value), accent});
    };

    section(m_head, "Cuenta");
    field(m_head, "Nombre", m_userName, true);
    field(m_head, "User ID", std::to_string(score->m_userID), true);
    field(m_head, "Account ID", score->m_accountID > 0 ? std::to_string(score->m_accountID) : "Sin cuenta", true);

    field(m_tail, "Insignia de mod", modBadgeName(score->m_modBadge));
    if (auto roles = paimon::roles::RoleService::get().lookup(m_userName); roles && roles->any()) {
        field(m_tail, "Rol en Paimbnails", rolesText(*roles));
    }
    if (!presence.empty()) field(m_tail, "Estado", presence);

    auto const stats = progression::statsFromScore(score);

    section(m_tail, "Estadisticas");
    field(m_tail, "Estrellas", grouped(score->m_stars));
    field(m_tail, "Lunas", grouped(score->m_moons));
    field(m_tail, "Diamantes", grouped(score->m_diamonds));
    field(m_tail, "Demonios", grouped(score->m_demons));
    field(m_tail, "Monedas secretas", grouped(score->m_secretCoins));
    field(m_tail, "Monedas de usuario", grouped(score->m_userCoins));
    field(m_tail, "Puntos de creador", grouped(score->m_creatorPoints));
    field(m_tail, "Ranking global", score->m_globalRank > 0
        ? "#" + grouped(score->m_globalRank) : "Sin ranking");
    if (score->m_playerRank > 0 && score->m_playerRank != score->m_globalRank) {
        field(m_tail, "Posicion en tabla", "#" + grouped(score->m_playerRank));
    }

    if (stats.hasDemonInfo) {
        auto const& demons = stats.demonInfo;
        field(m_tail, "Demons clasicos", fmt::format(
            "Easy {}   Medium {}   Hard {}\nInsane {}   Extreme {}",
            demons.easyClassic, demons.mediumClassic, demons.hardClassic,
            demons.insaneClassic, demons.extremeClassic));
        field(m_tail, "Demons platformer", fmt::format(
            "Easy {}   Medium {}   Hard {}\nInsane {}   Extreme {}",
            demons.easyPlatformer, demons.mediumPlatformer, demons.hardPlatformer,
            demons.insanePlatformer, demons.extremePlatformer));
        field(m_tail, "Demons especiales", fmt::format(
            "Semanales {}   Gauntlets {}", demons.weekly, demons.gauntlet));
    }
    if (stats.hasClassicInfo) {
        field(m_tail, "Niveles clasicos", difficultyText(stats.classicInfo));
        field(m_tail, "Clasicos extra", extrasText(stats.classicInfo));
    }
    if (stats.hasPlatformerInfo) {
        field(m_tail, "Niveles platformer", difficultyText(stats.platformerInfo));
    }

    section(m_tail, "Iconos");
    field(m_tail, "Icono principal", fmt::format("{} #{}", iconTypeName(score->m_iconType), score->m_iconID));
    field(m_tail, "Iconos equipados", fmt::format(
        "Cube {}   Ship {}   Ball {}\nUFO {}   Wave {}   Robot {}\nSpider {}   Swing {}   Jetpack {}",
        score->m_playerCube, score->m_playerShip, score->m_playerBall,
        score->m_playerUfo, score->m_playerWave, score->m_playerRobot,
        score->m_playerSpider, score->m_playerSwing, score->m_playerJetpack));
    field(m_tail, "Colores", fmt::format("Color 1 #{}   Color 2 #{}   Glow #{}",
        score->m_color1, score->m_color2, score->m_color3));
    field(m_tail, "Glow activado", score->m_glowEnabled ? "Si" : "No");
    field(m_tail, "Estela", idText(score->m_playerStreak, "Ninguna"));
    field(m_tail, "Efecto de muerte", idText(score->m_playerExplosion, "Ninguno"));

    section(m_tail, "Privacidad");
    field(m_tail, "Mensajes", audienceName(score->m_messageState));
    field(m_tail, "Solicitudes de amistad", score->m_friendReqStatus == 0 ? "Abiertas" : "Cerradas");
    field(m_tail, "Historial de comentarios", audienceName(score->m_commentHistoryStatus));

    auto* accounts = GJAccountManager::get();
    bool const ownProfile = accounts && score->m_accountID > 0
        && accounts->m_accountID == score->m_accountID;
    if (!ownProfile) field(m_tail, "Amistad contigo", friendStateName(score->m_friendStatus));

    section(m_tail, "Redes sociales");
    size_t const socialsStart = m_tail.size();
    auto social = [&](char const* label, std::string const& value) {
        if (!value.empty()) field(m_tail, label, value);
    };
    social("YouTube", score->m_youtubeURL);
    social("Twitter / X", score->m_twitterURL);
    social("Twitch", score->m_twitchURL);
    social("Discord", score->m_discordUsername);
    social("Instagram", score->m_instagramURL);
    social("TikTok", score->m_tiktokURL);
    if (m_tail.size() == socialsStart) field(m_tail, "Redes", "Ninguna");
}

std::vector<ProfileInfoPopup::Row> ProfileInfoPopup::historyRows() const {
    constexpr char const* kCreated = "Creacion (aprox.)";
    std::vector<Row> rows;

    switch (m_historyState) {
        case HistoryState::Idle:
            rows.push_back({RowKind::HistoryPrompt, kCreated, "Toca para consultar GDHistory", false});
            break;
        case HistoryState::Loading:
            rows.push_back({RowKind::Field, kCreated, "Consultando GDHistory...", false});
            break;
        case HistoryState::Missing:
            rows.push_back({RowKind::Field, kCreated, "GDHistory no tiene datos", false});
            break;
        case HistoryState::Unavailable:
            rows.push_back({RowKind::Field, kCreated, "No disponible en servidores privados", false});
            break;
        case HistoryState::Ready: {
            rows.push_back({RowKind::Field, kCreated,
                m_history.createdApprox.empty() ? "Sin datos" : m_history.createdApprox, true});
            if (!m_history.firstSeen.empty()) {
                rows.push_back({RowKind::Field, "Primer registro", m_history.firstSeen, false});
            }

            std::string oldNames;
            size_t count = 0;
            size_t skipped = 0;
            for (auto const& name : m_history.names) {
                if (name == m_userName) continue;
                if (count >= kMaxOldNames) {
                    ++skipped;
                    continue;
                }
                if (!oldNames.empty()) oldNames += ", ";
                oldNames += name;
                ++count;
            }
            if (skipped > 0) oldNames += fmt::format(" (+{})", skipped);
            if (!oldNames.empty()) rows.push_back({RowKind::Field, "Nombres anteriores", oldNames, false});
            break;
        }
    }
    return rows;
}

void ProfileInfoPopup::rebuildRows() {
    if (!m_scroll) return;
    auto* content = m_scroll->m_contentLayer;
    if (!content) return;

    float const viewH = kListTop - kListBottom;
    // distance scrolled down from the top, so a late answer does not yank the list back up.
    float const scrolled = content->getPositionY()
        - std::min(0.f, viewH - content->getContentSize().height);

    m_rows.clear();
    m_rows.insert(m_rows.end(), m_head.begin(), m_head.end());
    auto history = historyRows();
    m_rows.insert(m_rows.end(), history.begin(), history.end());
    m_rows.insert(m_rows.end(), m_tail.begin(), m_tail.end());

    content->removeAllChildren();

    // wrapped values change the row height, so every label exists before anything is placed.
    struct Item {
        CCLabelBMFont* label = nullptr;
        CCLabelBMFont* value = nullptr;
        float height = kSectionH;
    };
    std::vector<Item> items(m_rows.size());

    float totalH = 8.f;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        auto const& row = m_rows[i];
        auto& item = items[i];
        if (row.kind != RowKind::Section) {
            item.label = CCLabelBMFont::create(row.label.c_str(), "chatFont.fnt");
            item.label->setColor(kLabel);
            item.label->limitLabelWidth(kLabelW, kTextScale, kTextScale * 0.5f);

            auto color = row.kind == RowKind::HistoryPrompt ? paimon::ui::palette::info
                : (row.accent ? kAccent : kValue);
            item.value = paimon::ui::makeText(row.value.c_str(), kValueW, kTextScale, color);
            item.height = std::max(kRowMinH, item.value->getScaledContentSize().height + 10.f);
        }
        totalH += item.height;
    }
    totalH = std::max(totalH, viewH);
    content->setContentSize({kListW, totalH});

    auto* rowMenu = CCMenu::create();
    rowMenu->setPosition({0.f, 0.f});
    rowMenu->setContentSize({kListW, totalH});
    content->addChild(rowMenu, 2);

    float y = totalH - 4.f;
    int band = 0;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        auto const& row = m_rows[i];
        auto& item = items[i];
        y -= item.height;
        float const midY = y + item.height / 2.f;

        if (row.kind == RowKind::Section) {
            auto title = CCLabelBMFont::create(row.label.c_str(), "bigFont.fnt");
            title->setScale(0.4f);
            title->setColor(paimon::ui::palette::gold);
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({kLabelX - 2.f, midY - 1.f});
            content->addChild(title, 1);

            auto line = paimon::ui::makeDivider(kListW - 16.f);
            line->setPosition({kListW / 2.f, y + 2.f});
            content->addChild(line, 1);
            band = 0;
            continue;
        }

        auto stripe = CCLayerColor::create(ccc4(255, 255, 255, band % 2 == 0 ? 14 : 0));
        stripe->setContentSize({kListW, item.height});
        stripe->setPosition({0.f, y});
        content->addChild(stripe, 0);
        ++band;

        item.label->setAnchorPoint({0.f, 0.5f});
        item.label->setPosition({kLabelX, midY});
        content->addChild(item.label, 1);

        item.value->setAnchorPoint({0.f, 0.5f});
        item.value->setPosition({kValueX, midY});
        content->addChild(item.value, 1);

        auto hit = CCLayerColor::create(ccc4(255, 255, 255, 0));
        hit->setContentSize({kListW - 8.f, item.height - 2.f});
        auto btn = CCMenuItemSpriteExtra::create(hit, this, menu_selector(ProfileInfoPopup::onRow));
        btn->setContentSize({kListW - 8.f, item.height - 2.f});
        btn->setAnchorPoint({0.f, 0.f});
        btn->setPosition({4.f, y + 1.f});
        btn->setTag(static_cast<int>(i));
        rowMenu->addChild(btn);

        if (row.kind == RowKind::HistoryPrompt) {
            auto arrow = CCLabelBMFont::create(">", "chatFont.fnt");
            arrow->setScale(0.5f);
            arrow->setColor(paimon::ui::palette::info);
            arrow->setPosition({kListW - 12.f, midY});
            content->addChild(arrow, 1);
        }
    }

    m_scroll->moveToTop();
    if (scrolled > 0.f) {
        float const top = viewH - totalH;
        content->setPositionY(std::clamp(top + scrolled, std::min(0.f, top), 0.f));
    }
}

// never rebuild from inside a row tap: the list would drop the button that is still handling it.
void ProfileInfoPopup::queueRebuild() {
    this->unschedule(schedule_selector(ProfileInfoPopup::onRebuildTick));
    this->scheduleOnce(schedule_selector(ProfileInfoPopup::onRebuildTick), 0.f);
}

void ProfileInfoPopup::onRebuildTick(float) {
    rebuildRows();
}

void ProfileInfoPopup::requestHistory(bool userInitiated) {
    if (m_historyState == HistoryState::Loading) return;

    if (!gdhistory::reachable()) {
        m_historyState = HistoryState::Unavailable;
        queueRebuild();
        return;
    }

    m_historyState = HistoryState::Loading;
    queueRebuild();

    auto self = WeakRef<ProfileInfoPopup>(this);
    gdhistory::requestUserHistory(m_userID, userInitiated,
        [self](gdhistory::UserHistory const& history, bool ok) {
            auto ref = self.lock();
            if (!ref) return;
            static_cast<ProfileInfoPopup*>(ref.data())->applyHistory(history, ok);
        });
}

void ProfileInfoPopup::applyHistory(gdhistory::UserHistory const& history, bool ok) {
    m_history = history;
    m_historyState = ok ? HistoryState::Ready : HistoryState::Missing;
    queueRebuild();
}

std::string ProfileInfoPopup::plainText() const {
    std::string out;
    for (auto const& row : m_rows) {
        if (row.kind == RowKind::Section) {
            if (!out.empty()) out += "\n";
            out += fmt::format("[{}]\n", row.label);
        } else if (row.kind == RowKind::Field) {
            out += fmt::format("{}: {}\n", row.label, flat(row.value));
        }
    }
    return out;
}

void ProfileInfoPopup::onRow(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    if (index < 0 || index >= static_cast<int>(m_rows.size())) return;

    auto const& row = m_rows[index];
    if (row.kind == RowKind::HistoryPrompt) {
        requestHistory(true);
        return;
    }
    if (row.kind != RowKind::Field || row.value.empty()) return;

    clipboard::write(flat(row.value));
    PaimonNotify::show(fmt::format("{} copiado", row.label), NotificationIcon::Success);
}

void ProfileInfoPopup::onCopyIDs() {
    clipboard::write(m_accountID > 0
        ? fmt::format("user {}  -  acc {}", m_userID, m_accountID)
        : fmt::format("user {}", m_userID));
    PaimonNotify::show("IDs copiados", NotificationIcon::Success);
}

void ProfileInfoPopup::onCopyAll() {
    clipboard::write(plainText());
    PaimonNotify::show("Info del perfil copiada", NotificationIcon::Success);
}

} // namespace paimon::info
