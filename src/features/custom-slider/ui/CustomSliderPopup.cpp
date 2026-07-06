#include "CustomSliderPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/CustomSliderManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <fmt/format.h>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::slider;

namespace {
namespace kit = paimon::configkit;

const char* kIconTypeNames[] = { "Cubo", "Nave", "Bola", "OVNI", "Onda", "Robot", "Arana", "Swing" };
constexpr int kIconTypeCount = 8;
const char* kModeNames[] = { "Icono", "Imagen", "GIF" };
constexpr int kModeCount = 3;
const char* kAnimTypeNames[] = { "Ninguna", "Rebote", "Girar", "Ambas" };
constexpr int kAnimTypeCount = 4;
}

CustomSliderPopup* CustomSliderPopup::create() {
    auto* ret = new CustomSliderPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomSliderPopup::init() {
    if (!Popup::init(400.f, 280.f)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Slider Personalizado");

    auto content = m_mainLayer->getContentSize();

    // Vista previa fija arriba a la derecha (siempre visible al ajustar).
    {
        float px = content.width - 30.f, py = content.height - 26.f;
        auto* bg = paimon::SpriteHelper::createDarkPanel(34.f, 34.f, 80, 4.f);
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({px, py});
        m_mainLayer->addChild(bg, 4);
        m_previewNode = CCNode::create();
        m_previewNode->setPosition({px, py});
        m_mainLayer->addChild(m_previewNode, 5);

        auto* lbl = CCLabelBMFont::create("Vista", "chatFont.fnt");
        lbl->setScale(0.4f);
        lbl->setColor(kit::kDescColor);
        lbl->setPosition({px, py - 23.f});
        m_mainLayer->addChild(lbl, 4);
    }

    rebuild();

    // Boton fijo abajo: restaurar valores por defecto
    auto* resetSpr = ButtonSprite::create("Restaurar", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.5f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            CustomSliderManager::get().resetToDefaults();
            scheduleRebuild();
        });
    resetBtn->setPosition({content.width / 2.f, 18.f});
    m_buttonMenu->addChild(resetBtn);

    return true;
}

void CustomSliderPopup::onExit() {
    CustomSliderManager::get().saveConfig();
    Popup::onExit();
}

void CustomSliderPopup::scheduleRebuild() {
    Ref<CustomSliderPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

void CustomSliderPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
        m_shapeGridMenu = nullptr;
    }

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 36.f - 34.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = CustomSliderManager::get().config();

    // Interruptor principal
    auto* hero = kit::makeHeroToggle(scrollW,
        "Slider personalizado",
        "Cambia el puntero de las barras deslizantes del juego.",
        cfg.enabled,
        [this](bool v) {
            CustomSliderManager::get().config().enabled = v;
            refreshPreview();
            reapplyAllSliders();
        });

    // Tarjeta: que se muestra como puntero
    std::vector<CCNode*> styleRows;
    styleRows.push_back(kit::makeSelectRow(innerW,
        "Tipo de puntero",
        "Icono del juego, una imagen tuya o un GIF animado.",
        {kModeNames[0], kModeNames[1], kModeNames[2]},
        static_cast<int>(cfg.thumbMode),
        [this](int v) {
            CustomSliderManager::get().config().thumbMode = static_cast<SliderThumbMode>(v);
            refreshPreview();
            reapplyAllSliders();
            scheduleRebuild(); // cambian las opciones visibles
        }));

    const bool isIconMode = (cfg.thumbMode == SliderThumbMode::Icon);

    if (isIconMode) {
        std::vector<std::string> iconNames(kIconTypeNames, kIconTypeNames + kIconTypeCount);
        styleRows.push_back(kit::makeSelectRow(innerW,
            "Vehiculo", nullptr,
            iconNames, static_cast<int>(cfg.iconType),
            [this](int v) {
                CustomSliderManager::get().config().iconType = static_cast<SliderIconType>(v);
                refreshPreview();
                reapplyAllSliders();
            }));
        styleRows.push_back(kit::makeToggleRow(innerW,
            "Usar tu icono",
            "Muestra el icono que llevas equipado en el juego.",
            cfg.usePlayerIcon,
            [this](bool v) {
                CustomSliderManager::get().config().usePlayerIcon = v;
                refreshPreview();
                reapplyAllSliders();
            }));
        styleRows.push_back(kit::makeToggleRow(innerW,
            "Usar tus colores",
            "Pinta el icono con tus colores del juego.",
            cfg.usePlayerColors,
            [this](bool v) {
                CustomSliderManager::get().config().usePlayerColors = v;
                refreshPreview();
                reapplyAllSliders();
            }));
    } else {
        std::string fileName = cfg.customImagePath.empty()
            ? "Ningun archivo elegido"
            : geode::utils::string::pathToString(
                  std::filesystem::path(cfg.customImagePath).filename());
        if (fileName.size() > 28) fileName = fileName.substr(0, 25) + "...";

        styleRows.push_back(kit::makeButtonRow(innerW,
            "Imagen", fileName.c_str(), "Elegir",
            [this] { onPickImage(); }));

        styleRows.push_back(kit::makeToggleRow(innerW,
            "Marco",
            "Recorta la imagen dentro de una forma.",
            cfg.containerEnabled,
            [this](bool v) {
                CustomSliderManager::get().config().containerEnabled = v;
                refreshPreview();
                reapplyAllSliders();
                scheduleRebuild(); // muestra/oculta borde y formas
            }));

        if (cfg.containerEnabled) {
            styleRows.push_back(kit::makeToggleRow(innerW,
                "Borde del marco",
                "Dibuja un contorno alrededor de la forma.",
                cfg.containerBorderEnabled,
                [this](bool v) {
                    CustomSliderManager::get().config().containerBorderEnabled = v;
                    refreshPreview();
                    reapplyAllSliders();
                }));

            // Fila propia: rejilla de formas (2 filas de 10)
            {
                constexpr float kCell    = 18.f;
                constexpr int   kCols    = 10;
                constexpr float kGridGap = 2.f;
                const float gridW = kCols * kCell + (kCols - 1) * kGridGap;
                const float gridH = 2.f * kCell + kGridGap;
                const float rowH  = gridH + 18.f;

                auto* row = CCNode::create();
                row->setAnchorPoint({0.f, 0.f});
                row->setContentSize({innerW, rowH});

                auto* title = CCLabelBMFont::create("Forma del marco", "bigFont.fnt");
                title->setAnchorPoint({0.f, 1.f});
                title->limitLabelWidth(innerW - 20.f, 0.40f, 0.1f);
                title->setPosition({10.f, rowH - 2.f});
                row->addChild(title);

                m_shapeGridMenu = CCMenu::create();
                m_shapeGridMenu->setAnchorPoint({0.5f, 0.5f});
                m_shapeGridMenu->ignoreAnchorPointForPosition(false);
                m_shapeGridMenu->setContentSize({gridW, gridH});
                m_shapeGridMenu->setPosition({innerW / 2.f, gridH / 2.f});
                m_shapeGridMenu->setTouchPriority(
                    CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
                m_shapeGridMenu->setLayout(
                    RowLayout::create()
                        ->setGap(kGridGap)
                        ->setGrowCrossAxis(true)
                        ->setCrossAxisOverflow(false)
                        ->setCrossAxisAlignment(AxisAlignment::Center)
                        ->setAutoScale(false)
                );
                row->addChild(m_shapeGridMenu, 5);
                rebuildShapeGrid();

                styleRows.push_back(row);
            }
        }
    }

    auto* styleCard = kit::makeCard(scrollW, "Estilo del puntero", {120, 210, 255}, styleRows);

    // Pestanas Basico / Avanzado
    auto* tabs = kit::makeTabBar(scrollW, {"Basico", "Avanzado"}, m_tab,
        [this](int i) {
            m_tab = i;
            scheduleRebuild();
        });

    // Tarjeta: tamano
    auto* sizeCard = kit::makeCard(scrollW, "Tamano", {255, 200, 100}, {
        kit::makeSliderRow(innerW,
            "Tamano del puntero", "Que tan grande se ve sobre la barra.",
            cfg.iconScale, 0.10, 1.0,
            [](double v) { return fmt::format("x{:.2f}", v); },
            [this](double v) {
                CustomSliderManager::get().config().iconScale = static_cast<float>(v);
                refreshPreview();
                reapplyAllSliders();
            }),
    });

    // Tarjeta: animacion al arrastrar
    auto* animCard = kit::makeCard(scrollW, "Animacion al arrastrar", {255, 140, 220}, {
        kit::makeToggleRow(innerW,
            "Animar",
            "El puntero reacciona cuando lo arrastras.",
            cfg.animateOnDrag,
            [](bool v) { CustomSliderManager::get().config().animateOnDrag = v; }),
        kit::makeSelectRow(innerW,
            "Tipo", nullptr,
            {kAnimTypeNames[0], kAnimTypeNames[1], kAnimTypeNames[2], kAnimTypeNames[3]},
            static_cast<int>(cfg.animType),
            [](int v) { CustomSliderManager::get().config().animType = static_cast<SliderAnimType>(v); }),
        kit::makeSliderRow(innerW,
            "Duracion", "Cuanto dura la animacion.",
            cfg.animDuration, 0.05, 0.5,
            [](double v) { return fmt::format("{:.2f}s", v); },
            [](double v) { CustomSliderManager::get().config().animDuration = static_cast<float>(v); }),
        kit::makeSliderRow(innerW,
            "Rebote", "Cuanto crece el puntero al agarrarlo.",
            cfg.animBounceScale, 1.0, 2.0,
            [](double v) { return fmt::format("{:.0f}%", (v - 1.0) * 100.0); },
            [](double v) { CustomSliderManager::get().config().animBounceScale = static_cast<float>(v); }),
        kit::makeSliderRow(innerW,
            "Giro", "Cuantos grados gira al moverse.",
            cfg.animRotateDeg, 5.0, 45.0,
            [](double v) { return fmt::format("{:.0f}", v); },
            [](double v) { CustomSliderManager::get().config().animRotateDeg = static_cast<float>(v); }),
    });

    // Tarjeta: donde se aplica
    auto* targetsCard = kit::makeCard(scrollW, "Donde se aplica", {130, 240, 170}, {
        kit::makeToggleRow(innerW,
            "Opciones y volumen", "Barras del menu de opciones y del volumen.",
            cfg.targets.optionsSliders,
            [](bool v) { CustomSliderManager::get().config().targets.optionsSliders = v; }),
        kit::makeToggleRow(innerW,
            "Editor", "Barras dentro del editor de niveles.",
            cfg.targets.editorSliders,
            [](bool v) { CustomSliderManager::get().config().targets.editorSliders = v; }),
        kit::makeToggleRow(innerW,
            "Selector de color", "Barras de los selectores de color.",
            cfg.targets.colorSliders,
            [](bool v) { CustomSliderManager::get().config().targets.colorSliders = v; }),
        kit::makeToggleRow(innerW,
            "Garage", "Barras de la pantalla de iconos.",
            cfg.targets.garageSliders,
            [](bool v) { CustomSliderManager::get().config().targets.garageSliders = v; }),
    });

    std::vector<CCNode*> items = {hero, tabs};
    if (m_tab == 0) {
        items.push_back(styleCard);
        items.push_back(sizeCard);
        items.push_back(kit::makeHint(scrollW,
            "En Avanzado: animacion al arrastrar y en que barras se aplica."));
        // no usados en esta pestana
        animCard->removeAllChildren();
        targetsCard->removeAllChildren();
    } else {
        items.push_back(animCard);
        items.push_back(targetsCard);
        // no usados en esta pestana
        styleCard->removeAllChildren();
        sizeCard->removeAllChildren();
        m_shapeGridMenu = nullptr;
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 34.f});
    m_mainLayer->addChild(m_scroll);

    refreshPreview();
}

void CustomSliderPopup::rebuildShapeGrid() {
    if (!m_shapeGridMenu) return;
    m_shapeGridMenu->removeAllChildren();

    auto const& cfg = CustomSliderManager::get().config();
    auto shapes = getGeometricShapes();
    constexpr float kCell = 18.f;

    for (size_t i = 0; i < shapes.size(); i++) {
        auto const& shapeName = shapes[i].first;
        bool selected = (cfg.containerShape == shapeName);

        cocos2d::ccColor3B bgCol = selected ? ccColor3B{60, 160, 60} : ccColor3B{40, 40, 40};
        GLubyte bgAlpha = selected ? 220 : 140;
        auto* cellBg = paimon::SpriteHelper::createColorPanel(kCell, kCell, bgCol, bgAlpha, 3.f);
        cellBg->setContentSize({kCell, kCell});

        if (auto* icon = createShapeStencil(shapeName, kCell - 6.f)) {
            icon->setAnchorPoint({0.5f, 0.5f});
            icon->setPosition({kCell * 0.5f, kCell * 0.5f});
            cellBg->addChild(icon);
        }

        std::string shapeCopy = shapeName;
        auto* btn = CCMenuItemExt::createSpriteExtra(cellBg,
            [this, shapeCopy](CCMenuItemSpriteExtra*) {
                CustomSliderManager::get().config().containerShape = shapeCopy;
                rebuildShapeGrid();
                refreshPreview();
                reapplyAllSliders();
            });
        m_shapeGridMenu->addChild(btn);
    }
    m_shapeGridMenu->updateLayout();
}

void CustomSliderPopup::onPickImage() {
    WeakRef<CustomSliderPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto opt = std::move(result).unwrapOr(std::nullopt);
        if (!opt.has_value() || opt->empty()) return;
        auto path = *opt;
        auto& cfg = CustomSliderManager::get().config();
        auto dest = CustomSliderManager::get().imagesDir() / path.filename();
        std::error_code ec;
        std::filesystem::copy_file(path, dest, std::filesystem::copy_options::overwrite_existing, ec);
        cfg.customImagePath = ec ? geode::utils::string::pathToString(path) : geode::utils::string::pathToString(dest);
        auto* p = static_cast<CustomSliderPopup*>(popup.data());
        p->reapplyAllSliders();
        p->rebuild(); // refresca el nombre de archivo y la vista previa
    });
}

void CustomSliderPopup::refreshPreview() {
    if (!m_previewNode) return;
    m_previewNode->removeAllChildren();

    auto& cfg = CustomSliderManager::get().config();
    if (!cfg.enabled) {
        auto* lbl = CCLabelBMFont::create("OFF", "bigFont.fnt");
        lbl->setScale(0.3f); lbl->setColor({150, 150, 150});
        m_previewNode->addChild(lbl); return;
    }

    switch (cfg.thumbMode) {
        case SliderThumbMode::Image:
        case SliderThumbMode::Gif: {
            if (!cfg.customImagePath.empty()) {
                CCNode* raw = nullptr;
                if (cfg.thumbMode == SliderThumbMode::Gif) {
                    raw = AnimatedGIFSprite::create(cfg.customImagePath);
                }
                if (!raw) {
                    auto* tex = CCTextureCache::sharedTextureCache()->addImage(cfg.customImagePath.c_str(), false);
                    if (tex) raw = CCSprite::createWithTexture(tex);
                }
                if (!raw) {
                    auto* lbl = CCLabelBMFont::create("?", "bigFont.fnt");
                    lbl->setScale(0.5f); lbl->setColor({150, 150, 150});
                    m_previewNode->addChild(lbl); break;
                }

                if (cfg.containerEnabled) {
                constexpr float kSize = 28.f;
                    std::string shape = cfg.containerShape.empty() ? "circle" : cfg.containerShape;

                    auto* stencil = createShapeStencil(shape, kSize);
                    if (!stencil) stencil = createShapeStencil("circle", kSize);

                    auto* clipper = CCClippingNode::create();
                    clipper->setStencil(stencil);
                    clipper->setAlphaThreshold(-1.0f);
                    clipper->setContentSize({kSize, kSize});
                    clipper->setAnchorPoint({0.5f, 0.5f});
                    clipper->ignoreAnchorPointForPosition(false);
                    clipper->setPosition({0, 0});

                    float iw = std::max(raw->getContentWidth(), 1.f);
                    float ih = std::max(raw->getContentHeight(), 1.f);
                    raw->setScale(std::max(kSize / iw, kSize / ih));
                    raw->setAnchorPoint({0.5f, 0.5f});
                    raw->ignoreAnchorPointForPosition(false);
                    raw->setPosition({kSize / 2.f, kSize / 2.f});
                    clipper->addChild(raw);
                    m_previewNode->addChild(clipper);

                    if (cfg.containerBorderEnabled) {
                        float thick = std::clamp(cfg.containerBorderThickness, 0.5f, 8.f);
                        if (auto* border = createShapeBorder(shape, kSize + thick * 2.f, thick, cfg.containerBorderColor, 255)) {
                            border->setAnchorPoint({0.5f, 0.5f});
                            border->setPosition({0, 0});
                            m_previewNode->addChild(border, 5);
                        }
                    }
                } else {
                    float mx = std::max(raw->getContentSize().width, raw->getContentSize().height);
                    if (mx > 0.f) raw->setScale(28.f / mx);
                    m_previewNode->addChild(raw);
                }
                return;
            }
            auto* lbl = CCLabelBMFont::create("?", "bigFont.fnt");
            lbl->setScale(0.5f); lbl->setColor({150, 150, 150});
            m_previewNode->addChild(lbl); break;
        }
        default: {
            auto* gm = GameManager::get(); if (!gm) return;
            int iconId = cfg.usePlayerIcon ? getPlayerIconId(cfg.iconType) : cfg.customIconId;
            auto* player = SimplePlayer::create(iconId);
            if (!player) return;
            player->updatePlayerFrame(iconId, toGDIconType(cfg.iconType));
            if (cfg.usePlayerColors) {
                player->setColor(gm->colorForIdx(gm->getPlayerColor()));
                player->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
                if (gm->getPlayerGlow()) player->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
                else player->disableGlowOutline();
            } else {
                player->setColor(cfg.color1); player->setSecondColor(cfg.color2);
                if (cfg.enableGlow) player->setGlowOutline(cfg.color2); else player->disableGlowOutline();
            }
            player->setScale(std::min(cfg.iconScale * 1.5f, 1.0f));
            m_previewNode->addChild(player); break;
        }
    }
}

int CustomSliderPopup::getPlayerIconId(SliderIconType type) {
    auto* gm = GameManager::get();
    switch (type) {
        case SliderIconType::Cube:   return gm->getPlayerFrame();
        case SliderIconType::Ship:   return gm->getPlayerShip();
        case SliderIconType::Ball:   return gm->getPlayerBall();
        case SliderIconType::Ufo:    return gm->getPlayerBird();
        case SliderIconType::Wave:   return gm->getPlayerDart();
        case SliderIconType::Robot:  return gm->getPlayerRobot();
        case SliderIconType::Spider: return gm->getPlayerSpider();
        case SliderIconType::Swing:  return gm->getPlayerSwing();
    }
    return gm->getPlayerFrame();
}

IconType CustomSliderPopup::toGDIconType(SliderIconType type) {
    switch (type) {
        case SliderIconType::Cube:   return IconType::Cube;
        case SliderIconType::Ship:   return IconType::Ship;
        case SliderIconType::Ball:   return IconType::Ball;
        case SliderIconType::Ufo:    return IconType::Ufo;
        case SliderIconType::Wave:   return IconType::Wave;
        case SliderIconType::Robot:  return IconType::Robot;
        case SliderIconType::Spider: return IconType::Spider;
        case SliderIconType::Swing:  return IconType::Swing;
    }
    return IconType::Cube;
}

void CustomSliderPopup::reapplyAllSliders() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto& mgr = CustomSliderManager::get();
    if (!mgr.config().enabled) return;

    // Depth-limited traversal (sliders are never more than 8 levels deep)
    std::function<void(CCNode*, int)> findSliders = [&](CCNode* node, int depth) {
        if (!node || depth > 8) return;
        if (auto* slider = typeinfo_cast<Slider*>(node)) {
            if (auto* thumb = slider->getThumb()) {
                auto thumbSize = thumb->getContentSize();

                auto* normalBase = CCSprite::create();
                normalBase->setContentSize(thumbSize);
                auto* normalNode = CCSprite::create();
                normalNode->setScale(0.9f);
                normalBase->addChild(normalNode);
                normalNode->setPosition(thumbSize / 2.f);
                mgr.addIconToNode(normalNode, false);

                auto* selectedBase = CCSprite::create();
                selectedBase->setContentSize(thumbSize);
                auto* selectedNode = CCSprite::create();
                selectedNode->setScale(0.9f);
                selectedBase->addChild(selectedNode);
                selectedNode->setPosition(thumbSize / 2.f);
                mgr.addIconToNode(selectedNode, true);

                thumb->setNormalImage(normalBase);
                thumb->setSelectedImage(selectedBase);
            }
            return;
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                findSliders(child, depth + 1);
            }
        }
    };

    findSliders(scene, 0);
}
