#include "AutobuildPopup.hpp"

#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/Builder.hpp"
#include "../services/Capture.hpp"
#include "../services/TemplateStore.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/SetTextPopup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

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

std::vector<std::string> const kTargetNames = {"Marcadores", "Seleccion", "Area"};
std::vector<std::string> const kModeNames = {"Onda", "Sellos"};

char const* targetHelp(paimon::autobuild::TargetMode target) {
    using TM = paimon::autobuild::TargetMode;
    switch (target) {
        case TM::Markers:
            return "Rellena los bloques marcadores (467, 143, 146) que tengas seleccionados.";
        case TM::Selection:
            return "Usa cada objeto seleccionado como sitio donde construir, sean del tipo que sean.";
        case TM::Area:
            return "Rellena todo el rectangulo que ocupa la seleccion, sin colocar marcadores.";
    }
    return "";
}

int newSeed() {
    static std::mt19937 rng(std::random_device{}());
    return static_cast<int>(std::uniform_int_distribution<int>(1, 999999)(rng));
}

char const* modeHelp(paimon::autobuild::Mode mode) {
    return mode == paimon::autobuild::Mode::Wave
        ? "Aprende una rejilla de piezas y que puede ir al lado de que. Ideal para bloques y relleno."
        : "Guarda cada grupo de objetos entero y suelta uno en cada sitio. Ideal para adornos sueltos.";
}

} // namespace

namespace paimon::autobuild {

AutobuildPopup* AutobuildPopup::create() {
    auto* ret = new AutobuildPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

EditorUI* AutobuildPopup::editor() {
    auto* lel = LevelEditorLayer::get();
    return lel ? lel->m_editorUI : nullptr;
}

bool AutobuildPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    setTitle("Autobuild");
    setID("autobuild-popup"_spr);

    m_options = Options::load();
    m_dimOpacity = this->getOpacity();
    TemplateStore::get().load();

    rebuild();
    return true;
}

void AutobuildPopup::setCompact(bool compact) {
    m_compact = compact;
    if (m_bgSprite) m_bgSprite->setVisible(!compact);
    if (m_title) m_title->setVisible(!compact);
    if (m_closeBtn) m_closeBtn->setVisible(!compact);
    this->setOpacity(compact ? 0 : m_dimOpacity);
    if (compact) m_tab = 1;
    // Deferred: the button that triggered this lives in the node tree we drop.
    scheduleRebuild();
}

void AutobuildPopup::scheduleRebuild() {
    Ref<AutobuildPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

void AutobuildPopup::setStatus(std::string text, ccColor3B color) {
    m_status = std::move(text);
    m_statusColor = color;
}

void AutobuildPopup::rebuild() {
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

    if (m_compact) {
        // Only the build controls stay on screen, on a small strip of their own.
        auto* strip = SpriteHelper::createColorPanel(width, 52.f, {10, 14, 26}, 205, 8.f);
        if (strip) {
            strip->setAnchorPoint({0.f, 0.f});
            strip->setPosition({12.f, 6.f});
            m_content->addChild(strip, -1);
        }
    } else {
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
        auto* foldSpr = ButtonSprite::create("Ver nivel", "bigFont.fnt",
                                             "GJ_button_04.png", 0.7f);
        if (foldSpr) {
            foldSpr->setScale(0.4f);
            auto* foldBtn = CCMenuItemExt::createSpriteExtra(
                foldSpr, [this](CCMenuItemSpriteExtra*) { setCompact(true); });
            foldBtn->setPosition({kPopupW - 80.f, kPopupH - 22.f});
            topMenu->addChild(foldBtn);
        }

        auto* tabs = kit::makeTabBar(width, {"Plantillas", "Construir", "Ajustes"}, m_tab,
                                     [this](int index) {
                                         m_tab = index;
                                         scheduleRebuild();
                                     });
        tabs->setPosition({12.f, kScrollTop + 4.f});
        m_content->addChild(tabs);

        std::vector<CCNode*> items;
        switch (m_tab) {
            case 0:  items = templatesTab(width, inner); break;
            case 1:  items = buildTab(width, inner); break;
            default: items = settingsTab(width, inner); break;
        }

        auto* scroll = kit::makeScrollStack({width, kScrollTop - kScrollBottom}, items);
        scroll->setPosition({12.f, kScrollBottom});
        m_content->addChild(scroll);
    }

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

void AutobuildPopup::addAction(char const* text, char const* sprite, bool enabled,
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

void AutobuildPopup::buildActions() {
    m_actions = CCMenu::create();
    m_actions->setPosition({kPopupW / 2.f, 26.f});
    m_actions->setContentSize({kPopupW - 40.f, 34.f});
    m_actions->setLayout(RowLayout::create()->setGap(8.f));
    m_content->addChild(m_actions);

    auto& store = TemplateStore::get();
    bool const hasSelection = store.selected() != nullptr;

    if (m_compact) {
        addAction("Construir", "GJ_button_01.png", hasSelection, [this] { runBuild(false); });
        addAction("Semilla", "GJ_button_03.png", hasSelection, [this] { runBuild(true); });
        addAction("Deshacer", "GJ_button_06.png", canUndo(editor()), [this] { undoBuild(); });
        addAction("Volver", "GJ_button_04.png", true, [this] { setCompact(false); });
        m_actions->updateLayout();
        return;
    }

    if (m_tab == 0) {
        addAction("Capturar", "GJ_button_01.png", true, [this] { runCapture(false); });
        addAction("Muestra", "GJ_button_03.png", hasSelection, [this] { runCapture(true); });
        addAction("Importar", "GJ_button_04.png", true, [this] { importTemplate(); });
    } else if (m_tab == 1) {
        addAction("Construir", "GJ_button_01.png", hasSelection, [this] { runBuild(false); });
        addAction("Otra semilla", "GJ_button_03.png", hasSelection, [this] { runBuild(true); });
        addAction("Deshacer", "GJ_button_06.png", canUndo(editor()), [this] { undoBuild(); });
    } else {
        addAction("Restablecer", "GJ_button_06.png", true, [this] {
            m_options = Options{};
            m_options.save();
            setStatus("Ajustes restablecidos.", kOk);
            scheduleRebuild();
        });
        addAction("Ayuda", "GJ_button_04.png", true, [this] { showHelp(); });
    }

    m_actions->updateLayout();
}


CCNode* AutobuildPopup::templateRow(float width, int index) {
    auto& store = TemplateStore::get();
    auto const& tpl = store.all()[index];
    bool const selected = index == store.selectedIndex();

    constexpr float kRowH = 46.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto* panel = SpriteHelper::createColorPanel(
        width, kRowH, selected ? ccColor3B{26, 44, 70} : kit::kCardColor,
        selected ? 210 : kit::kCardAlpha, 7.f);
    if (panel) {
        panel->setAnchorPoint({0.f, 0.f});
        row->addChild(panel, -1);
    }
    if (selected) {
        auto* bar = SpriteHelper::createColorPanel(3.f, kRowH - 14.f, kOk, 255, 1.5f);
        if (bar) {
            bar->setAnchorPoint({0.f, 0.f});
            bar->setPosition({6.f, 7.f});
            row->addChild(bar);
        }
    }

    auto* name = CCLabelBMFont::create(tpl.name.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 1.f});
    name->limitLabelWidth(width - 150.f, 0.45f, 0.1f);
    name->setColor(selected ? kOk : kit::kTitleColor);
    name->setPosition({14.f, kRowH - 8.f});
    row->addChild(name);

    auto* info = CCLabelBMFont::create(tpl.summary().c_str(), "chatFont.fnt");
    info->setAnchorPoint({0.f, 1.f});
    info->setScale(0.42f);
    info->setColor(kit::kDescColor);
    info->setPosition({14.f, kRowH - 26.f});
    row->addChild(info);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 5);

    auto* useSpr = ButtonSprite::create(selected ? "En uso" : "Usar", "bigFont.fnt",
                                        selected ? "GJ_button_02.png" : "GJ_button_01.png", 0.6f);
    useSpr->setScale(0.46f);
    auto* useBtn = CCMenuItemExt::createSpriteExtra(useSpr, [this, index](CCMenuItemSpriteExtra*) {
        TemplateStore::get().select(index);
        setStatus("", kit::kDescColor);
        scheduleRebuild();
    });
    useBtn->setPosition({width - 92.f, kRowH / 2.f});
    menu->addChild(useBtn);

    auto* renameSpr = SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (renameSpr) {
        renameSpr->setScale(0.42f);
        auto* renameBtn = CCMenuItemExt::createSpriteExtra(
            renameSpr, [this, index](CCMenuItemSpriteExtra*) { askRename(index); });
        renameBtn->setPosition({width - 50.f, kRowH / 2.f});
        menu->addChild(renameBtn);
    }

    auto* deleteSpr = SpriteHelper::safeCreateWithFrameName("GJ_trashBtn_001.png");
    if (deleteSpr) {
        deleteSpr->setScale(0.42f);
        auto* deleteBtn = CCMenuItemExt::createSpriteExtra(
            deleteSpr, [this, index](CCMenuItemSpriteExtra*) { confirmDelete(index); });
        deleteBtn->setPosition({width - 20.f, kRowH / 2.f});
        menu->addChild(deleteBtn);
    }

    return row;
}

std::vector<CCNode*> AutobuildPopup::templatesTab(float width, float inner) {
    std::vector<CCNode*> items;

    items.push_back(kit::makeCard(width, "Captura", {120, 210, 255}, {
        kit::makeSelectRow(inner, "Modo", modeHelp(m_options.captureMode), kModeNames,
                           m_options.captureMode == Mode::Wave ? 0 : 1,
                           [this](int index) {
                               m_options.captureMode = index == 1 ? Mode::Stamp : Mode::Wave;
                               m_options.save();
                               scheduleRebuild();
                           }),
    }));

    auto& store = TemplateStore::get();
    if (store.all().empty()) {
        items.push_back(kit::makeHint(width,
            "Todavia no tienes plantillas. Selecciona una zona ya decorada y pulsa "
            "Capturar, o importa una libreria .tblib."));
        return items;
    }

    for (int i = 0; i < static_cast<int>(store.all().size()); ++i) {
        items.push_back(templateRow(width, i));
    }
    return items;
}


std::vector<CCNode*> AutobuildPopup::buildTab(float width, float inner) {
    std::vector<CCNode*> items;
    auto& store = TemplateStore::get();
    auto const* tpl = store.selected();

    if (!tpl) {
        items.push_back(kit::makeHint(width,
            "Elige una plantilla en la pestana Plantillas antes de construir."));
        return items;
    }

    auto* summary = CCNode::create();
    summary->setAnchorPoint({0.f, 0.f});
    summary->setContentSize({inner, 50.f});
    {
        auto* name = CCLabelBMFont::create(tpl->name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 1.f});
        name->limitLabelWidth(inner - 20.f, 0.45f, 0.1f);
        name->setPosition({10.f, 48.f});
        summary->addChild(name);

        auto* info = CCLabelBMFont::create(tpl->summary().c_str(), "chatFont.fnt");
        info->setAnchorPoint({0.f, 1.f});
        info->setScale(0.42f);
        info->setColor(kit::kDescColor);
        info->setPosition({10.f, 30.f});
        summary->addChild(info);

        // What the current selection would fill, checked before building.
        std::string preview;
        ccColor3B previewColor = kOk;
        auto plan = planBuild(editor(), m_options, tpl->cell);
        if (plan.isErr()) {
            preview = plan.unwrapErr();
            previewColor = kWarn;
        } else {
            preview = fmt::format("Listo: {} sitios seleccionados.", plan.unwrap().targets.size());
        }
        auto* line = CCLabelBMFont::create(preview.c_str(), "chatFont.fnt",
                                           (inner - 20.f) / 0.4f, kCCTextAlignmentLeft);
        line->setAnchorPoint({0.f, 1.f});
        line->setScale(0.4f);
        line->setColor(previewColor);
        line->setPosition({10.f, 14.f});
        summary->addChild(line);
    }
    items.push_back(kit::makeCard(width, "Plantilla", {255, 210, 100}, {summary}));

    std::vector<CCNode*> destination;
    destination.push_back(kit::makeSelectRow(inner, "Donde", targetHelp(m_options.target),
        kTargetNames, static_cast<int>(m_options.target),
        [this](int index) {
            m_options.target = static_cast<TargetMode>(std::clamp(index, 0, 2));
            m_options.save();
            scheduleRebuild();
        }));

    if (m_options.target != TargetMode::Area) {
        destination.push_back(kit::makeToggleRow(inner, "Capa 2 (bloque 143)", nullptr,
            m_options.layer2Markers,
            [this](bool value) { m_options.layer2Markers = value; m_options.save(); }));
        destination.push_back(kit::makeToggleRow(inner, "Capa 3 (bloque 146)", nullptr,
            m_options.layer3Markers,
            [this](bool value) { m_options.layer3Markers = value; m_options.save(); }));
    }
    destination.push_back(kit::makeToggleRow(inner, "Borrar marcadores",
        "Quita los bloques guia al construir. Deshacer los devuelve.",
        m_options.removeMarkers,
        [this](bool value) { m_options.removeMarkers = value; m_options.save(); }));
    items.push_back(kit::makeCard(width, "Destino", {130, 240, 170}, destination));

    std::vector<CCNode*> tuning;
    tuning.push_back(kit::makeToggleRow(inner, "Copiar colores",
        "Importa solo los canales de color que usan las piezas colocadas.",
        m_options.copyColors,
        [this](bool value) { m_options.copyColors = value; m_options.save(); }));
    if (tpl->mode == Mode::Wave) {
        tuning.push_back(kit::makeToggleRow(inner, "Reglas estrictas",
            "Solo repite vecindades que viste en la muestra. Apagalo si el sitio "
            "es mucho mas grande y quieres que improvise.",
            m_options.strictRules,
            [this](bool value) { m_options.strictRules = value; m_options.save(); }));
        tuning.push_back(kit::makeToggleRow(inner, "Permitir huecos",
            "Deja celdas vacias cuando encajan mejor que forzar una pieza.",
            m_options.allowGaps,
            [this](bool value) { m_options.allowGaps = value; m_options.save(); }));
    } else {
        tuning.push_back(kit::makeToggleRow(inner, "Evitar repetir",
            "No usa la misma pieza dos veces seguidas.", m_options.avoidRepeats,
            [this](bool value) { m_options.avoidRepeats = value; m_options.save(); }));
        tuning.push_back(kit::makeToggleRow(inner, "Sin solapes",
            "Salta un sitio si la pieza chocaria con otra ya colocada.",
            m_options.avoidOverlap,
            [this](bool value) { m_options.avoidOverlap = value; m_options.save(); }));
    }
    tuning.push_back(kit::makeToggleRow(inner, "Semilla fija",
        "Con semilla fija el resultado se repite igual cada vez.",
        m_options.seed != 0,
        [this](bool value) {
            m_options.seed = value ? newSeed() : 0;
            m_options.save();
            scheduleRebuild();
        }));
    if (m_options.seed != 0) {
        tuning.push_back(kit::makeButtonRow(inner, "Semilla",
            fmt::format("Valor actual: {}", m_options.seed).c_str(), "Cambiar",
            [this] {
                m_options.seed = newSeed();
                m_options.save();
                scheduleRebuild();
            }));
    }
    items.push_back(kit::makeCard(width, "Resultado", {170, 190, 255}, tuning));

    return items;
}


CCNode* AutobuildPopup::stepperRow(float width, char const* title, char const* desc,
                                   int value, std::function<void(int)> onChange) {
    constexpr float kRowH = 34.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
    label->setAnchorPoint({0.f, 1.f});
    label->limitLabelWidth(width - 170.f, 0.4f, 0.1f);
    label->setPosition({10.f, kRowH - 6.f});
    row->addChild(label);

    if (desc && desc[0] != '\0') {
        auto* hint = CCLabelBMFont::create(desc, "chatFont.fnt");
        hint->setAnchorPoint({0.f, 1.f});
        hint->setScale(0.4f);
        hint->setColor(kit::kDescColor);
        hint->setPosition({10.f, kRowH - 20.f});
        row->addChild(hint);
    }

    auto* valueLabel = CCLabelBMFont::create(std::to_string(value).c_str(), "bigFont.fnt");
    valueLabel->setScale(0.42f);
    valueLabel->setColor(kit::kValueColor);
    valueLabel->setPosition({width - 82.f, kRowH / 2.f});
    row->addChild(valueLabel);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 5);

    auto current = std::make_shared<int>(value);
    auto step = [current, valueLabel, onChange](int delta) {
        *current = std::clamp(*current + delta, -9999, 9999);
        valueLabel->setString(std::to_string(*current).c_str());
        if (onChange) onChange(*current);
    };

    struct StepButton {
        char const* text;
        int delta;
        float x;
    };
    StepButton const buttons[] = {
        {"-10", -10, width - 148.f},
        {"-1",   -1, width - 116.f},
        {"+1",    1, width - 48.f},
        {"+10",  10, width - 16.f},
    };
    for (auto const& info : buttons) {
        auto* spr = ButtonSprite::create(info.text, "bigFont.fnt", "GJ_button_04.png", 0.7f);
        if (!spr) continue;
        spr->setScale(0.36f);
        auto* button = CCMenuItemExt::createSpriteExtra(
            spr, [step, delta = info.delta](CCMenuItemSpriteExtra*) { step(delta); });
        button->setPosition({info.x, kRowH / 2.f});
        menu->addChild(button);
    }

    return row;
}

std::vector<CCNode*> AutobuildPopup::settingsTab(float width, float inner) {
    std::vector<CCNode*> items;

    items.push_back(kit::makeCard(width, "Captura", {120, 210, 255}, {
        kit::makeSliderRow(inner, "Tamano de celda",
            "Rejilla que usa el modo Onda al aprender y al construir.",
            m_options.captureCell, 10.0, 120.0,
            [](double v) { return fmt::format("{:.0f}", v); },
            [this](double v) {
                m_options.captureCell = static_cast<float>(std::round(v));
                m_options.save();
            }),
        kit::makeSliderRow(inner, "Radio de grupo",
            "Distancia maxima entre objetos del mismo sello.",
            m_options.clusterRadius, 30.0, 240.0,
            [](double v) { return fmt::format("{:.0f}", v); },
            [this](double v) {
                m_options.clusterRadius = static_cast<float>(std::round(v));
                m_options.save();
            }),
    }));

    items.push_back(kit::makeCard(width, "Desplazar IDs", {255, 165, 210}, {
        stepperRow(inner, "Colores", "Suma este numero a los canales 1-999.",
                   m_options.shiftColors,
                   [this](int v) { m_options.shiftColors = v; m_options.save(); }),
        stepperRow(inner, "Grupos", nullptr, m_options.shiftGroups,
                   [this](int v) { m_options.shiftGroups = v; m_options.save(); }),
        stepperRow(inner, "Capas del editor", nullptr, m_options.shiftLayers,
                   [this](int v) { m_options.shiftLayers = v; m_options.save(); }),
        stepperRow(inner, "Orden Z", nullptr, m_options.shiftZOrder,
                   [this](int v) { m_options.shiftZOrder = v; m_options.save(); }),
        stepperRow(inner, "Grupo extra",
                   "Anade este grupo a todo lo generado. 0 para no anadir ninguno.",
                   m_options.addGroup,
                   [this](int v) {
                       m_options.addGroup = std::clamp(v, 0, 9999);
                       m_options.save();
                   }),
    }));

    items.push_back(kit::makeCard(width, "Archivos", {170, 190, 255}, {
        kit::makeButtonRow(inner, "Carpeta de plantillas",
            "Aqui viven los .pab. Copialos para compartirlos.", "Abrir",
            [this] {
                if (!geode::utils::file::openFolder(TemplateStore::directory())) {
                    setStatus("No se pudo abrir la carpeta.", kWarn);
                    scheduleRebuild();
                }
            }),
        kit::makeButtonRow(inner, "Recargar",
            "Vuelve a leer la carpeta por si copiaste plantillas a mano.", "Recargar",
            [this] {
                TemplateStore::get().reload();
                setStatus(fmt::format("{} plantillas en la carpeta.",
                                      TemplateStore::get().all().size()), kOk);
                scheduleRebuild();
            }),
    }));

    items.push_back(kit::makeCard(width, "Limites", {255, 195, 95}, {
        kit::makeSliderRow(inner, "Maximo de objetos",
            "Corta la construccion al llegar a este numero.",
            m_options.maxObjects, 1000.0, 100000.0,
            [](double v) { return fmt::format("{:.0f}", v); },
            [this](double v) {
                m_options.maxObjects = static_cast<int>(std::round(v));
                m_options.save();
            }),
        kit::makeSliderRow(inner, "Reintentos",
            "Cuanto insiste la onda antes de forzar una pieza.",
            m_options.backtracks, 0.0, 2000.0,
            [](double v) { return fmt::format("{:.0f}", v); },
            [this](double v) {
                m_options.backtracks = static_cast<int>(std::round(v));
                m_options.save();
            }),
    }));

    return items;
}


void AutobuildPopup::runCapture(bool asSample) {
    auto* ui = editor();
    if (!ui) {
        setStatus("El editor no esta disponible.", kError);
        scheduleRebuild();
        return;
    }

    auto& store = TemplateStore::get();
    Mode mode = m_options.captureMode;
    if (asSample && store.selected()) mode = store.selected()->mode;

    auto result = capture(ui, mode, m_options);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), kError);
        scheduleRebuild();
        return;
    }

    auto sample = result.unwrap();
    if (asSample) {
        auto target = *store.selected();
        auto merged = accumulate(target, sample);
        if (merged.isErr()) {
            setStatus(merged.unwrapErr(), kError);
            scheduleRebuild();
            return;
        }
        store.replace(store.selectedIndex(), target);
        setStatus(fmt::format("Muestra anadida: {}", store.selected()->summary()), kOk);
    } else {
        sample.name = fmt::format("Plantilla {}", store.all().size() + 1);
        int index = store.add(std::move(sample));
        setStatus(fmt::format("Capturada: {}", store.all()[index].summary()), kOk);
        m_tab = 0;
    }
    scheduleRebuild();
}

void AutobuildPopup::importTemplate() {
    WeakRef<AutobuildPopup> self = this;
    pt::pickBuildTemplate([self](Result<std::optional<std::filesystem::path>> result) {
        auto locked = self.lock();
        if (!locked || !locked->getParent()) return;
        auto* popup = static_cast<AutobuildPopup*>(locked.data());

        auto path = std::move(result).unwrapOr(std::nullopt);
        if (!path || path->empty()) return;

        auto imported = TemplateStore::get().importFile(*path);
        if (imported.isErr()) {
            popup->setStatus(imported.unwrapErr(), kError);
        } else {
            auto const& tpl = TemplateStore::get().all()[imported.unwrap()];
            popup->setStatus(fmt::format("Importada: {} - {}", tpl.name, tpl.summary()), kOk);
        }
        popup->m_tab = 0;
        popup->scheduleRebuild();
    });
}

void AutobuildPopup::runBuild(bool reroll) {
    auto* ui = editor();
    auto const* tpl = TemplateStore::get().selected();
    if (!ui || !tpl) {
        setStatus("Elige una plantilla primero.", kError);
        scheduleRebuild();
        return;
    }

    auto result = reroll ? regenerate(ui, *tpl, m_options) : generate(ui, *tpl, m_options);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), kError);
        PaimonNotify::show("Autobuild: nada que construir", NotificationIcon::Warning);
        scheduleRebuild();
        return;
    }

    auto report = result.unwrap();
    setStatus(report.describe(), report.forced > 0 ? kWarn : kOk);
    PaimonNotify::show(fmt::format("Autobuild: {} objetos", report.objects),
                       NotificationIcon::Success);
    // Get out of the way so the result is visible right after building.
    if (!m_compact) setCompact(true);
    else scheduleRebuild();
}

void AutobuildPopup::undoBuild() {
    auto* ui = editor();
    if (!ui) return;
    auto result = undoLast(ui);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), kWarn);
    } else {
        setStatus("Construccion deshecha.", kOk);
    }
    scheduleRebuild();
}

void AutobuildPopup::askRename(int index) {
    auto& store = TemplateStore::get();
    if (index < 0 || index >= static_cast<int>(store.all().size())) return;
    m_renameIndex = index;

    auto* popup = SetTextPopup::create(store.all()[index].name, "Nombre", 30, "Renombrar",
                                       "Guardar", true, 220.f);
    if (!popup) return;
    popup->m_delegate = this;
    popup->show();
}

void AutobuildPopup::setTextPopupClosed(SetTextPopup*, gd::string text) {
    int index = m_renameIndex;
    m_renameIndex = -1;
    std::string name(text);
    if (name.empty()) return;
    TemplateStore::get().rename(index, name);
    setStatus(fmt::format("Renombrada a {}", name), kOk);
    scheduleRebuild();
}

void AutobuildPopup::confirmDelete(int index) {
    auto& store = TemplateStore::get();
    if (index < 0 || index >= static_cast<int>(store.all().size())) return;

    Ref<AutobuildPopup> self = this;
    createQuickPopup(
        "Borrar plantilla",
        fmt::format("Se borrara <cy>{}</c> del disco. Esto no se puede deshacer.",
                    store.all()[index].name),
        "Cancelar", "Borrar",
        [self, index](FLAlertLayer*, bool confirmed) {
            if (!confirmed || !self || !self->getParent()) return;
            TemplateStore::get().remove(index);
            self->setStatus("Plantilla borrada.", kWarn);
            self->scheduleRebuild();
        });
}

void AutobuildPopup::showHelp() {
    FLAlertLayer::create(
        "Autobuild",
        "<cg>1.</c> Decora una zona pequena, seleccionala y pulsa <cy>Capturar</c>.\n"
        "<cg>2.</c> Marca donde quieres construir: bloques <co>467 / 143 / 146</c>, "
        "la propia seleccion o un area.\n"
        "<cg>3.</c> Vuelve a <cy>Construir</c> y pulsa Construir.\n\n"
        "<cl>Onda</c> aprende que pieza puede ir al lado de cual y rellena rejillas "
        "coherentes. <cl>Sellos</c> guarda grupos enteros y suelta uno en cada sitio.\n\n"
        "<cy>Otra semilla</c> rehace el mismo sitio con otro resultado y "
        "<cr>Deshacer</c> borra lo generado y devuelve los marcadores.\n\n"
        "Las plantillas se guardan en <cp>config/autobuild</c> y puedes importar "
        "librerias <cp>.tblib</c> de otros autobuilders.",
        "OK")->show();
}

} // namespace paimon::autobuild
