#include "ButtonSkinPopup.hpp"

#include "../services/ButtonSkinRuntime.hpp"
#include "../services/ButtonSkinStore.hpp"
#include "../../quick-hub/services/QuickButtonSfx.hpp"
#include "../../quick-hub/ui/QuickButtonEditKit.hpp"
#include "../../quick-hub/ui/QuickButtonSfxPopup.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <system_error>

using namespace geode::prelude;

namespace paimon::buttonskin {

namespace {

namespace kit = paimon::configkit;
namespace ui = paimon::ui;

constexpr float kPopupW = 470.f;
constexpr float kPopupH = 290.f;

constexpr float kLeftX = 14.f;
constexpr float kLeftW = 150.f;
constexpr float kLeftCx = kLeftX + kLeftW / 2.f;
constexpr float kPreviewY = 144.f;
constexpr float kPreviewH = 112.f;

constexpr float kRightX = 176.f;
constexpr float kRightW = kPopupW - kRightX - 14.f;
constexpr float kScrollY = 40.f;
constexpr float kTabY = 230.f;
constexpr float kScrollH = kTabY - 4.f - kScrollY;

constexpr ccColor3B kFillAccent{255, 170, 220};
constexpr ccColor3B kOriginalAccent{120, 210, 255};
constexpr ccColor3B kShapeAccent{130, 240, 170};
constexpr ccColor3B kSoundAccent{255, 200, 110};

int rowTouchPriority() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

std::string percent(double v) {
    return fmt::format("{}%", static_cast<int>(std::lround(v * 100.0)));
}

std::string signedPercent(double v) {
    int value = static_cast<int>(std::lround(v * 100.0));
    return value > 0 ? fmt::format("+{}%", value) : fmt::format("{}%", value);
}

std::string degrees(double v) {
    return fmt::format("{} deg", static_cast<int>(std::lround(v)));
}

std::string points(double v) {
    int value = static_cast<int>(std::lround(v));
    return value > 0 ? fmt::format("+{} pt", value) : fmt::format("{} pt", value);
}

// title over a strip of equal buttons; the picked one turns cyan.
CCNode* makeChoiceRow(float width, char const* title, std::vector<char const*> const& options,
                      int selected, std::function<void(int)> onSelect) {
    constexpr float kRowH = 46.f;
    constexpr float kGap = 4.f;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});
    row->addChild(ui::makeInset({width, kRowH}, 55), -1);

    auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
    label->setAnchorPoint({0.f, 1.f});
    label->limitLabelWidth(width - 20.f, 0.38f, 0.1f);
    label->setPosition({10.f, kRowH - 7.f});
    row->addChild(label);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(rowTouchPriority());
    row->addChild(menu, 5);

    int const count = std::max<int>(1, static_cast<int>(options.size()));
    float const segW = (width - 20.f - kGap * static_cast<float>(count - 1)) / static_cast<float>(count);
    // raw pointers only: the buttons own the callbacks that own this list.
    auto buttons = std::make_shared<std::vector<CCMenuItemSpriteExtra*>>();
    auto callback = std::make_shared<std::function<void(int)>>(std::move(onSelect));
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        auto* button = ui::makeButton(options[static_cast<size_t>(i)], [buttons, callback, i] {
            for (size_t j = 0; j < buttons->size(); ++j) {
                ui::setButtonSkin((*buttons)[j], static_cast<int>(j) == i ? ui::Btn::Cyan : ui::Btn::Gray);
            }
            ui::matchButtonLabels(*buttons);
            if (*callback) (*callback)(i);
        }, i == selected ? ui::Btn::Cyan : ui::Btn::Gray, segW, 0.55f, "bigFont.fnt");
        button->setPosition({10.f + static_cast<float>(i) * (segW + kGap) + segW / 2.f, 15.f});
        menu->addChild(button);
        buttons->push_back(button);
    }
    ui::matchButtonLabels(*buttons);
    return row;
}

} // namespace

ButtonSkinPopup* ButtonSkinPopup::s_instance = nullptr;

bool ButtonSkinPopup::isOpen() {
    return s_instance != nullptr;
}

ButtonSkinPopup* ButtonSkinPopup::create(SkinEditTarget target, std::optional<ButtonSkin> existing) {
    auto* ret = new ButtonSkinPopup();
    ret->m_target = std::move(target);
    if (existing) {
        ret->m_skin = std::move(*existing);
        ret->m_editing = true;
        // the clicked button becomes the skin's address: narrowing a shared style to
        // "solo este boton" must land on this one, not on the first button it was made on.
        copyButtonAddress(ret->m_skin.target, ret->m_target.identity);
        ret->m_skin.signature = ret->m_target.signature;
    } else {
        ret->m_skin.target = ret->m_target.identity;
        ret->m_skin.signature = ret->m_target.signature;
    }
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ButtonSkinPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    s_instance = this;
    ButtonSkinRuntime::markSkinUi(this);
    this->setID("button-skin-editor"_spr);

    this->setTitle(m_editing ? "Editar estilo del boton" : "Personalizar boton");
    this->addCorners();
    this->addInfoButton("Personalizar boton",
        "Cambia como se ve y suena este boton del juego.\n\n"
        "<cy>Relleno</c>: pinta una <cg>imagen</c>, un <cg>color</c> o un <cg>degradado</c> "
        "que se acopla a la forma del boton, como si lo pintaras. Ajusta encaje, tamano, "
        "posicion, giro, mezcla y opacidad.\n"
        "<cj>Original</c>: tine, aclara u oculta el dibujo del juego.\n"
        "<co>Forma</c>: tamano, giro y posicion del boton entero.\n"
        "<cy>Sonido</c>: sonido propio al pulsar o silencio.\n\n"
        "<cg>Aplicar a</c> elige si cambia solo este boton, los iguales de esta pantalla "
        "o los iguales de todo el juego.");

    buildLeftColumn();

    auto* tabs = kit::makeTabBar(kRightW, {"Relleno", "Original", "Forma", "Sonido"}, m_tab,
        [this](int index) {
            m_tab = index;
            scheduleRebuild(false);
        });
    tabs->setPosition({kRightX, kTabY});
    m_mainLayer->addChild(tabs, 3);

    auto* saveButton = ui::makeButton("Guardar", [this] { this->onSave(); }, ui::Btn::Green, 0.f, 0.66f);
    m_buttonMenu->addChildAtPosition(saveButton, Anchor::BottomRight, ccp(-58.f, 22.f));

    rebuildTab(false);
    rebuildPreview();
    this->scheduleUpdate();
    return true;
}

void ButtonSkinPopup::onExit() {
    quickhub::stopQuickButtonSfx();
    if (s_instance == this) s_instance = nullptr;
    PaimonPopup::onExit();
}

void ButtonSkinPopup::update(float) {
    if (m_previewDirty) rebuildPreview();
}

bool ButtonSkinPopup::scopeAvailable(SkinScope scope) const {
    switch (scope) {
        case SkinScope::SameScreen:
            // without a known screen "iguales aqui" would spread over the whole game.
            return !sameScreenClass(m_skin.target).empty() &&
                   (!m_skin.target.targetNodeId.empty() || !m_skin.signature.empty());
        case SkinScope::Everywhere:
            return !m_skin.signature.empty();
        default:
            return true;
    }
}

void ButtonSkinPopup::buildLeftColumn() {
    if (auto* frame = ui::makeInset({kLeftW, kPreviewH}, 110)) {
        frame->setPosition({kLeftX, kPreviewY});
        m_mainLayer->addChild(frame, 0);
    }

    // a grown or moved button must not spill over the rest of the editor.
    constexpr float kInner = 4.f;
    auto* stencil = paimon::SpriteHelper::createRectStencil(kLeftW - kInner * 2.f, kPreviewH - kInner * 2.f);
    auto* clip = CCClippingNode::create(stencil);
    clip->setPosition({kLeftX + kInner, kPreviewY + kInner});
    m_mainLayer->addChild(clip, 1);

    m_preview = CCNode::create();
    m_preview->setPosition({kLeftW / 2.f - kInner, kPreviewH / 2.f - kInner});
    clip->addChild(m_preview);

    auto const& id = m_skin.target;
    std::string screen = quickhub::friendlyScreenName(id.ownerClass.empty() ? id.sceneClass : id.ownerClass);
    std::string button = !id.targetNodeId.empty() ? id.targetNodeId
                       : !id.labelText.empty()    ? id.labelText
                                                  : std::string("sin id");

    auto addLine = [&](std::string const& text, ccColor3B color, float y) {
        auto* label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(kLeftW - 4.f, 0.4f, 0.16f);
        label->setColor(color);
        label->setPosition({kLeftX + 2.f, y});
        m_mainLayer->addChild(label, 2);
    };
    addLine(fmt::format("Pantalla: {}", screen), {190, 200, 220}, kPreviewY - 11.f);
    addLine(fmt::format("Boton: {}", button), {150, 160, 185}, kPreviewY - 24.f);

    auto* heading = ui::makeTitle("Aplicar a", kLeftW, 0.42f);
    heading->setAnchorPoint({0.f, 0.5f});
    heading->setPosition({kLeftX + 2.f, kPreviewY - 40.f});
    m_mainLayer->addChild(heading, 2);

    struct ScopeOption { char const* label; SkinScope scope; };
    static ScopeOption const options[] = {
        {"Solo este boton", SkinScope::Exact},
        {"Iguales en pantalla", SkinScope::SameScreen},
        {"Todo el juego", SkinScope::Everywhere},
    };
    float y = kPreviewY - 57.f;
    for (auto const& option : options) {
        auto scope = option.scope;
        auto* button = ui::makeButton(option.label, [this, scope] {
            if (!scopeAvailable(scope)) return;
            m_skin.scope = scope;
            refreshScopeButtons();
        }, ui::Btn::Gray, kLeftW - 10.f, 0.5f, "bigFont.fnt");
        button->setPosition({kLeftCx, y});
        m_buttonMenu->addChild(button);
        m_scopeButtons.push_back(button);
        y -= 19.f;
    }
    ui::matchButtonLabels(m_scopeButtons);
    refreshScopeButtons();

    if (m_editing) {
        auto* remove = ui::makeButton("Quitar estilo", [this] { this->onRemove(); },
            ui::Btn::Red, 0.f, 0.5f, "bigFont.fnt");
        remove->setPosition({kLeftCx, 20.f});
        m_buttonMenu->addChild(remove);
    }
}

void ButtonSkinPopup::refreshScopeButtons() {
    static SkinScope const scopes[] = {SkinScope::Exact, SkinScope::SameScreen, SkinScope::Everywhere};
    for (size_t i = 0; i < m_scopeButtons.size() && i < 3; ++i) {
        auto* button = m_scopeButtons[i];
        bool const available = scopeAvailable(scopes[i]);
        ui::setButtonSkin(button, m_skin.scope == scopes[i] ? ui::Btn::Cyan : ui::Btn::Gray);
        button->setEnabled(available);
        if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(button->getNormalImage())) {
            rgba->setCascadeOpacityEnabled(true);
            rgba->setOpacity(available ? 255 : 110);
        }
    }
}

void ButtonSkinPopup::rebuildPreview() {
    m_previewDirty = false;
    if (!m_preview) return;
    m_preview->removeAllChildren();
    if (!m_target.shape) {
        auto* label = CCLabelBMFont::create("Sin vista previa", "chatFont.fnt");
        label->setScale(0.45f);
        label->setColor({150, 160, 185});
        m_preview->addChild(label);
        return;
    }
    m_preview->addChild(buildSkinPreview(m_skin, m_target.shape, kPreviewH - 8.f,
                                         m_target.pointScale, true));
}

void ButtonSkinPopup::scheduleRebuild(bool keepScroll) {
    m_keepScroll = m_rebuildQueued ? (m_keepScroll && keepScroll) : keepScroll;
    if (m_rebuildQueued) return;
    m_rebuildQueued = true;
    // the control that asked still lives in the current scroll: rebuild next frame.
    this->retain();
    Loader::get()->queueInMainThread([this] {
        m_rebuildQueued = false;
        if (this->getParent()) this->rebuildTab(m_keepScroll);
        this->release();
    });
}

void ButtonSkinPopup::rebuildTab(bool keepScroll) {
    float fromTop = 0.f;
    bool restore = false;
    if (m_scroll) {
        if (keepScroll && m_scroll->m_contentLayer) {
            auto* content = m_scroll->m_contentLayer;
            float top = m_scroll->getContentSize().height - content->getContentSize().height;
            fromTop = content->getPositionY() - top;
            restore = true;
        }
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    std::vector<CCNode*> items;
    switch (m_tab) {
        case 1: items = buildOriginalTab(kRightW); break;
        case 2: items = buildShapeTab(kRightW); break;
        case 3: items = buildSoundTab(kRightW); break;
        default: items = buildFillTab(kRightW); break;
    }

    m_scroll = kit::makeScrollStack({kRightW, kScrollH}, items, 6.f);
    m_scroll->setPosition({kRightX, kScrollY});
    m_mainLayer->addChild(m_scroll, 2);

    if (restore && m_scroll->m_contentLayer) {
        auto* content = m_scroll->m_contentLayer;
        float top = kScrollH - content->getContentSize().height;
        content->setPositionY(std::clamp(top + fromTop, std::min(top, 0.f), 0.f));
    }
}

std::vector<CCNode*> ButtonSkinPopup::buildFillTab(float width) {
    float const inner = kit::cardInnerWidth(width);
    std::vector<CCNode*> items;

    std::vector<CCNode*> rows;
    rows.push_back(makeChoiceRow(inner, "Tipo de relleno", {"Nada", "Imagen", "Color", "Degradado"},
        static_cast<int>(m_skin.fill), [this](int index) {
            m_skin.fill = static_cast<SkinFill>(index);
            markDirty();
            scheduleRebuild(true);
            if (m_skin.fill == SkinFill::Image && m_skin.imagePath.empty()) onChooseImage();
        }));

    if (m_skin.fill == SkinFill::Image) {
        std::string name = quickhub::fileNameOf(m_skin.imagePath);
        rows.push_back(kit::makeButtonRow(inner, "Imagen",
            name.empty() ? "PNG, JPG o WEBP, hasta 8 MB." : name.c_str(),
            name.empty() ? "Elegir" : "Cambiar", [this] { this->onChooseImage(); }, ui::Btn::Cyan));
    } else if (m_skin.fill == SkinFill::Color) {
        rows.push_back(kit::makeColorRow(inner, "Color", nullptr, m_skin.fillColor,
            [this](ccColor3B color) { m_skin.fillColor = color; markDirty(); }));
    } else if (m_skin.fill == SkinFill::Gradient) {
        rows.push_back(kit::makeColorRow(inner, "Color A", "Arranca en este lado.", m_skin.fillColor,
            [this](ccColor3B color) { m_skin.fillColor = color; markDirty(); }));
        rows.push_back(kit::makeColorRow(inner, "Color B", nullptr, m_skin.fillColorB,
            [this](ccColor3B color) { m_skin.fillColorB = color; markDirty(); }));
        rows.push_back(kit::makeSliderRow(inner, "Angulo", "0 = A arriba, 90 = A a la izquierda.",
            m_skin.fillRotation, -180.0, 180.0, degrees,
            [this](double v) { m_skin.fillRotation = static_cast<float>(v); markDirty(); }));
    }

    if (m_skin.fill != SkinFill::None) {
        rows.push_back(kit::makeToggleRow(inner, "Acoplar a la forma",
            "Solo pinta donde el boton tiene pixeles. Apagado cubre todo su recuadro.",
            m_skin.maskToShape, [this](bool v) { m_skin.maskToShape = v; markDirty(); }));
    }
    items.push_back(kit::makeCard(width, "Relleno", kFillAccent, rows));

    if (m_skin.fill == SkinFill::Image) {
        items.push_back(kit::makeCard(width, "Encaje de la imagen", kFillAccent, {
            makeChoiceRow(inner, "Encaje", {"Cubrir", "Contener", "Estirar"},
                static_cast<int>(m_skin.fit), [this](int index) {
                    m_skin.fit = static_cast<SkinFit>(index);
                    markDirty();
                }),
            kit::makeSliderRow(inner, "Tamano", nullptr, m_skin.fillScale,
                limits::kFillScaleMin, limits::kFillScaleMax, percent,
                [this](double v) { m_skin.fillScale = static_cast<float>(v); markDirty(); }),
            kit::makeSliderRow(inner, "Mover X", nullptr, m_skin.fillOffsetX,
                -limits::kFillOffsetMax, limits::kFillOffsetMax, signedPercent,
                [this](double v) { m_skin.fillOffsetX = static_cast<float>(v); markDirty(); }),
            kit::makeSliderRow(inner, "Mover Y", nullptr, m_skin.fillOffsetY,
                -limits::kFillOffsetMax, limits::kFillOffsetMax, signedPercent,
                [this](double v) { m_skin.fillOffsetY = static_cast<float>(v); markDirty(); }),
            kit::makeSliderRow(inner, "Giro", nullptr, m_skin.fillRotation, -180.0, 180.0, degrees,
                [this](double v) { m_skin.fillRotation = static_cast<float>(v); markDirty(); }),
            kit::makeToggleRow(inner, "Voltear horizontal", nullptr, m_skin.fillFlipX,
                [this](bool v) { m_skin.fillFlipX = v; markDirty(); }),
            kit::makeToggleRow(inner, "Voltear vertical", nullptr, m_skin.fillFlipY,
                [this](bool v) { m_skin.fillFlipY = v; markDirty(); }),
        }));
    }

    if (m_skin.fill != SkinFill::None) {
        items.push_back(kit::makeCard(width, "Mezcla", kFillAccent, {
            makeChoiceRow(inner, "Modo", {"Normal", "Multiplicar", "Sumar", "Pantalla"},
                static_cast<int>(m_skin.blend), [this](int index) {
                    m_skin.blend = static_cast<SkinBlend>(index);
                    markDirty();
                }),
            kit::makeSliderRow(inner, "Opacidad", nullptr, m_skin.fillOpacity, 0.0, 1.0, percent,
                [this](double v) { m_skin.fillOpacity = static_cast<float>(v); markDirty(); }),
        }));
        items.push_back(kit::makeHint(width,
            "Multiplicar conserva las sombras y bordes del boton; Sumar y Pantalla lo iluminan."));
    } else {
        items.push_back(kit::makeHint(width,
            "Elige Imagen, Color o Degradado para pintar el boton. El relleno toma su forma, "
            "como si lo pintaras encima."));
    }
    return items;
}

std::vector<CCNode*> ButtonSkinPopup::buildOriginalTab(float width) {
    float const inner = kit::cardInnerWidth(width);
    return {
        kit::makeCard(width, "Dibujo original", kOriginalAccent, {
            kit::makeToggleRow(inner, "Tenir", "Multiplica los colores del boton por el tinte.",
                m_skin.tintEnabled, [this](bool v) { m_skin.tintEnabled = v; markDirty(); }),
            kit::makeColorRow(inner, "Color del tinte", nullptr, m_skin.tint,
                [this](ccColor3B color) {
                    m_skin.tint = color;
                    // picking a color means the tint is wanted.
                    if (!m_skin.tintEnabled) {
                        m_skin.tintEnabled = true;
                        scheduleRebuild(true);
                    }
                    markDirty();
                }),
            kit::makeSliderRow(inner, "Opacidad", nullptr, m_skin.baseOpacity, 0.0, 1.0, percent,
                [this](double v) { m_skin.baseOpacity = static_cast<float>(v); markDirty(); }),
            kit::makeToggleRow(inner, "Ocultar original",
                "Solo queda visible el relleno. Sin relleno, el boton se vuelve invisible.",
                m_skin.hideOriginal, [this](bool v) { m_skin.hideOriginal = v; markDirty(); }),
        }),
        kit::makeHint(width, "El boton sigue respondiendo al click aunque lo ocultes."),
    };
}

std::vector<CCNode*> ButtonSkinPopup::buildShapeTab(float width) {
    float const inner = kit::cardInnerWidth(width);
    return {
        kit::makeCard(width, "Boton completo", kShapeAccent, {
            kit::makeSliderRow(inner, "Tamano", "El area de click crece con el boton.",
                m_skin.scale, limits::kScaleMin, limits::kScaleMax, percent,
                [this](double v) { m_skin.scale = static_cast<float>(v); markDirty(); }),
            kit::makeSliderRow(inner, "Giro", nullptr, m_skin.rotation, -180.0, 180.0, degrees,
                [this](double v) { m_skin.rotation = static_cast<float>(v); markDirty(); }),
            kit::makeSliderRow(inner, "Mover X", nullptr, m_skin.offsetX,
                -limits::kOffsetMax, limits::kOffsetMax, points,
                [this](double v) { m_skin.offsetX = static_cast<float>(v); markDirty(); }),
            kit::makeSliderRow(inner, "Mover Y", nullptr, m_skin.offsetY,
                -limits::kOffsetMax, limits::kOffsetMax, points,
                [this](double v) { m_skin.offsetY = static_cast<float>(v); markDirty(); }),
            kit::makeButtonRow(inner, "Restablecer forma", nullptr, "Restablecer", [this] {
                m_skin.scale = 1.f;
                m_skin.rotation = 0.f;
                m_skin.offsetX = 0.f;
                m_skin.offsetY = 0.f;
                markDirty();
                scheduleRebuild(true);
            }, ui::Btn::Gray),
        }),
        kit::makeHint(width, "Mover usa puntos de la pantalla; la vista previa lo muestra a escala."),
    };
}

std::string ButtonSkinPopup::soundSummary() const {
    auto const& t = m_skin.target;
    auto kind = static_cast<quickhub::QuickButtonSfxKind>(t.sfxKind);
    if (kind == quickhub::QuickButtonSfxKind::None) return "Ninguno: suena el del juego.";
    std::string source = t.sfxPath;
    if (kind == quickhub::QuickButtonSfxKind::File) source = quickhub::fileNameOf(t.sfxPath);
    if (kind == quickhub::QuickButtonSfxKind::Online) source = fmt::format("#{}", t.sfxId);
    return fmt::format("{}: {}", quickhub::sfxKindName(t.sfxKind), source);
}

std::vector<CCNode*> ButtonSkinPopup::buildSoundTab(float width) {
    float const inner = kit::cardInnerWidth(width);
    auto summary = soundSummary();
    return {
        kit::makeCard(width, "Al pulsar", kSoundAccent, {
            kit::makeButtonRow(inner, "Sonido propio", summary.c_str(), "Editar", [this] {
                if (auto* editor = quickhub::QuickButtonSfxPopup::create(&m_skin.target, [this] {
                        scheduleRebuild(true);
                    })) {
                    editor->show();
                }
            }, ui::Btn::Cyan),
            kit::makeToggleRow(inner, "Silenciar el original",
                "Calla el sonido del juego aunque no pongas uno propio.",
                m_skin.muteOriginal, [this](bool v) { m_skin.muteOriginal = v; }),
            kit::makeButtonRow(inner, "Probar", nullptr, "Probar", [this] { this->onTestSound(); },
                ui::Btn::Green),
        }),
        kit::makeHint(width, "El sonido propio reemplaza al del juego cada vez que pulsas el boton."),
    };
}

void ButtonSkinPopup::onTestSound() {
    if (hasCustomSound(m_skin)) {
        if (!quickhub::playQuickButtonSfx(m_skin.target)) {
            PaimonNotify::show("No se pudo reproducir: revisa el sonido.", NotificationIcon::Warning);
        }
        return;
    }
    PaimonNotify::show(m_skin.muteOriginal ? "Silenciado: al pulsar no sonara nada."
                                           : "Suena el sonido original del juego.",
                       NotificationIcon::Info);
}

void ButtonSkinPopup::onChooseImage() {
    WeakRef<ButtonSkinPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto picked = std::move(result).unwrapOr(std::nullopt);
        if (!picked.has_value() || picked->empty()) return;
        static_cast<ButtonSkinPopup*>(popup.data())->importImage(*picked);
    });
}

void ButtonSkinPopup::importImage(std::filesystem::path const& src) {
    // reject before copying: the copy runs on the main thread.
    auto ext = geode::utils::string::toLower(paimon::paths::toUtf8(src.extension()));
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp") {
        PaimonNotify::show("Usa un PNG, JPG o WEBP.", NotificationIcon::Warning);
        return;
    }
    std::error_code sizeError;
    auto bytes = std::filesystem::file_size(src, sizeError);
    if (!sizeError && bytes > 8 * 1024 * 1024) {
        PaimonNotify::show("Imagen muy pesada (max 8 MB).", NotificationIcon::Warning);
        return;
    }

    std::string stem = !m_skin.id.empty() ? m_skin.id : m_skin.target.name;
    std::string dest = quickhub::importFileToConfigDir(
        src, ButtonSkinStore::imagesDir(), stem,
        [](std::filesystem::path const&, std::string const& destStr) {
            bool ok = false;
            if (auto* tex = CCTextureCache::sharedTextureCache()->addImage(destStr.c_str(), false)) {
                auto px = tex->getContentSizeInPixels();
                ok = px.width > 2.f || px.height > 2.f;
            }
            if (!ok) {
                PaimonNotify::show("Ese archivo no es una imagen valida.", NotificationIcon::Error);
                return false;
            }
            return true;
        });
    if (dest.empty()) return;
    m_skin.imagePath = std::move(dest);
    m_skin.fill = SkinFill::Image;
    markDirty();
    scheduleRebuild(true);
}

void ButtonSkinPopup::onSave() {
    sanitize(m_skin);
    if (m_skin.fill == SkinFill::Image) {
        std::error_code ec;
        bool exists = !m_skin.imagePath.empty() &&
                      std::filesystem::is_regular_file(m_skin.imagePath, ec) && !ec;
        if (!exists) {
            if (!m_skin.imagePath.empty()) {
                PaimonNotify::show("La imagen ya no existe: se quito el relleno.", NotificationIcon::Warning);
            }
            m_skin.imagePath.clear();
            m_skin.fill = SkinFill::None;
        }
    }
    if (!scopeAvailable(m_skin.scope)) m_skin.scope = SkinScope::Exact;

    auto& store = ButtonSkinStore::get();
    auto& runtime = ButtonSkinRuntime::get();

    if (!changesAnything(m_skin)) {
        if (!m_editing) {
            PaimonNotify::show("No hay cambios que guardar.", NotificationIcon::Warning);
            return;
        }
        store.remove(m_skin.id);
        runtime.requestScan();
        PaimonNotify::show("Boton restaurado.", NotificationIcon::Success);
        this->keyBackClicked();
        return;
    }

    if (m_skin.id.empty()) {
        m_skin.id = store.makeUniqueId(m_skin.target.name.empty() ? "boton" : m_skin.target.name);
    }
    if (!store.save(m_skin)) {
        PaimonNotify::show("No se pudo guardar el estilo.", NotificationIcon::Error);
        return;
    }

    // dress the clicked button now; the scan finds its look-alikes on its own.
    if (auto item = m_target.item.lock()) {
        runtime.adopt(item, m_skin.id);
    } else {
        runtime.requestScan();
    }

    PaimonNotify::show(m_editing ? "Estilo actualizado!" : "Boton personalizado!", NotificationIcon::Success);
    this->keyBackClicked();
}

void ButtonSkinPopup::onRemove() {
    if (!m_editing || m_skin.id.empty()) return;
    WeakRef<ButtonSkinPopup> self = this;
    geode::createQuickPopup("Quitar estilo",
        m_skin.scope == SkinScope::Exact
            ? std::string("El boton vuelve a verse y sonar como el original.")
            : std::string("Todos los botones con este estilo vuelven a ser los originales."),
        "Cancelar", "Quitar", [self](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup) return;
            auto* editor = static_cast<ButtonSkinPopup*>(popup.data());
            ButtonSkinStore::get().remove(editor->m_skin.id);
            ButtonSkinRuntime::get().requestScan();
            PaimonNotify::show("Estilo quitado.", NotificationIcon::Success);
            editor->keyBackClicked();
        });
}

} // namespace paimon::buttonskin
