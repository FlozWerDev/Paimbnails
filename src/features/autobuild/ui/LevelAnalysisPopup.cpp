#include "LevelAnalysisPopup.hpp"

#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/TemplateStore.hpp"
#include "AutobuildPopup.hpp"
#include "PreviewNode.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SetTextPopup.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;
constexpr float kScrollTop = 236.f;
constexpr float kScrollBottom = 56.f;

constexpr ccColor3B kOk    = {120, 255, 140};
constexpr ccColor3B kWarn  = {255, 190, 90};
constexpr ccColor3B kError = {255, 120, 120};

ccColor3B kindColor(paimon::autobuild::RegionKind kind) {
    using RK = paimon::autobuild::RegionKind;
    switch (kind) {
        case RK::Structure:  return {150, 220, 255};
        case RK::Hazard:     return {255, 130, 130};
        case RK::Decoration: return {130, 240, 170};
        case RK::Background: return {130, 150, 235};
        case RK::Foreground: return {255, 210, 120};
        case RK::Logic:      return {200, 160, 255};
    }
    return {200, 200, 200};
}

} // namespace

namespace paimon::autobuild {

LevelAnalysisPopup* LevelAnalysisPopup::create() {
    auto* ret = new LevelAnalysisPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LevelAnalysisPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    setTitle("Analizar nivel");
    setID("autobuild-analysis-popup"_spr);
    TemplateStore::get().load();
    loadTaxonomyFile();

    m_lastId = Mod::get()->getSavedValue<int>("autobuild-last-level", 0);
    rebuild();
    return true;
}

void LevelAnalysisPopup::onClose(CCObject* sender) {
    AutobuildPopup::refreshOpenPanel();
    Popup::onClose(sender);
}

void LevelAnalysisPopup::scheduleRebuild() {
    Ref<LevelAnalysisPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

void LevelAnalysisPopup::setStatus(std::string text, ccColor3B color) {
    m_status = std::move(text);
    m_statusColor = color;
}

void LevelAnalysisPopup::rebuild() {
    if (m_content) {
        m_content->removeFromParent();
        m_content = nullptr;
    }
    m_actions = nullptr;

    m_content = CCNode::create();
    m_content->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(m_content);

    float const width = kPopupW - 24.f;
    float const inner = kit::cardInnerWidth(width);

    auto* topMenu = CCMenu::create();
    topMenu->setPosition({0.f, 0.f});
    topMenu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    m_content->addChild(topMenu, 5);
    if (auto* helpSpr = SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        helpSpr->setScale(0.62f);
        auto* helpBtn = CCMenuItemExt::createSpriteExtra(
            helpSpr, [this](CCMenuItemSpriteExtra*) { showHelp(); });
        helpBtn->setPosition({kPopupW - 22.f, kPopupH - 22.f});
        topMenu->addChild(helpBtn);
    }

    auto* tabs = kit::makeTabBar(width, {"Nivel", "Piezas", "Paleta"}, m_tab,
                                 [this](int index) {
                                     m_tab = index;
                                     scheduleRebuild();
                                 });
    tabs->setPosition({12.f, kScrollTop + 4.f});
    m_content->addChild(tabs);

    std::vector<CCNode*> items;
    switch (m_tab) {
        case 0:  items = levelTab(width, inner); break;
        case 1:  items = piecesTab(width, inner); break;
        default: items = paletteTab(width, inner); break;
    }

    auto* scroll = kit::makeScrollStack({width, kScrollTop - kScrollBottom}, items);
    scroll->setPosition({12.f, kScrollBottom});
    m_content->addChild(scroll);

    if (!m_status.empty()) {
        auto* label = CCLabelBMFont::create(m_status.c_str(), "chatFont.fnt",
                                            (width - 20.f) / 0.42f, kCCTextAlignmentCenter);
        label->setScale(0.42f);
        label->setColor(m_statusColor);
        label->setAnchorPoint({0.5f, 0.5f});
        label->setPosition({kPopupW / 2.f, kScrollBottom - 12.f});
        m_content->addChild(label);
    }

    buildActions();
}

void LevelAnalysisPopup::addAction(char const* text, char const* sprite, bool enabled,
                                   std::function<void()> onPress) {
    if (!m_actions) return;
    auto* spr = ButtonSprite::create(text, "bigFont.fnt", sprite, 0.7f);
    if (!spr) return;
    spr->setScale(0.62f);
    if (!enabled) spr->setColor({110, 110, 110});

    auto* button = CCMenuItemExt::createSpriteExtra(
        spr, [enabled, cb = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (enabled && cb) cb();
        });
    if (!enabled) button->setOpacity(140);
    m_actions->addChild(button);
}

void LevelAnalysisPopup::buildActions() {
    m_actions = CCMenu::create();
    m_actions->setPosition({kPopupW / 2.f, 26.f});
    m_actions->setContentSize({kPopupW - 40.f, 34.f});
    m_actions->setLayout(RowLayout::create()->setGap(8.f));
    m_content->addChild(m_actions);

    bool const ready = m_data != nullptr && !m_running;
    addAction("Buscar id", "GJ_button_01.png", !m_running, [this] { askLevelId(); });
    addAction("Nivel abierto", "GJ_button_03.png", !m_running, [this] { analyzeOpen(); });
    addAction("Importar todo", "GJ_button_02.png", ready && !m_suggestions.empty(),
              [this] { importAll(); });
    m_actions->updateLayout();
}

std::vector<CCNode*> LevelAnalysisPopup::levelTab(float width, float inner) {
    std::vector<CCNode*> items;

    if (m_running) {
        items.push_back(kit::makeHint(width, "Analizando el nivel..."));
        return items;
    }
    if (!m_data) {
        items.push_back(kit::makeHint(width,
            "Escribe el id de un nivel y Autobuild lo descarga.\n"
            "Separa lo que es estructura de lo que es fondo\n"
            "y te ofrece cada pieza como plantilla."));
        if (m_lastId > 0) {
            items.push_back(kit::makeButtonRow(inner, "Ultimo nivel",
                fmt::format("Volver a analizar el {}.", m_lastId).c_str(), "Analizar",
                [this] { startAnalysis(m_lastId); }));
        }
        return items;
    }

    auto* header = CCNode::create();
    header->setAnchorPoint({0.f, 0.f});
    header->setContentSize({inner, 52.f});
    {
        auto title = m_report.name.empty() ? fmt::format("Nivel {}", m_report.levelId)
                                           : m_report.name;
        auto* name = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 1.f});
        name->limitLabelWidth(inner - 20.f, 0.45f, 0.1f);
        name->setPosition({10.f, 50.f});
        header->addChild(name);

        auto* info = CCLabelBMFont::create(m_report.summary().c_str(), "chatFont.fnt",
                                           (inner - 20.f) / 0.4f, kCCTextAlignmentLeft);
        info->setAnchorPoint({0.f, 1.f});
        info->setScale(0.4f);
        info->setColor(kit::kDescColor);
        info->setPosition({10.f, 32.f});
        header->addChild(info);

        auto const shape = fmt::format("Largo {:.0f} - suelo {:.0f} - {} hitos de gameplay",
                                       m_report.lengthX, m_report.groundY,
                                       m_report.beats.size());
        auto* line = CCLabelBMFont::create(shape.c_str(), "chatFont.fnt");
        line->setAnchorPoint({0.f, 1.f});
        line->setScale(0.4f);
        line->setColor(kit::kValueColor);
        line->setPosition({10.f, 16.f});
        header->addChild(line);
    }
    items.push_back(kit::makeCard(width, "Nivel", {255, 210, 100}, {header}));

    std::vector<CCNode*> breakdown;
    for (int kind = 0; kind < kRegionKinds; ++kind) {
        if (m_report.counts[kind] == 0) continue;
        auto const label = fmt::format("{} zonas - {} objetos", m_report.counts[kind],
                                       m_report.objectsByKind[kind]);
        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({inner, 24.f});

        auto* name = CCLabelBMFont::create(regionKindName(static_cast<RegionKind>(kind)),
                                           "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.4f);
        name->setColor(kindColor(static_cast<RegionKind>(kind)));
        name->setPosition({10.f, 12.f});
        row->addChild(name);

        auto* value = CCLabelBMFont::create(label.c_str(), "chatFont.fnt");
        value->setAnchorPoint({1.f, 0.5f});
        value->setScale(0.4f);
        value->setColor(kit::kDescColor);
        value->setPosition({inner - 10.f, 12.f});
        row->addChild(value);
        breakdown.push_back(row);
    }
    if (m_report.truncated) {
        breakdown.push_back(kit::makeHint(inner,
            "El nivel supera el limite de objetos\ny se analizo solo una parte."));
    }
    items.push_back(kit::makeCard(width, "Que hay dentro", {130, 240, 170}, breakdown));

    return items;
}

CCNode* LevelAnalysisPopup::suggestionRow(float width, int index) {
    auto const& suggestion = m_suggestions[index];
    auto const& region = m_report.regions[suggestion.regions.front()];

    constexpr float kRowH = 62.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    bool const done = index < static_cast<int>(m_imported.size()) && m_imported[index];
    if (auto* panel = SpriteHelper::createColorPanel(width, kRowH,
                                                     done ? ccColor3B{18, 38, 26}
                                                          : kit::kCardColor,
                                                     kit::kCardAlpha, 7.f)) {
        panel->setAnchorPoint({0.f, 0.f});
        row->addChild(panel, -1);
    }

    if (auto* preview = PreviewNode::create({70.f, kRowH - 12.f})) {
        preview->setPosition({8.f, 6.f});
        preview->showRegion(*m_data, region);
        row->addChild(preview);
    }

    auto* name = CCLabelBMFont::create(suggestion.name.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 1.f});
    name->limitLabelWidth(width - 190.f, 0.42f, 0.1f);
    name->setColor(kindColor(suggestion.kind));
    name->setPosition({86.f, kRowH - 8.f});
    row->addChild(name);

    auto const detail = fmt::format("{} objetos - {} - {:.0f}x{:.0f}", suggestion.objects,
                                    suggestion.mode == Mode::Wave ? "onda" : "sello",
                                    region.metrics.width(), region.metrics.height());
    auto* info = CCLabelBMFont::create(detail.c_str(), "chatFont.fnt");
    info->setAnchorPoint({0.f, 1.f});
    info->setScale(0.4f);
    info->setColor(kit::kDescColor);
    info->setPosition({86.f, kRowH - 26.f});
    row->addChild(info);

    auto const scores = fmt::format("estructura {:.2f} / fondo {:.2f} - confianza {:.0f}%",
                                    region.metrics.structureScore,
                                    region.metrics.backgroundScore,
                                    region.confidence * 100.f);
    auto* score = CCLabelBMFont::create(scores.c_str(), "chatFont.fnt");
    score->setAnchorPoint({0.f, 1.f});
    score->setScale(0.36f);
    score->setColor({120, 128, 148});
    score->setPosition({86.f, kRowH - 40.f});
    row->addChild(score);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 5);

    auto* spr = ButtonSprite::create(done ? "Anadida" : "Importar", 76, true, "bigFont.fnt",
                                     done ? "GJ_button_02.png" : "GJ_button_01.png", 18.f, 0.45f);
    auto* button = CCMenuItemExt::createSpriteExtra(
        spr, [this, index](CCMenuItemSpriteExtra*) { importSuggestion(index); });
    button->setPosition({width - 44.f, kRowH / 2.f});
    menu->addChild(button);

    return row;
}

std::vector<CCNode*> LevelAnalysisPopup::piecesTab(float width, float inner) {
    std::vector<CCNode*> items;
    if (!m_data) {
        items.push_back(kit::makeHint(width, "Analiza un nivel primero."));
        return items;
    }
    if (m_suggestions.empty()) {
        items.push_back(kit::makeHint(width,
            "El nivel no dejo ninguna pieza reutilizable. Suele pasar\n"
            "con niveles muy cortos o hechos de una sola figura."));
        return items;
    }

    items.push_back(kit::makeHint(inner,
        "Cada fila es una forma que el nivel repite, o una zona\n"
        "grande que merece plantilla propia. Los triggers no se copian."));
    for (int i = 0; i < static_cast<int>(m_suggestions.size()); ++i) {
        items.push_back(suggestionRow(width, i));
    }
    return items;
}

CCNode* LevelAnalysisPopup::paletteRow(float width, int index) {
    auto const& entry = m_report.palette[index];

    constexpr float kRowH = 30.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    if (auto* swatch = SpriteHelper::createColorPanel(
            22.f, 18.f, {entry.channel.r, entry.channel.g, entry.channel.b}, 255, 3.f)) {
        swatch->setAnchorPoint({0.f, 0.5f});
        swatch->setPosition({8.f, kRowH / 2.f});
        row->addChild(swatch);
    }

    auto const title = entry.channel.id >= 1000
        ? fmt::format("Canal {} (fijo)", entry.channel.id)
        : fmt::format("Canal {}", entry.channel.id);
    auto* name = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->setScale(0.38f);
    name->setPosition({38.f, kRowH / 2.f + 5.f});
    row->addChild(name);

    auto const detail = fmt::format("{} - {} usos{}", regionKindName(entry.role),
                                    entry.uses(), entry.channel.blending ? " - blending" : "");
    auto* info = CCLabelBMFont::create(detail.c_str(), "chatFont.fnt");
    info->setAnchorPoint({0.f, 0.5f});
    info->setScale(0.36f);
    info->setColor(kindColor(entry.role));
    info->setPosition({38.f, kRowH / 2.f - 7.f});
    row->addChild(info);

    auto const rgb = fmt::format("{} {} {}", static_cast<int>(entry.channel.r),
                                 static_cast<int>(entry.channel.g),
                                 static_cast<int>(entry.channel.b));
    auto* value = CCLabelBMFont::create(rgb.c_str(), "chatFont.fnt");
    value->setAnchorPoint({1.f, 0.5f});
    value->setScale(0.36f);
    value->setColor(kit::kDescColor);
    value->setPosition({width - 10.f, kRowH / 2.f});
    row->addChild(value);

    return row;
}

std::vector<CCNode*> LevelAnalysisPopup::paletteTab(float width, float inner) {
    std::vector<CCNode*> items;
    if (!m_data) {
        items.push_back(kit::makeHint(width, "Analiza un nivel primero."));
        return items;
    }
    if (m_report.palette.empty()) {
        items.push_back(kit::makeHint(width, "El nivel no declara canales de color."));
        return items;
    }

    items.push_back(kit::makeHint(inner,
        "El papel de cada canal sale de donde se usa: si casi todo\n"
        "lo que pinta esta en el fondo, es un canal de fondo."));
    std::vector<CCNode*> rows;
    for (int i = 0; i < static_cast<int>(m_report.palette.size()) && i < 40; ++i) {
        rows.push_back(paletteRow(inner, i));
    }
    items.push_back(kit::makeCard(width, "Canales", {255, 165, 210}, rows));
    return items;
}

void LevelAnalysisPopup::askLevelId() {
    auto* popup = SetTextPopup::create(m_lastId > 0 ? std::to_string(m_lastId) : "",
                                       "Id del nivel", 12, "Analizar nivel", "Analizar",
                                       true, 0.f);
    if (!popup) return;
    popup->m_delegate = this;
    kit::showAbove(popup, this);
}

void LevelAnalysisPopup::setTextPopupClosed(SetTextPopup*, gd::string text) {
    std::string digits;
    for (char c : std::string(text)) {
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    }
    if (digits.empty() || digits.size() > 10) {
        setStatus("Ese id no es valido.", kError);
        scheduleRebuild();
        return;
    }
    auto parsed = geode::utils::numFromString<int>(digits);
    if (!parsed.isOk()) {
        setStatus("Ese id no es valido.", kError);
        scheduleRebuild();
        return;
    }
    startAnalysis(parsed.unwrap());
}

void LevelAnalysisPopup::startAnalysis(int levelId) {
    if (m_running) return;
    m_running = true;
    m_lastId = levelId;
    Mod::get()->setSavedValue("autobuild-last-level", levelId);
    setStatus(fmt::format("Descargando el nivel {}...", levelId), kit::kDescColor);
    m_tab = 0;
    rebuild();

    WeakRef<LevelAnalysisPopup> self = this;
    analyzeLevelId(levelId, [self](Result<AnalysisResult> result) {
        auto locked = self.lock();
        if (!locked || !locked->getParent()) return;
        auto* popup = static_cast<LevelAnalysisPopup*>(locked.data());

        popup->m_running = false;
        if (result.isErr()) {
            popup->setStatus(result.unwrapErr(), kError);
            popup->scheduleRebuild();
            return;
        }

        auto value = result.unwrap();
        popup->m_data = std::move(value.data);
        popup->m_report = std::move(value.report);
        popup->m_suggestions = suggestTemplates(*popup->m_data, popup->m_report);
        popup->m_imported.assign(popup->m_suggestions.size(), 0);
        popup->setStatus(fmt::format("{} piezas encontradas.", popup->m_suggestions.size()),
                         popup->m_suggestions.empty() ? kWarn : kOk);
        popup->m_tab = popup->m_suggestions.empty() ? 0 : 1;
        popup->scheduleRebuild();
    });
}

void LevelAnalysisPopup::analyzeOpen() {
    if (m_running) return;
    m_running = true;
    setStatus("Leyendo el nivel abierto...", kit::kDescColor);
    rebuild();

    WeakRef<LevelAnalysisPopup> self = this;
    analyzeOpenLevel([self](Result<AnalysisResult> result) {
        auto locked = self.lock();
        if (!locked || !locked->getParent()) return;
        auto* popup = static_cast<LevelAnalysisPopup*>(locked.data());

        popup->m_running = false;
        if (result.isErr()) {
            popup->setStatus(result.unwrapErr(), kError);
            popup->scheduleRebuild();
            return;
        }

        auto value = result.unwrap();
        popup->m_data = std::move(value.data);
        popup->m_report = std::move(value.report);
        popup->m_suggestions = suggestTemplates(*popup->m_data, popup->m_report);
        popup->m_imported.assign(popup->m_suggestions.size(), 0);
        popup->setStatus(fmt::format("{} piezas encontradas.", popup->m_suggestions.size()),
                         popup->m_suggestions.empty() ? kWarn : kOk);
        popup->m_tab = popup->m_suggestions.empty() ? 0 : 1;
        popup->scheduleRebuild();
    });
}

void LevelAnalysisPopup::importSuggestion(int index) {
    if (!m_data || index < 0 || index >= static_cast<int>(m_suggestions.size())) return;

    auto tpl = templateFrom(*m_data, m_report, m_suggestions[index], 30.f);
    if (!tpl.valid()) {
        setStatus("Esa pieza se quedo sin objetos utilizables.", kWarn);
        scheduleRebuild();
        return;
    }
    if (m_report.levelId > 0) {
        tpl.name = fmt::format("{} - {}", tpl.name, m_report.levelId);
    }

    TemplateStore::get().add(std::move(tpl));
    if (index < static_cast<int>(m_imported.size())) m_imported[index] = 1;
    setStatus(fmt::format("Plantilla anadida: {}", m_suggestions[index].name), kOk);
    scheduleRebuild();
}

void LevelAnalysisPopup::importAll() {
    if (!m_data) return;
    int added = 0;
    for (int i = 0; i < static_cast<int>(m_suggestions.size()); ++i) {
        if (i < static_cast<int>(m_imported.size()) && m_imported[i]) continue;
        auto tpl = templateFrom(*m_data, m_report, m_suggestions[i], 30.f);
        if (!tpl.valid()) continue;
        if (m_report.levelId > 0) tpl.name = fmt::format("{} - {}", tpl.name, m_report.levelId);
        TemplateStore::get().add(std::move(tpl));
        if (i < static_cast<int>(m_imported.size())) m_imported[i] = 1;
        ++added;
    }
    setStatus(fmt::format("{} plantillas anadidas.", added), added > 0 ? kOk : kWarn);
    PaimonNotify::show(fmt::format("Autobuild: {} plantillas", added),
                       added > 0 ? NotificationIcon::Success : NotificationIcon::Warning);
    scheduleRebuild();
}

void LevelAnalysisPopup::showHelp() {
    kit::showAbove(FLAlertLayer::create(
        "Analizar nivel",
        "Autobuild descarga el nivel y lo lee como lo leerias tu: mira la <cy>capa Z</c>, "
        "si el objeto cae en la rejilla de 30, su escala, su giro y que canal de color "
        "lo pinta. Con eso separa <cl>estructura</c>, <co>decoracion</c>, "
        "<cp>fondo</c>, primer plano y <cr>triggers</c>.\n\n"
        "Despues busca las formas que el nivel <cy>repite</c> y te las ofrece como "
        "plantillas listas para construir. Los triggers nunca se copian.\n\n"
        "Si algun objeto queda mal clasificado, escribe una linea "
        "<cp>id tipo</c> en <cp>config/autobuild/objects.txt</c> "
        "(tipos: solid, slope, hazard, portal, pad, orb, collectible, trigger, deco, "
        "text) y vuelve a analizar.",
        "OK"), this);
}

} // namespace paimon::autobuild
