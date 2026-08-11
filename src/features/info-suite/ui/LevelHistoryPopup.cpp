#include "LevelHistoryPopup.hpp"
#include "LevelHistoryDetailPopup.hpp"
#include "InfoBlocks.hpp"
#include "../services/GDHistoryClient.hpp"
#include "../services/LevelFacts.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;
using namespace paimon::info::blocks;

namespace paimon::info {

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 292.f;
constexpr float kListW = 398.f;
constexpr float kListH = 198.f;
constexpr float kListX = (kPopupW - kListW) / 2.f;
constexpr float kListY = 44.f;
constexpr float kCardW = kListW - 12.f;
constexpr float kStateH = 74.f;
constexpr float kMilesH = 62.f;
constexpr float kCellH = 62.f;
constexpr float kHeadH = 18.f;
constexpr float kGap = 4.f;

constexpr ccColor3B kDim{150, 150, 150};
constexpr ccColor3B kSoft{205, 205, 205};

char const* featureCaption(GJFeatureState state) {
    switch (state) {
        case GJFeatureState::Featured:  return "FEATURED";
        case GJFeatureState::Epic:      return "EPIC";
        case GJFeatureState::Legendary: return "LEGENDARY";
        case GJFeatureState::Mythic:    return "MYTHIC";
        default:                        return "FEATURE";
    }
}

// Las monedas de featured y epic cambiaron de nombre entre versiones, asi que
// cada estado lleva su lista de recambios hasta algo que exista seguro.
std::vector<char const*> featureFrames(GJFeatureState state) {
    switch (state) {
        case GJFeatureState::Mythic:
            return {"GJ_epicCoin3_001.png", "GJ_epicCoin_001.png", "GJ_starsIcon_001.png"};
        case GJFeatureState::Legendary:
            return {"GJ_epicCoin2_001.png", "GJ_epicCoin_001.png", "GJ_starsIcon_001.png"};
        case GJFeatureState::Epic:
            return {"GJ_epicCoin_001.png", "GJ_featuredCoin_001.png", "GJ_starsIcon_001.png"};
        default:
            return {"GJ_featuredCoin_001.png", "GJ_starsIcon_001.png"};
    }
}

ccColor3B milestoneColor(HistoryEntry const& entry, HistoryMilestone milestone) {
    switch (milestone) {
        case HistoryMilestone::First:      return {150, 178, 215};
        case HistoryMilestone::Rated:      return {255, 214, 92};
        case HistoryMilestone::Restarred:  return {255, 186, 120};
        case HistoryMilestone::Unrated:    return {232, 118, 118};
        case HistoryMilestone::Difficulty: return {168, 220, 255};
        case HistoryMilestone::Featured:   return {118, 198, 255};
        case HistoryMilestone::Unfeatured: return {198, 130, 130};
        case HistoryMilestone::Version:    return {148, 226, 148};
        case HistoryMilestone::Epic:       break;
    }
    switch (entry.feature) {
        case GJFeatureState::Legendary: return {110, 232, 255};
        case GJFeatureState::Mythic:    return {255, 108, 130};
        default:                        return {255, 158, 68};
    }
}

char const* milestoneButton(HistoryEntry const& entry, HistoryMilestone milestone) {
    switch (milestone) {
        case HistoryMilestone::First:      return "GJ_button_02.png";
        case HistoryMilestone::Rated:      return "GJ_button_01.png";
        case HistoryMilestone::Restarred:  return "GJ_button_03.png";
        case HistoryMilestone::Unrated:    return "GJ_button_06.png";
        case HistoryMilestone::Difficulty: return "GJ_button_02.png";
        case HistoryMilestone::Featured:   return "GJ_button_03.png";
        case HistoryMilestone::Unfeatured: return "GJ_button_06.png";
        case HistoryMilestone::Version:    return "GJ_button_05.png";
        case HistoryMilestone::Epic:       break;
    }
    switch (entry.feature) {
        case GJFeatureState::Legendary: return "GJ_button_02.png";
        case GJFeatureState::Mythic:    return "GJ_button_06.png";
        default:                        return "GJ_button_03.png";
    }
}

// Icono + texto ya medidos, para poder encadenar chips en una fila.
CCNode* makeChip(std::vector<char const*> const& frames, std::string const& text,
                 ccColor3B color, float iconScale, float textScale, float maxTextWidth = 0.f) {
    constexpr float height = 13.f;
    auto* chip = CCNode::create();
    float cursor = 0.f;

    if (auto* icon = firstFrame(frames)) {
        icon->setScale(iconScale);
        icon->setAnchorPoint({0.f, 0.5f});
        icon->setPosition({0.f, height / 2.f});
        chip->addChild(icon);
        cursor = icon->getScaledContentSize().width + 3.f;
    }

    if (auto* label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt")) {
        label->setScale(textScale);
        label->setColor(color);
        label->setAnchorPoint({0.f, 0.5f});
        if (maxTextWidth > 0.f) label->limitLabelWidth(maxTextWidth, textScale, textScale * 0.5f);
        label->setPosition({cursor, height / 2.f});
        chip->addChild(label);
        cursor += label->getScaledContentSize().width;
    }

    chip->setContentSize({cursor, height});
    chip->setAnchorPoint({0.f, 0.5f});
    return chip;
}

// Coloca los chips de izquierda a derecha y descarta los que ya no entran.
void flowChips(CCNode* parent, std::vector<CCNode*> const& chips, float x, float y,
               float maxWidth) {
    float cursor = 0.f;
    for (auto* chip : chips) {
        if (!chip) continue;
        float width = chip->getContentSize().width;
        if (cursor > 0.f && cursor + width > maxWidth) break;
        chip->setPosition({x + cursor, y});
        parent->addChild(chip, 1);
        cursor += width + 9.f;
    }
}

CCNode* makeBadge(std::string const& text, char const* background) {
    auto* sprite = ButtonSprite::create(
        text.c_str(), 0, false, "bigFont.fnt", background, 30.f, 0.56f);
    if (!sprite) return nullptr;

    sprite->setScale(0.5f);
    auto* badge = CCNode::create();
    badge->setContentSize(sprite->getScaledContentSize());
    sprite->setPosition(badge->getContentSize() / 2.f);
    badge->addChild(sprite);
    return badge;
}

// Numero de estrellas (o lunas, en plataformas) centrado en un punto.
void addStarBadge(CCNode* parent, int stars, bool platformer, CCPoint center,
                  float textScale, float iconScale) {
    if (stars <= 0) return;

    auto* label = CCLabelBMFont::create(std::to_string(stars).c_str(), "bigFont.fnt");
    if (!label) return;
    label->setScale(textScale);
    label->setColor(kAccent);
    label->setAnchorPoint({0.f, 0.5f});

    auto* icon = platformer
        ? firstFrame({"GJ_moonsIcon_001.png", "GJ_starsIcon_001.png"})
        : firstFrame({"GJ_starsIcon_001.png"});
    if (icon) icon->setScale(iconScale);

    float const labelW = label->getScaledContentSize().width;
    float const iconW = icon ? icon->getScaledContentSize().width : 0.f;
    float const gap = icon ? 2.f : 0.f;
    float x = center.x - (labelW + gap + iconW) / 2.f;

    label->setPosition({x, center.y});
    parent->addChild(label, 1);

    if (icon) {
        icon->setAnchorPoint({0.f, 0.5f});
        icon->setPosition({x + labelW + gap, center.y});
        parent->addChild(icon, 1);
    }
}

// Una de las cuatro casillas de la fila de hitos: dibujo arriba, de que va en
// medio y cuando ocurrio abajo.
void addTile(CCNode* parent, int slot, float width, float height, CCNode* icon,
             std::string const& caption, std::string const& value, ccColor3B valueColor,
             char const* valueFont) {
    auto* tile = CCNode::create();
    tile->setContentSize({width, height});
    tile->setPosition({slot * width, 0.f});
    parent->addChild(tile, 1);

    if (icon) {
        icon->setPosition({width / 2.f, height - 18.f});
        tile->addChild(icon);
    }

    addText(tile, caption.c_str(), "chatFont.fnt", 0.3f, kLabel,
            {width / 2.f, 22.f}, {0.5f, 0.5f}, width - 6.f);
    addText(tile, value.c_str(), valueFont, 0.34f, valueColor,
            {width / 2.f, 9.f}, {0.5f, 0.5f}, width - 6.f);
}

CCNode* makeSectionLabel(float width, std::string const& text) {
    auto* node = CCNode::create();
    node->setContentSize({width, kHeadH});
    addText(node, text.c_str(), "chatFont.fnt", 0.34f, kLabel,
            {4.f, kHeadH / 2.f}, {0.f, 0.5f}, width - 8.f);
    return node;
}

} // namespace

LevelHistoryPopup* LevelHistoryPopup::create(GJGameLevel* level) {
    if (!paimon::modules::isEnabled(gdhistory::kModuleId)) return nullptr;

    auto ret = new LevelHistoryPopup();
    if (ret && ret->init(level)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LevelHistoryPopup::init(GJGameLevel* level) {
    if (!level || level->m_levelID.value() <= 0) return false;
    if (!paimon::modules::isEnabled(gdhistory::kModuleId)) return false;
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);
    m_level = level;
    setTitle("Historial del nivel", "goldFont.fnt", 0.65f, 18.f);

    m_statusLabel = CCLabelBMFont::create("Cargando historial...", "chatFont.fnt");
    m_statusLabel->setScale(0.42f);
    m_statusLabel->setColor(kDim);
    m_statusLabel->setPosition({kPopupW / 2.f, kListY + kListH + 11.f});
    m_mainLayer->addChild(m_statusLabel, 3);

    if (auto* panel = paimon::SpriteHelper::createDarkPanel(kListW, kListH, 65, 5.f)) {
        panel->setPosition({kListX, kListY});
        m_mainLayer->addChild(panel);
    }

    m_scroll = geode::ScrollLayer::create({kListW, kListH});
    if (!m_scroll) return false;
    m_scroll->setPosition({kListX, kListY});
    m_mainLayer->addChild(m_scroll, 2);

    m_content = CCNode::create();
    m_content->setContentSize({kListW, kListH});
    m_scroll->m_contentLayer->addChild(m_content);
    m_scroll->m_contentLayer->setContentSize({kListW, kListH});

    m_toolMenu = CCMenu::create();
    m_toolMenu->setPosition({0.f, 0.f});
    m_toolMenu->setVisible(false);
    m_mainLayer->addChild(m_toolMenu, 3);

    auto addTool = [this](char const* text, SEL_MenuHandler handler, float x) -> ButtonSprite* {
        auto* sprite = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.7f);
        if (!sprite) return nullptr;
        sprite->setScale(0.5f);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, handler);
        if (!button) return nullptr;
        button->setPosition({x, 23.f});
        m_toolMenu->addChild(button);
        return sprite;
    };

    m_orderSprite = addTool("Viejos primero",
                            menu_selector(LevelHistoryPopup::onOrder), kPopupW / 2.f - 66.f);
    m_filterSprite = addTool("Todos",
                             menu_selector(LevelHistoryPopup::onFilter), kPopupW / 2.f + 66.f);
    refreshToolButtons();

    loadHistory();
    return true;
}

void LevelHistoryPopup::loadHistory() {
    auto levelID = m_level ? m_level->m_levelID.value() : 0;
    if (levelID <= 0) return;

    WeakRef<LevelHistoryPopup> self = this;
    gdhistory::requestLevelHistory(levelID, [self](matjson::Value root) {
        if (auto popup = self.lock()) popup->applyHistory(std::move(root));
    });

    // La fecha de subida no sale de los snapshots: es la estimacion aparte que
    // publica GDHistory, y suele llegar despues que la lista.
    gdhistory::requestLevelDate(levelID, [self](std::string const& date) {
        auto popup = self.lock();
        if (!popup || date.empty()) return;
        popup->m_uploadDate = date;
        if (popup->m_loaded) popup->rebuildList();
    });
}

void LevelHistoryPopup::applyHistory(matjson::Value root) {
    if (!m_statusLabel) return;

    if (!root.isObject()) {
        m_statusLabel->setString("No se pudo cargar el historial.");
        m_statusLabel->setColor({255, 160, 160});
        return;
    }

    m_history = parseLevelHistory(root);
    if (m_history.entries.empty()) {
        m_statusLabel->setString("Este nivel no tiene registros.");
        m_statusLabel->setColor(kDim);
        return;
    }

    m_loaded = true;
    m_statusLabel->setString(fmt::format("{} registros  -  del {} al {}",
        m_history.entries.size(), m_history.entries.front().date,
        m_history.entries.back().date).c_str());
    m_statusLabel->setColor({185, 220, 185});

    if (m_toolMenu) m_toolMenu->setVisible(true);
    rebuildList();
}

void LevelHistoryPopup::refreshToolButtons() {
    // Los botones dicen como esta la lista ahora, no lo que haria pulsarlos.
    auto apply = [](ButtonSprite* sprite, char const* text) {
        if (!sprite) return;
        sprite->setString(text);
        // El boton se quedaria con el area de toque del texto anterior.
        if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sprite->getParent())) {
            item->setContentSize(sprite->getScaledContentSize());
        }
    };

    apply(m_orderSprite, m_newestFirst ? "Nuevos primero" : "Viejos primero");
    apply(m_filterSprite, m_onlyMilestones ? "Solo hitos" : "Todos");
}

void LevelHistoryPopup::rebuildList() {
    if (!m_content || !m_scroll) return;
    m_content->removeAllChildren();

    std::vector<int> shown;
    shown.reserve(m_history.entries.size());
    for (int i = 0; i < static_cast<int>(m_history.entries.size()); i++) {
        if (m_onlyMilestones && m_history.entries[i].milestones.empty()) continue;
        shown.push_back(i);
    }
    if (m_newestFirst) std::reverse(shown.begin(), shown.end());

    std::vector<CCNode*> stack;
    if (auto* node = makeStateBlock(kCardW)) stack.push_back(node);
    if (auto* node = makeMilestoneBlock(kCardW)) stack.push_back(node);
    stack.push_back(makeSectionLabel(kCardW, fmt::format("REGISTROS ({})", shown.size())));

    for (int index : shown) {
        if (auto* node = makeEntryCell(m_history.entries[index], index, kCardW)) {
            stack.push_back(node);
        }
    }

    float totalH = kGap;
    for (auto* node : stack) totalH += node->getContentSize().height + kGap;
    totalH = std::max(kListH, totalH);

    m_content->setContentSize({kListW, totalH});
    m_scroll->m_contentLayer->setContentSize({kListW, totalH});

    float y = totalH - kGap;
    for (auto* node : stack) {
        float const height = node->getContentSize().height;
        node->setAnchorPoint({0.5f, 0.5f});
        node->setPosition({kListW / 2.f, y - height / 2.f});
        m_content->addChild(node);
        y -= height + kGap;
    }

    m_scroll->scrollToTop();
}

CCNode* LevelHistoryPopup::makeStateBlock(float width) {
    auto const& history = m_history;
    auto* block = makeBlock(width, kStateH, 85);

    constexpr float faceX = 34.f;
    if (auto* face = GJDifficultySprite::create(history.face, GJDifficultyName::Short)) {
        face->updateFeatureState(history.feature);
        face->setScale(0.72f);
        face->setPosition({faceX, kStateH - 29.f});
        block->addChild(face, 1);
    }
    addStarBadge(block, history.stars, history.length == 5, {faceX, 13.f}, 0.42f, 0.46f);

    float const textX = 70.f;
    float const rightX = width - 12.f;
    float const textW = width - textX - 12.f;

    auto name = history.levelName;
    if (name.empty()) name = std::string(m_level->m_levelName);
    if (name.empty()) name = "Nivel sin nombre";
    addText(block, name.c_str(), "bigFont.fnt", 0.5f, kValue,
            {textX, kStateH - 15.f}, {0.f, 0.5f}, textW * 0.68f);

    addText(block, fmt::format("#{}", m_level->m_levelID.value()).c_str(), "chatFont.fnt",
            0.36f, kLabel, {rightX, kStateH - 15.f}, {1.f, 0.5f}, textW * 0.3f);

    auto author = history.username.empty() ? std::string("Usuario desconocido") : history.username;
    addText(block, fmt::format("por {}", author).c_str(), "chatFont.fnt", 0.4f, kSoft,
            {textX, kStateH - 34.f}, {0.f, 0.5f}, textW * 0.44f);

    if (!history.song.empty()) {
        auto* chip = makeChip({"GJ_sMusicIcon_001.png", "GJ_musicOnBtn_001.png"},
                              history.song, kDim, 0.5f, 0.34f, textW * 0.46f);
        chip->setAnchorPoint({1.f, 0.5f});
        chip->setPosition({rightX, kStateH - 34.f});
        block->addChild(chip, 1);
    }

    // Lo raro va primero: si la fila se queda sin sitio se cortan los ultimos,
    // y un nivel borrado o un daily importan mas que la duracion.
    std::vector<CCNode*> chips;
    if (history.deleted) {
        chips.push_back(makeChip({"GJ_deleteIcon_001.png", "GJ_deleteBtn_001.png"},
                                 history.deletedDate.empty()
                                     ? std::string("Borrado")
                                     : fmt::format("Borrado {}", history.deletedDate),
                                 {245, 120, 120}, 0.5f, 0.34f));
    }
    if (history.dailyID > 0) {
        chips.push_back(makeChip({"GJ_calendarBtn_001.png", "GJ_timeIcon_001.png"},
                                 history.dailyID > 100000
                                     ? fmt::format("Weekly {}", history.dailyID - 100000)
                                     : fmt::format("Daily {}", history.dailyID),
                                 kAccent, 0.5f, 0.34f));
    }
    if (history.downloads >= 0) {
        chips.push_back(makeChip({"GJ_downloadsIcon_001.png"},
                                 formatThousands(history.downloads), kSoft, 0.5f, 0.34f));
    }
    if (history.likes >= 0) {
        chips.push_back(makeChip({"GJ_likesIcon_001.png", "GJ_like2Icon_001.png"},
                                 formatThousands(history.likes), kSoft, 0.5f, 0.34f));
    }
    if (history.objects > 0) {
        chips.push_back(makeChip({"GJ_hammerIcon_001.png", "GJ_sBlocksIcon_001.png"},
                                 formatThousands(history.objects), kSoft, 0.5f, 0.34f));
    }
    if (auto length = lengthName(history.length); !length.empty()) {
        chips.push_back(makeChip({"GJ_timeIcon_001.png"}, length, kSoft, 0.5f, 0.34f));
    }
    flowChips(block, chips, textX, 13.f, textW);

    return block;
}

CCNode* LevelHistoryPopup::makeMilestoneBlock(float width) {
    auto const& history = m_history;
    if (history.entries.empty()) return nullptr;

    auto const& first = history.entries.front();
    auto* block = makeBlock(width, kMilesH, 85);
    float const tileW = width / 4.f;

    // Las fechas exactas van en dorado; lo que solo se puede acotar ("antes
    // de...") o nunca paso se queda en gris para no confundirlas.
    //
    // Fecha de subida: la estimacion de GDHistory cuando llego, y si no el
    // snapshot mas viejo que existe.
    auto* uploadIcon = firstFrame({"GJ_timeIcon_001.png"});
    if (uploadIcon) uploadIcon->setScale(0.5f);
    addTile(block, 0, tileW, kMilesH, uploadIcon,
            m_uploadDate.empty() ? "PRIMER DATO" : "SUBIDO",
            m_uploadDate.empty() ? first.date : m_uploadDate, kValue, "goldFont.fnt");

    auto* rateIcon = GJDifficultySprite::create(
        history.rateIndex >= 0 ? history.entries[history.rateIndex].face : history.face,
        GJDifficultyName::Short);
    if (rateIcon) rateIcon->setScale(0.42f);

    std::string rateValue = "Sin rate";
    ccColor3B rateColor = kDim;
    char const* rateFont = "chatFont.fnt";
    if (history.rateIndex >= 0) {
        rateValue = history.entries[history.rateIndex].date;
        rateColor = kValue;
        rateFont = "goldFont.fnt";
    } else if (history.stars > 0) {
        rateValue = fmt::format("antes de {}", first.date);
        rateColor = kSoft;
    }
    auto rateCaption = history.stars > 0 ? fmt::format("RATE {}", history.stars)
                                         : std::string("RATE");
    addTile(block, 1, tileW, kMilesH, rateIcon, rateCaption, rateValue, rateColor, rateFont);

    auto* featureIcon = firstFrame(featureFrames(history.feature));
    if (featureIcon) featureIcon->setScale(0.55f);

    std::string featureValue = "No";
    ccColor3B featureColor = kDim;
    char const* featureFont = "chatFont.fnt";
    if (history.featureIndex >= 0) {
        featureValue = history.entries[history.featureIndex].date;
        featureColor = kValue;
        featureFont = "goldFont.fnt";
    } else if (history.feature != GJFeatureState::None) {
        featureValue = fmt::format("antes de {}", first.date);
        featureColor = kSoft;
    }
    addTile(block, 2, tileW, kMilesH, featureIcon, featureCaption(history.feature),
            featureValue, featureColor, featureFont);

    auto* versionIcon = firstFrame({"GJ_hammerIcon_001.png", "GJ_sBlocksIcon_001.png"});
    if (versionIcon) versionIcon->setScale(0.5f);

    std::string versionValue = "Sin cambios";
    ccColor3B versionColor = kDim;
    char const* versionFont = "chatFont.fnt";
    if (history.versionIndex >= 0) {
        versionValue = history.entries[history.versionIndex].date;
        versionColor = kValue;
        versionFont = "goldFont.fnt";
    }
    auto versionCaption = history.version > 0 ? fmt::format("VERSION v{}", history.version)
                                              : std::string("VERSION");
    addTile(block, 3, tileW, kMilesH, versionIcon, versionCaption, versionValue,
            versionColor, versionFont);

    return block;
}

CCNode* LevelHistoryPopup::makeEntryCell(HistoryEntry const& entry, int index, float width) {
    auto* holder = CCNode::create();
    holder->setContentSize({width, kCellH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kCellH});
    holder->addChild(menu);

    auto* panel = paimon::SpriteHelper::createDarkPanel(width, kCellH, 72, 5.f);
    if (!panel) return holder;

    auto* button = CCMenuItemSpriteExtra::create(
        panel, this, menu_selector(LevelHistoryPopup::onEntry));
    if (!button) return holder;

    button->setSizeMult(1.02f);
    button->setTag(index);
    button->setContentSize({width, kCellH});
    button->setPosition({width / 2.f, kCellH / 2.f});
    menu->addChild(button);

    // El boton coloca el fondo a su manera; recolocarlo deja toda la fila en
    // coordenadas de (0,0) a (ancho,alto), que es donde va el resto.
    panel->setAnchorPoint({0.f, 0.f});
    panel->setPosition({0.f, 0.f});

    constexpr float faceX = 27.f;
    if (auto* face = GJDifficultySprite::create(entry.face, GJDifficultyName::Short)) {
        face->updateFeatureState(entry.feature);
        face->setScale(0.55f);
        face->setPosition({faceX, kCellH - 24.f});
        button->addChild(face, 1);
    }
    addStarBadge(button, entry.stars, entry.length == 5, {faceX, 12.f}, 0.34f, 0.38f);

    float const textX = 54.f;
    float const rightX = width - 10.f;
    float const textW = width - textX - 114.f;

    auto name = entry.levelName.empty() ? m_history.levelName : entry.levelName;
    if (name.empty()) name = "Nivel sin nombre";
    addText(button, name.c_str(), "bigFont.fnt", 0.42f, kValue,
            {textX, kCellH - 14.f}, {0.f, 0.5f}, textW);

    auto author = entry.username.empty() ? m_history.username : entry.username;
    auto line = author.empty() ? std::string("Usuario desconocido") : fmt::format("por {}", author);
    if (entry.version > 0) line += fmt::format("  -  v{}", entry.version);
    addText(button, line.c_str(), "chatFont.fnt", 0.36f, kSoft,
            {textX, kCellH - 32.f}, {0.f, 0.5f}, 150.f);

    std::vector<CCNode*> chips;
    if (entry.downloads >= 0) {
        chips.push_back(makeChip({"GJ_downloadsIcon_001.png"},
                                 formatThousands(entry.downloads), kDim, 0.45f, 0.32f));
    }
    if (entry.likes >= 0) {
        chips.push_back(makeChip({"GJ_likesIcon_001.png", "GJ_like2Icon_001.png"},
                                 formatThousands(entry.likes), kDim, 0.45f, 0.32f));
    }
    if (entry.objects > 0) {
        chips.push_back(makeChip({"GJ_hammerIcon_001.png", "GJ_sBlocksIcon_001.png"},
                                 formatThousands(entry.objects), kDim, 0.45f, 0.32f));
    }
    if (auto length = lengthName(entry.length); !length.empty()) {
        chips.push_back(makeChip({"GJ_timeIcon_001.png"}, length, kDim, 0.45f, 0.32f));
    }
    if (entry.coins > 0) {
        chips.push_back(makeChip({entry.coinsVerified
                                      ? "GJ_coinsIcon_001.png"
                                      : "GJ_coinsIcon2_001.png"},
                                 std::to_string(entry.coins), kDim, 0.45f, 0.32f));
    }
    flowChips(button, chips, textX, 12.f, width - textX - 106.f);

    addText(button, entry.date.c_str(), "goldFont.fnt", 0.36f, kValue,
            {rightX, kCellH - 14.f}, {1.f, 0.5f}, 104.f);

    auto footer = entry.clock.empty() ? entry.source
                                      : fmt::format("{}  {}", entry.clock, entry.source);
    addText(button, footer.c_str(), "chatFont.fnt", 0.3f, {130, 130, 130},
            {rightX, 12.f}, {1.f, 0.5f}, 100.f);

    float badgeX = rightX;
    int drawn = 0;
    for (auto milestone : entry.milestones) {
        if (drawn >= 2) break;
        auto* badge = makeBadge(
            milestoneLabel(entry, milestone), milestoneButton(entry, milestone));
        if (!badge) continue;
        badge->setAnchorPoint({1.f, 0.5f});
        badge->setPosition({badgeX, kCellH - 32.f});
        button->addChild(badge, 2);
        badgeX -= badge->getContentSize().width + 4.f;
        drawn++;
    }

    if (!entry.milestones.empty()) {
        auto const color = milestoneColor(entry, entry.milestones.front());
        if (auto* outline = paimon::SpriteHelper::createRoundedRectOutline(
                width, kCellH, 5.f,
                {color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.85f}, 1.2f)) {
            outline->setPosition({0.f, 0.f});
            button->addChild(outline, 3);
        }
    }

    return holder;
}

void LevelHistoryPopup::onEntry(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;

    int index = node->getTag();
    if (index < 0 || index >= static_cast<int>(m_history.entries.size())) return;

    if (auto* popup = LevelHistoryDetailPopup::create(m_history.entries[index])) {
        popup->show();
    }
}

void LevelHistoryPopup::onOrder(CCObject*) {
    m_newestFirst = !m_newestFirst;
    refreshToolButtons();
    rebuildList();
}

void LevelHistoryPopup::onFilter(CCObject*) {
    m_onlyMilestones = !m_onlyMilestones;
    refreshToolButtons();
    rebuildList();
}

} // namespace paimon::info
