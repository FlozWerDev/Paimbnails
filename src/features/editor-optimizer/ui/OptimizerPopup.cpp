#include "OptimizerPopup.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../smooth-scroll/services/SmoothScrollController.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::optimizer {

namespace kit = paimon::editor::kit;

namespace {

constexpr float kPopupWidth = 520.f;
constexpr float kPopupHeight = 315.f;

constexpr float kPreviewX = 14.f;
constexpr float kPreviewY = 136.f;
constexpr float kPreviewWidth = 222.f;
constexpr float kPreviewHeight = 132.f;
constexpr float kPreviewInset = 3.f;
constexpr float kHeaderBarHeight = 16.f;

constexpr float kRailX = 240.f;
constexpr float kRailWidth = 30.f;
constexpr float kToolSize = 19.f;
constexpr float kToolPitch = 23.f;

constexpr float kLeftWidth = 256.f;
constexpr float kStatsY = 66.f;
constexpr float kStatsHeight = 64.f;

constexpr float kPanelX = 280.f;
constexpr float kPanelWidth = 226.f;
constexpr float kRowHeight = 20.f;

constexpr float kActionY = 46.f;
constexpr float kActionHeight = 24.f;

constexpr float kStatusX = 30.f;
constexpr float kStatusY = 10.f;
constexpr float kStatusWidth = 460.f;
constexpr float kStatusHeight = 19.f;

constexpr float kZoomStep = 1.35f;
constexpr float kScrollZoomIn = 1.14f;
constexpr float kScrollZoomOut = 0.88f;
constexpr float kMinZoom = 0.25f;
constexpr float kMaxZoom = 12.f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

bool spanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

char const* tr(char const* en, char const* es) {
    return spanish() ? es : en;
}

void drawBox(
    CCDrawNode* draw,
    float cx,
    float cy,
    float w,
    float h,
    float rotDeg,
    ccColor4F const& fill,
    ccColor4F const& border,
    float borderWidth = 0.7f
) {
    float const rad = -rotDeg * kDegToRad;
    float const c = std::cos(rad);
    float const s = std::sin(rad);
    float const hw = w * 0.5f;
    float const hh = h * 0.5f;
    auto rotPt = [&](float lx, float ly) -> CCPoint {
        return {cx + lx * c - ly * s, cy + lx * s + ly * c};
    };
    std::array<CCPoint, 4> corners{
        rotPt(-hw, -hh),
        rotPt(hw, -hh),
        rotPt(hw, hh),
        rotPt(-hw, hh),
    };
    draw->drawPolygon(corners.data(), 4, fill, borderWidth, borderWidth > 0.f ? border : fill);
}

} // namespace

OptimizerPopup* OptimizerPopup::create() {
    if (!paimon::modules::isEnabled("paimbnails.optimizer.editor")) return nullptr;
    auto* popup = new OptimizerPopup();
    if (popup && popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

bool OptimizerPopup::init() {
    if (!PaimonPopup::init(kPopupWidth, kPopupHeight)) return false;
    setID("structure-optimizer-popup"_spr);
    setTitle(tr("Structure Optimizer", "Optimizador de Estructuras"));
    addInfoButton(
        tr("Structure Optimizer", "Optimizador de Estructuras"),
        tr(
            "Combines aligned identical blocks into fewer stretched objects.\n"
            "- <cg>Collision Limit</c>: keeps solid objects within safe GD scale bounds so hitboxes and visuals never glitch.\n"
            "- <cy>Preserve Corners</c>: leaves corner pieces and 90-degree L-corners untouched so they never look oversized.\n"
            "- <cb>2D Fills</c>: merges uniform borderless fill blocks into 2D rectangles.\n"
            "Supports <cg>Ctrl+Z</c> undo.",
            "Combina bloques identicos alineados en menos objetos alargados.\n"
            "- <cg>Limite Colision</c>: mantiene los objetos solidos dentro de un largo seguro para evitar bugs visuales o de hitbox en GD.\n"
            "- <cy>Preservar Esquinas</c>: deja intactos los bloques de esquina y esquinas L para que no se agranden ni deformen.\n"
            "- <cb>Rellenos 2D</c>: fusiona bloques planos sin bordes en rectangulos 2D.\n"
            "Soporta deshacer con <cg>Ctrl+Z</c>."
        )
    );
    addCorners(SideArtStyle::PopupGold);

    m_options = loadOptimizerOptions();
    if (auto* ui = editorUI()) {
        auto* sel = ui->getSelectedObjects();
        m_selectedOnly = (sel && sel->count() > 0) || ui->m_selectedObject != nullptr;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    buildPreviewPanel(menu);
    buildStatsPanel();
    buildOptionsPanel(menu);

    WeakRef<OptimizerPopup> self = this;

    auto analyzeBtn = kit::pill(
        "paim_ui_undo.png", "GJ_updateBtn_001.png",
        tr("Analyze", "Analizar"), "GJ_button_04.png",
        {108.f, kActionHeight},
        [self] {
            if (auto p = self.lock()) p->analyze(true);
        }
    );
    analyzeBtn.button->setPosition({72.f, kActionY});
    menu->addChild(analyzeBtn.button);

    m_viewPill = kit::pill(
        "paim_phys_hitbox.png", "GJ_infoIcon_001.png",
        tr("View: After", "Ver: Despues"), "GJ_button_02.png",
        {126.f, kActionHeight},
        [self] {
            if (auto p = self.lock()) p->toggleViewMode();
        }
    );
    m_viewPill.button->setPosition({196.f, kActionY});
    menu->addChild(m_viewPill.button);

    m_applyPill = kit::pill(
        "paim_ui_hammer.png", "GJ_hammerIcon_001.png",
        tr("Optimize Structure", "Optimizar Estructura"), "GJ_button_01.png",
        {214.f, kActionHeight},
        [self] {
            if (auto p = self.lock()) p->applyOptimization();
        }
    );
    m_applyPill.button->setPosition({396.f, kActionY});
    menu->addChild(m_applyPill.button);

    m_mainLayer->addChild(kit::inset({kStatusX, kStatusY, kStatusWidth, kStatusHeight}, 90));
    m_statusIcon = kit::glyph("paim_ui_badge.png", "GJ_infoIcon_001.png", 10.f);
    m_statusIcon->setPosition({kStatusX + 11.f, kStatusY + kStatusHeight * 0.5f});
    m_mainLayer->addChild(m_statusIcon, 1);
    m_statusLabel = kit::label("", "bigFont.fnt", 0.3f);
    m_statusLabel->setAlignment(kCCTextAlignmentCenter);
    m_statusLabel->setPosition({
        kStatusX + 20.f + (kStatusWidth - 24.f) * 0.5f,
        kStatusY + kStatusHeight * 0.5f + 0.5f,
    });
    m_mainLayer->addChild(m_statusLabel, 1);

    setMouseEnabled(true);
    refreshOptionsUI();
    analyze(true);
    return true;
}

void OptimizerPopup::buildPreviewPanel(CCMenu* menu) {
    WeakRef<OptimizerPopup> self = this;
    m_mainLayer->addChild(kit::inset({kPreviewX, kPreviewY, kPreviewWidth, kPreviewHeight}, 220));

    float const clipW = kPreviewWidth - kPreviewInset * 2.f;
    float const clipH = kPreviewHeight - kPreviewInset * 2.f;
    auto* clip = CCClippingNode::create();
    clip->setPosition({kPreviewX + kPreviewInset, kPreviewY + kPreviewInset});
    clip->setContentSize({clipW, clipH});
    m_mainLayer->addChild(clip, 2);

    auto* stencil = CCLayerColor::create({255, 255, 255, 255}, clipW, clipH);
    clip->setStencil(stencil);

    auto* bg = CCLayerColor::create({18, 22, 34, 255}, clipW, clipH);
    clip->addChild(bg, 0);

    m_previewWorld = CCNode::create();
    clip->addChild(m_previewWorld, 2);

    m_drawNode = CCDrawNode::create();
    m_previewWorld->addChild(m_drawNode, 1);

    auto* headerBar = CCLayerColor::create({0, 0, 0, 125}, clipW, kHeaderBarHeight);
    headerBar->setPosition({0.f, clipH - kHeaderBarHeight});
    clip->addChild(headerBar, 4);

    m_viewLabel = kit::label("", "goldFont.fnt", 0.34f);
    m_viewLabel->setPosition({
        kPreviewX + kPreviewWidth * 0.5f,
        kPreviewY + kPreviewHeight - kPreviewInset - kHeaderBarHeight * 0.5f + 1.2f,
    });
    m_mainLayer->addChild(m_viewLabel, 6);

    m_mainLayer->addChild(kit::inset({kRailX, kPreviewY, kRailWidth, kPreviewHeight}, 90));
    std::array<CCMenuItem*, 4> tools{};
    tools[0] = kit::toolButton("paim_ui_zoomIn.png", "GJ_zoomInBtn_001.png",
        EditorBaseColor::LightBlue, kToolSize, [self] {
            if (auto p = self.lock()) p->adjustZoom(kZoomStep);
        });
    tools[1] = kit::toolButton("paim_ui_zoomOut.png", "GJ_zoomOutBtn_001.png",
        EditorBaseColor::LightBlue, kToolSize, [self] {
            if (auto p = self.lock()) p->adjustZoom(1.f / kZoomStep);
        });
    tools[2] = kit::toolButton("paim_ui_fit.png", "GJ_resetBtn_001.png",
        EditorBaseColor::LightBlue, kToolSize, [self] {
            if (auto p = self.lock()) p->fitCameraToScene();
        });
    m_viewToggle = kit::toolToggle("paim_phys_hitbox.png", "GJ_checkOn_001.png",
        EditorBaseColor::Cyan, kToolSize, m_showAfter, [self](bool) {
            if (auto p = self.lock()) p->toggleViewMode(false);
        });
    tools[3] = m_viewToggle;

    for (std::size_t i = 0; i < tools.size(); ++i) {
        if (!tools[i]) continue;
        tools[i]->setPosition({
            kRailX + kRailWidth * 0.5f,
            kPreviewY + kPreviewHeight - 13.f - static_cast<float>(i) * kToolPitch,
        });
        menu->addChild(tools[i]);
    }
}

void OptimizerPopup::buildStatsPanel() {
    m_mainLayer->addChild(kit::inset({kPreviewX, kStatsY, kLeftWidth, kStatsHeight}, 75));
    std::array<ccColor3B, 3> const colors{
        kit::tint::green,
        kit::tint::label,
        kit::tint::gold,
    };
    for (std::size_t i = 0; i < 3; ++i) {
        float const y = kStatsY + kStatsHeight - 13.f - static_cast<float>(i) * 19.f;
        auto* lbl = kit::label("-", "bigFont.fnt", 0.31f, colors[i]);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({kPreviewX + 8.f, y});
        m_mainLayer->addChild(lbl, 2);
        m_statLabels[i] = lbl;
    }
}

void OptimizerPopup::buildOptionsPanel(CCMenu* menu) {
    WeakRef<OptimizerPopup> self = this;
    float const top = kPreviewY + kPreviewHeight;
    m_mainLayer->addChild(kit::inset({kPanelX, kStatsY, kPanelWidth, top - kStatsY}, 75));

    auto* heading = kit::header(
        "paim_gif_optimize.png", "GJ_optionsBtn_001.png",
        tr("Optimization Rules", "Reglas de Optimizacion"), kPanelWidth - 8.f
    );
    heading->setPosition({kPanelX + 4.f, top - 22.f});
    m_mainLayer->addChild(heading);

    m_scopePill = kit::pill(
        "paim_ui_badge.png", "GJ_infoIcon_001.png",
        tr("Scope: Selection", "Alcance: Seleccion"), "GJ_button_05.png",
        {kPanelWidth - 12.f, 20.f},
        [self] {
            if (auto p = self.lock()) p->toggleScope();
        },
        "bigFont.fnt"
    );
    m_scopePill.button->setPosition({kPanelX + kPanelWidth * 0.5f, top - 36.f});
    menu->addChild(m_scopePill.button);

    std::array<kit::StepperSpec, 2> const steppers{{
        {"paim_phys_hitbox.png", "GJ_plus2Btn_001.png",
            tr("Max Collision Len", "Largo Max Colision"), kit::tint::cyan},
        {"paim_gif_modeBlocks.png", "GJ_plus2Btn_001.png",
            tr("Max Deco Len", "Largo Max Deco"), kit::tint::violet},
    }};
    float const stepperTop = top - 52.f - kRowHeight;
    for (std::size_t i = 0; i < steppers.size(); ++i) {
        CCRect const row{
            kPanelX + 4.f,
            stepperTop - static_cast<float>(i) * kRowHeight,
            kPanelWidth - 8.f,
            kRowHeight,
        };
        int const field = static_cast<int>(i);
        m_stepperLabels[i] = kit::stepper(m_mainLayer, menu, steppers[i], row, i == 0,
            [self, field](int dir) {
                if (auto p = self.lock()) p->adjustStepper(field, dir);
            });
    }

    auto makeToggleRow = [&](float y, bool stripe, char const* title, bool initial,
                             std::function<void(bool)> onChange) {
        if (stripe) {
            auto* band = CCLayerColor::create({0, 0, 0, 28}, kPanelWidth - 8.f, 22.f);
            band->setPosition({kPanelX + 4.f, y - 11.f});
            m_mainLayer->addChild(band);
        }
        auto* lbl = kit::label(title, "bigFont.fnt", 0.3f, kit::tint::label);
        lbl->limitLabelWidth(kPanelWidth - 44.f, 0.3f, 0.18f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({kPanelX + 10.f, y + 0.5f});
        m_mainLayer->addChild(lbl);

        auto* toggle = kit::toolToggle(
            "paim_ui_badge.png", "GJ_checkOn_001.png",
            EditorBaseColor::Green, 17.f, initial, std::move(onChange)
        );
        if (toggle) {
            toggle->setPosition({kPanelX + kPanelWidth - 18.f, y});
            menu->addChild(toggle);
        }
    };

    makeToggleRow(
        top - 108.f, true,
        tr("Preserve Corner Blocks", "Preservar Esquinas"),
        m_options.preserveCorners,
        [self](bool val) {
            auto p = self.lock();
            if (!p) return;
            p->m_options.preserveCorners = val;
            saveOptimizerOptions(p->m_options);
            p->recomputePlan();
        }
    );

    makeToggleRow(
        top - 131.f, false,
        tr("Merge 2D Flat Fills", "Fusionar Rellenos 2D"),
        m_options.merge2DFills,
        [self](bool val) {
            auto p = self.lock();
            if (!p) return;
            p->m_options.merge2DFills = val;
            saveOptimizerOptions(p->m_options);
            p->recomputePlan();
        }
    );

    // Compact visual legend at the bottom of the right panel.
    struct Swatch {
        ccColor3B color;
        char const* label;
    };
    std::array<Swatch, 3> const swatches{{
        {{75, 235, 175}, tr("Merged", "Fusion")},
        {{255, 200, 75}, tr("Corner", "Esquina")},
        {{140, 155, 190}, tr("Intact", "Intacto")},
    }};
    float const swatchY = kStatsY + 14.f;
    float const swatchW = (kPanelWidth - 16.f) / 3.f;
    for (std::size_t i = 0; i < swatches.size(); ++i) {
        float const sx = kPanelX + 8.f + static_cast<float>(i) * swatchW;
        auto* dot = CCLayerColor::create(
            {swatches[i].color.r, swatches[i].color.g, swatches[i].color.b, 220},
            8.f, 8.f
        );
        dot->setPosition({sx, swatchY - 4.f});
        m_mainLayer->addChild(dot, 2);

        auto* text = kit::label(swatches[i].label, "bigFont.fnt", 0.25f, kit::tint::muted);
        text->limitLabelWidth(swatchW - 14.f, 0.25f, 0.14f);
        text->setAnchorPoint({0.f, 0.5f});
        text->setPosition({sx + 11.f, swatchY});
        m_mainLayer->addChild(text, 2);
    }
}

void OptimizerPopup::onClose(CCObject* sender) {
    saveOptimizerOptions(m_options);
    Popup::onClose(sender);
}

LevelEditorLayer* OptimizerPopup::editorLayer() const {
    return LevelEditorLayer::get();
}

EditorUI* OptimizerPopup::editorUI() const {
    auto* editor = editorLayer();
    return editor ? editor->m_editorUI : nullptr;
}

void OptimizerPopup::analyze(bool refitCamera) {
    auto* editor = editorLayer();
    auto* ui = editorUI();
    if (!editor || !ui) {
        setStatus(tr("Editor unavailable.", "El editor no esta disponible."), {255, 120, 120});
        return;
    }

    m_scene = captureEditorScene(editor, ui, m_selectedOnly);
    m_selectedOnly = m_scene.usedSelection;
    recomputePlan();
    if (refitCamera) fitCameraToScene();
}

void OptimizerPopup::recomputePlan() {
    m_plan = planStructureOptimization(m_scene.records, m_options);
    refreshOptionsUI();
    refreshStats();
    rebuildPreview();

    bool const hasMerges = !m_plan.merges.empty();
    if (m_applyPill.button) {
        m_applyPill.button->setEnabled(hasMerges);
        GLubyte const alpha = hasMerges ? 255 : 135;
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(m_applyPill.background)) rgba->setOpacity(alpha);
        if (m_applyPill.icon) m_applyPill.icon->setOpacity(alpha);
        if (m_applyPill.label) m_applyPill.label->setOpacity(alpha);
    }

    if (m_plan.inputCount == 0) {
        setStatus(
            tr("No objects in scope. Select a structure or switch to Full Level.",
               "No hay objetos para analizar. Selecciona una estructura o usa Todo el nivel."),
            {255, 205, 95}
        );
    } else if (m_plan.merges.empty()) {
        setStatus(
            tr("No aligned blocks to merge with the current rules.",
               "No se encontraron bloques alineados para fusionar con las reglas actuales."),
            {255, 205, 95}
        );
    } else {
        std::size_t const saved = m_plan.inputCount - m_plan.outputCount;
        float const pct = 100.f * static_cast<float>(saved) / static_cast<float>(m_plan.inputCount);
        setStatus(
            fmt::format(
                fmt::runtime(tr("Ready: {} objects can be reduced to {} (-{}, -{:.1f}%).",
                   "Listo: {} objetos se reducen a {} (-{}, -{:.1f}%).")),
                m_plan.inputCount, m_plan.outputCount, saved, pct
            ),
            {150, 240, 180}
        );
    }
}

void OptimizerPopup::applyOptimization() {
    if (m_plan.merges.empty()) {
        setStatus(
            tr("Nothing to optimize yet.", "No hay nada que optimizar todavia."),
            {255, 205, 95}
        );
        return;
    }
    auto* editor = editorLayer();
    auto* ui = editorUI();
    auto result = applyOptimizationPlan(editor, ui, m_scene, m_plan);
    if (result.isErr()) {
        setStatus(result.unwrapErr(), {255, 120, 120});
        return;
    }

    auto const report = result.unwrap();
    PaimonNotify::show(
        fmt::format(
            fmt::runtime(tr("Optimized: {} -> {} objects (-{}).",
               "Optimizado: {} -> {} objetos (-{}).")),
            report.removedObjects, report.createdObjects, report.netSaved
        ),
        NotificationIcon::Success
    );
    analyze(false);
    setStatus(
        fmt::format(
            fmt::runtime(tr("Applied! Replaced {} blocks with {} stretched objects (Ctrl+Z to undo).",
               "Aplicado! Se reemplazaron {} bloques por {} objetos (Ctrl+Z para deshacer).")),
            report.removedObjects, report.createdObjects
        ),
        {135, 255, 150}
    );
}

void OptimizerPopup::toggleScope() {
    auto* ui = editorUI();
    bool const hasSelection = ui && ((ui->getSelectedObjects() && ui->getSelectedObjects()->count() > 0) ||
                                     ui->m_selectedObject != nullptr);
    if (!m_selectedOnly && !hasSelection) {
        setStatus(
            tr("Nothing selected in the editor; staying on Full Level.",
               "No hay seleccion en el editor; se mantiene Todo el nivel."),
            {255, 205, 95}
        );
        return;
    }
    m_selectedOnly = !m_selectedOnly;
    analyze(true);
}

void OptimizerPopup::toggleViewMode(bool syncToggle) {
    m_showAfter = !m_showAfter;
    refreshOptionsUI(syncToggle);
    rebuildPreview();
}

void OptimizerPopup::adjustStepper(int field, int direction) {
    if (field == 0) {
        m_options.maxCollisionScale = std::clamp(
            m_options.maxCollisionScale + static_cast<float>(direction), 2.f, 8.f
        );
    } else if (field == 1) {
        m_options.maxDecoScale = std::clamp(
            m_options.maxDecoScale + static_cast<float>(direction), 2.f, 16.f
        );
    } else {
        return;
    }
    saveOptimizerOptions(m_options);
    recomputePlan();
}

void OptimizerPopup::refreshOptionsUI(bool syncViewToggle) {
    kit::setValue(m_stepperLabels[0], fmt::format("{}x", static_cast<int>(std::lround(m_options.maxCollisionScale))));
    kit::setValue(m_stepperLabels[1], fmt::format("{}x", static_cast<int>(std::lround(m_options.maxDecoScale))));

    kit::setPillText(
        m_scopePill,
        m_selectedOnly
            ? fmt::format(fmt::runtime(tr("Scope: Selection ({})", "Alcance: Seleccion ({})")), m_scene.selectionCount)
            : fmt::format(fmt::runtime(tr("Scope: Full Level ({})", "Alcance: Nivel ({})")), m_scene.levelCount)
    );
    kit::setPillText(
        m_viewPill,
        m_showAfter ? tr("View: After", "Ver: Despues") : tr("View: Before", "Ver: Antes")
    );
    if (syncViewToggle && m_viewToggle) m_viewToggle->toggle(m_showAfter);
    updatePreviewHeader();
}

void OptimizerPopup::refreshStats() {
    if (!m_statLabels[0]) return;
    if (m_plan.inputCount == 0) {
        m_statLabels[0]->setString(tr("Objects: 0", "Objetos: 0"));
        m_statLabels[1]->setString(tr("Collision: 0 | Deco: 0", "Colision: 0 | Deco: 0"));
        m_statLabels[2]->setString(tr("Corners: 0 | Excluded: 0", "Esquinas: 0 | Excluidos: 0"));
        return;
    }

    std::size_t const saved = m_plan.inputCount - m_plan.outputCount;
    float const pct = 100.f * static_cast<float>(saved) / static_cast<float>(m_plan.inputCount);

    m_statLabels[0]->setString(fmt::format(
        fmt::runtime(tr("Objects: {} -> {} (-{}, -{:.1f}%)", "Objetos: {} -> {} (-{}, -{:.1f}%)")),
        m_plan.inputCount, m_plan.outputCount, saved, pct
    ).c_str());
    m_statLabels[1]->setString(fmt::format(
        fmt::runtime(tr("Collision: {} -> {} | Deco: {} -> {}", "Colision: {} -> {} | Deco: {} -> {}")),
        m_plan.solidInputCount, m_plan.solidOutputCount,
        m_plan.decoInputCount, m_plan.decoOutputCount
    ).c_str());
    m_statLabels[2]->setString(fmt::format(
        fmt::runtime(tr("Corners kept: {} | Excluded: {}", "Esquinas intactas: {} | Excluidos: {}")),
        m_plan.preservedCornerCount, m_plan.excludedCount
    ).c_str());

    for (auto* lbl : m_statLabels) {
        lbl->limitLabelWidth(kLeftWidth - 16.f, 0.31f, 0.16f);
    }
}

void OptimizerPopup::rebuildPreview() {
    if (!m_drawNode) return;
    m_drawNode->clear();
    if (m_scene.records.empty()) return;

    std::vector<bool> mergedSources(m_scene.records.size(), false);
    for (auto const& g : m_plan.merges) {
        for (std::size_t idx : g.sourceIndices) {
            if (idx < mergedSources.size()) mergedSources[idx] = true;
        }
    }
    std::vector<bool> corners(m_scene.records.size(), false);
    for (std::size_t idx : m_plan.preservedCorners) {
        if (idx < corners.size()) corners[idx] = true;
    }

    static constexpr ccColor4F kFillMerged{0.25f, 0.90f, 0.65f, 0.42f};
    static constexpr ccColor4F kLineMerged{0.35f, 1.00f, 0.78f, 0.95f};
    static constexpr ccColor4F kFillSource{0.25f, 0.85f, 0.95f, 0.24f};
    static constexpr ccColor4F kLineSource{0.35f, 0.90f, 1.00f, 0.85f};
    static constexpr ccColor4F kFillCorner{1.00f, 0.78f, 0.25f, 0.48f};
    static constexpr ccColor4F kLineCorner{1.00f, 0.86f, 0.35f, 1.00f};
    static constexpr ccColor4F kFillIntact{0.50f, 0.56f, 0.72f, 0.22f};
    static constexpr ccColor4F kLineIntact{0.62f, 0.68f, 0.85f, 0.65f};

    std::size_t intactCount = 0;
    std::size_t totalToDraw = m_showAfter ? m_plan.merges.size() : 0;
    for (std::size_t i = 0; i < m_scene.records.size(); ++i) {
        bool const isMergedSource = mergedSources[i];
        if (m_showAfter && isMergedSource) continue;
        ++totalToDraw;
        if (!corners[i] && !isMergedSource) ++intactCount;
    }

    // Keep CCDrawNode lightweight on massive levels by dropping outlines first,
    // and striding non-optimizable background objects while keeping 100% of merges & corners.
    bool const lightBorders = totalToDraw > 1800;
    bool const noBorders = totalToDraw > 3500;
    constexpr std::size_t kMaxIntactPreview = 4000;
    std::size_t const intactStride = intactCount > kMaxIntactPreview
        ? (intactCount + kMaxIntactPreview - 1) / kMaxIntactPreview
        : 1;
    std::size_t intactSeen = 0;

    for (std::size_t i = 0; i < m_scene.records.size(); ++i) {
        auto const& rec = m_scene.records[i];
        bool const isMergedSource = mergedSources[i];
        if (m_showAfter && isMergedSource) continue;

        float const w = rec.baseWidth * rec.scaleX;
        float const h = rec.baseHeight * rec.scaleY;
        if (corners[i]) {
            drawBox(m_drawNode, rec.x, rec.y, w, h, rec.rotation, kFillCorner, kLineCorner,
                    noBorders ? 0.f : 0.7f);
        } else if (isMergedSource) {
            drawBox(m_drawNode, rec.x, rec.y, w, h, rec.rotation, kFillSource, kLineSource,
                    noBorders ? 0.f : 0.7f);
        } else {
            if ((intactSeen++ % intactStride) != 0) continue;
            drawBox(m_drawNode, rec.x, rec.y, w, h, rec.rotation, kFillIntact, kLineIntact,
                    (lightBorders || noBorders) ? 0.f : 0.7f);
        }
    }

    if (m_showAfter) {
        for (auto const& g : m_plan.merges) {
            auto const& keeper = m_scene.records[g.keeperIndex];
            float const w = keeper.baseWidth * g.newScaleX;
            float const h = keeper.baseHeight * g.newScaleY;
            drawBox(m_drawNode, g.newX, g.newY, w, h, keeper.rotation, kFillMerged, kLineMerged,
                    noBorders ? 0.f : 0.7f);
        }
    }

    refreshCameraTransform();
}

void OptimizerPopup::fitCameraToScene() {
    m_zoom = 1.f;
    if (m_scene.records.empty()) {
        m_camCenter = CCPoint{0.f, 0.f};
        m_baseScale = 1.f;
        refreshCameraTransform();
        updatePreviewHeader();
        return;
    }

    bool hasEligible = false;
    for (auto const& rec : m_scene.records) {
        if (rec.isEligible) {
            hasEligible = true;
            break;
        }
    }

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& rec : m_scene.records) {
        if (hasEligible && !rec.isEligible) continue;
        float const half = std::max(rec.baseWidth * rec.scaleX, rec.baseHeight * rec.scaleY) * 0.6f;
        minX = std::min(minX, rec.x - half);
        minY = std::min(minY, rec.y - half);
        maxX = std::max(maxX, rec.x + half);
        maxY = std::max(maxY, rec.y + half);
    }

    float const spanX = std::max(maxX - minX, 60.f);
    float const spanY = std::max(maxY - minY, 60.f);
    float const clipW = kPreviewWidth - kPreviewInset * 2.f;
    float const clipH = kPreviewHeight - kPreviewInset * 2.f - kHeaderBarHeight;

    m_camCenter = CCPoint{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f};
    m_baseScale = std::clamp(std::min((clipW - 18.f) / spanX, (clipH - 18.f) / spanY), 0.02f, 2.5f);
    refreshCameraTransform();
    updatePreviewHeader();
}

void OptimizerPopup::refreshCameraTransform() {
    if (!m_previewWorld) return;
    float const clipW = kPreviewWidth - kPreviewInset * 2.f;
    float const clipH = kPreviewHeight - kPreviewInset * 2.f - kHeaderBarHeight;
    float const scale = m_baseScale * m_zoom;
    m_previewWorld->setScale(scale);
    m_previewWorld->setPosition({
        clipW * 0.5f - m_camCenter.x * scale,
        clipH * 0.5f - m_camCenter.y * scale,
    });
}

void OptimizerPopup::adjustZoom(float factor, CCPoint const* focusMain) {
    float const oldZoom = m_zoom;
    float const newZoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
    if (std::abs(newZoom - oldZoom) < 1e-5f) return;

    if (focusMain) {
        float const clipW = kPreviewWidth - kPreviewInset * 2.f;
        float const clipH = kPreviewHeight - kPreviewInset * 2.f - kHeaderBarHeight;
        CCPoint const offset{
            focusMain->x - (kPreviewX + kPreviewInset + clipW * 0.5f),
            focusMain->y - (kPreviewY + kPreviewInset + clipH * 0.5f),
        };
        float const oldScale = std::max(m_baseScale * oldZoom, 0.0001f);
        float const newScale = std::max(m_baseScale * newZoom, 0.0001f);
        CCPoint const worldAtCursor{
            m_camCenter.x + offset.x / oldScale,
            m_camCenter.y + offset.y / oldScale,
        };
        m_camCenter.x = worldAtCursor.x - offset.x / newScale;
        m_camCenter.y = worldAtCursor.y - offset.y / newScale;
    }

    m_zoom = newZoom;
    refreshCameraTransform();
    updatePreviewHeader();
}

void OptimizerPopup::updatePreviewHeader() {
    if (!m_viewLabel) return;
    std::string const text = fmt::format(
        "{} | x{:.1f}",
        m_showAfter ? tr("After", "Despues") : tr("Before", "Antes"),
        m_zoom
    );
    m_viewLabel->setString(text.c_str());
    m_viewLabel->limitLabelWidth(kPreviewWidth - 20.f, 0.34f, 0.2f);
}

void OptimizerPopup::setStatus(std::string const& text, ccColor3B color) {
    if (!m_statusLabel) return;
    m_statusLabel->setColor(color);
    kit::setWrapped(m_statusLabel, text, {kStatusWidth - 24.f, kStatusHeight - 3.f}, 0.3f, 0.23f);
    if (m_statusIcon) m_statusIcon->setColor(color);
}

bool OptimizerPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    CCRect const preview{kPreviewX, kPreviewY, kPreviewWidth, kPreviewHeight};
    if (m_mainLayer && preview.containsPoint(m_mainLayer->convertToNodeSpace(touch->getLocation()))) {
        m_panning = true;
        return true;
    }
    return FLAlertLayer::ccTouchBegan(touch, event);
}

void OptimizerPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_panning) {
        FLAlertLayer::ccTouchMoved(touch, event);
        return;
    }
    auto const delta = touch->getDelta();
    float const scale = std::max(m_baseScale * m_zoom, 0.001f);
    m_camCenter.x -= delta.x / scale;
    m_camCenter.y -= delta.y / scale;
    refreshCameraTransform();
}

void OptimizerPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_panning) {
        m_panning = false;
        return;
    }
    FLAlertLayer::ccTouchEnded(touch, event);
}

void OptimizerPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_panning = false;
    FLAlertLayer::ccTouchCancelled(touch, event);
}

bool OptimizerPopup::scrollWheel(float vertical, float horizontal) {
    float const steps = paimon::smoothscroll::SmoothScrollController::get()
        .filteredZoomSteps(vertical, horizontal);
    if (std::abs(steps) < 0.000001f) return true;
    float const baseFactor = steps > 0.f ? kScrollZoomIn : kScrollZoomOut;
    float const factor = std::pow(baseFactor, std::abs(steps));

    if (m_mainLayer) {
        CCRect const preview{
            kPreviewX + kPreviewInset,
            kPreviewY + kPreviewInset,
            kPreviewWidth - kPreviewInset * 2.f,
            kPreviewHeight - kPreviewInset * 2.f - kHeaderBarHeight,
        };
        CCPoint const mouseMain = m_mainLayer->convertToNodeSpace(geode::cocos::getMousePos());
        if (preview.containsPoint(mouseMain)) {
            adjustZoom(factor, &mouseMain);
            return true;
        }
    }
    adjustZoom(factor);
    return true;
}

} // namespace paimon::optimizer
