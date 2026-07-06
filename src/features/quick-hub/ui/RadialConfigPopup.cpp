#include "RadialConfigPopup.hpp"
#include "../services/QuickHubManager.hpp"
#include "../data/QuickHubCategories.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::quickhub {

// Helpers

static RadialOptionDef const* findOptionById(
    std::vector<RadialOptionDef> const& allOpts,
    std::string const& id
) {
    for (auto const& opt : allOpts) {
        if (opt.id == id) return &opt;
    }
    return nullptr;
}

// Create

RadialConfigPopup* RadialConfigPopup::create() {
    auto ret = new RadialConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// Init

bool RadialConfigPopup::init() {
    if (!Popup::init(420.f, 280.f)) return false;
    paimon::markDynamicPopup(this);

    this->setTitle("Configurar Quick Hub");

    auto winSize = m_mainLayer->getContentSize();
    float cx = winSize.width / 2.f;

    // Subtitulo explicativo
    auto subtitle = CCLabelBMFont::create(
        "Elige que botones aparecen en el menu radial y en que orden.", "chatFont.fnt");
    subtitle->setScale(0.5f);
    subtitle->setColor({166, 176, 198});
    subtitle->setPosition({cx, winSize.height - 32.f});
    m_mainLayer->addChild(subtitle, 2);

    // Cargar configuracion actual como copia de trabajo
    m_activeIds = QuickHubManager::get().getActiveOptions();

    // Preview circular (lado izquierdo)
    m_previewNode = CCNode::create();
    m_previewNode->setPosition({cx - 80.f, winSize.height / 2.f + 5.f});
    m_previewNode->setContentSize({160.f, 160.f});
    m_previewNode->setAnchorPoint({0.5f, 0.5f});
    m_mainLayer->addChild(m_previewNode, 2);

    // Label "Vista previa"
    auto previewLabel = CCLabelBMFont::create("Vista previa", "goldFont.fnt");
    previewLabel->setScale(0.35f);
    previewLabel->setPosition({cx - 80.f, winSize.height - 48.f});
    m_mainLayer->addChild(previewLabel, 2);

    // Toggle: abrir el radial manteniendo Ctrl (debajo de la vista previa)
    auto* holdRow = paimon::configkit::makeToggleRow(190.f,
        "Abrir con Ctrl",
        "Manten Ctrl para abrirlo.",
        QuickHubManager::isHoldCtrlEnabled(),
        [](bool v) { QuickHubManager::setHoldCtrlEnabled(v); });
    holdRow->setPosition({cx - 175.f, 40.f});
    m_mainLayer->addChild(holdRow, 2);

    // Lista scrolleable (lado derecho)
    float listX = cx + 50.f;
    float listW = 150.f;
    float listH = winSize.height - 80.f;

    auto listBg = paimon::SpriteHelper::createDarkPanel(listW + 10.f, listH + 10.f, 80, 6.f);
    listBg->setPosition({listX - listW / 2.f - 5.f, 35.f});
    m_mainLayer->addChild(listBg, 0);

    m_scrollLayer = ScrollLayer::create({listW, listH});
    m_scrollLayer->setPosition({listX - listW / 2.f, 40.f});
    m_mainLayer->addChild(m_scrollLayer, 1);

    // Label "Botones del menu"
    auto listLabel = CCLabelBMFont::create("Botones del menu", "goldFont.fnt");
    listLabel->setScale(0.3f);
    listLabel->setPosition({listX, winSize.height - 48.f});
    m_mainLayer->addChild(listLabel, 2);

    // Botones inferiores
    auto saveSpr = ButtonSprite::create("Guardar", "goldFont.fnt", "GJ_button_01.png", .8f);
    saveSpr->setScale(0.6f);
    auto saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr, [this](CCMenuItemSpriteExtra*) {
        this->onSave(nullptr);
    });
    saveBtn->setPosition({cx - 40.f, -winSize.height / 2.f + 25.f});
    m_buttonMenu->addChild(saveBtn);

    auto resetSpr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_06.png", .8f);
    resetSpr->setScale(0.6f);
    auto resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr, [this](CCMenuItemSpriteExtra*) {
        this->onReset(nullptr);
    });
    resetBtn->setPosition({cx + 40.f, -winSize.height / 2.f + 25.f});
    m_buttonMenu->addChild(resetBtn);

    rebuildList();
    rebuildPreview();

    return true;
}

// Rebuild Preview

void RadialConfigPopup::rebuildPreview() {
    m_previewNode->removeAllChildren();

    // Circulo de fondo
    auto bgCircle = paimon::SpriteHelper::createRoundedRect(
        130.f, 130.f, 65.f,
        {0.1f, 0.1f, 0.15f, 0.85f},
        {0.4f, 0.4f, 0.5f, 0.6f},
        1.5f
    );
    if (bgCircle) {
        bgCircle->setPosition({-65.f, -65.f});
        m_previewNode->addChild(bgCircle, 0);
    }

    auto allOpts = QuickHubManager::get().getAllRadialOptions();
    int count = static_cast<int>(m_activeIds.size());
    if (count == 0) return;

    float radius = 48.f;
    float angleStep = 360.f / static_cast<float>(count);

    for (int i = 0; i < count; i++) {
        auto const* opt = findOptionById(allOpts, m_activeIds[i]);
        if (!opt) continue;

        float angleDeg = -90.f + angleStep * static_cast<float>(i);
        float angleRad = angleDeg * (static_cast<float>(M_PI) / 180.f);
        float x = cosf(angleRad) * radius;
        float y = sinf(angleRad) * radius;

        auto icon = CCSprite::createWithSpriteFrameName(opt->icon.c_str());
        if (!icon) {
            icon = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        }
        if (icon) {
            icon->setScale(0.38f);
            icon->setPosition({x, y});
            icon->setColor(opt->color);
            m_previewNode->addChild(icon, 1);
        }

        // Numero de posicion
        auto numLabel = CCLabelBMFont::create(
            fmt::format("{}", i + 1).c_str(), "chatFont.fnt");
        numLabel->setScale(0.45f);
        numLabel->setPosition({x, y - 14.f});
        numLabel->setColor({200, 200, 200});
        m_previewNode->addChild(numLabel, 2);
    }
}

// Rebuild List

void RadialConfigPopup::rebuildList() {
    auto* content = m_scrollLayer->m_contentLayer;
    content->removeAllChildren();

    float listW = m_scrollLayer->getContentSize().width;
    float rowH = 28.f;
    float gap = 3.f;

    // Calcular opciones no activas (disponibles para agregar)
    auto allOpts = QuickHubManager::get().getAllRadialOptions();
    std::vector<RadialOptionDef const*> available;
    for (auto const& opt : allOpts) {
        bool isActive = false;
        for (auto const& id : m_activeIds) {
            if (id == opt.id) { isActive = true; break; }
        }
        if (!isActive) available.push_back(&opt);
    }

    // Total de filas
    int activeCount = static_cast<int>(m_activeIds.size());
    int availCount = static_cast<int>(available.size());
    int totalRows = activeCount + (availCount > 0 ? 1 + availCount : 0);
    float totalH = totalRows * (rowH + gap) + 10.f;
    float contentH = std::max(totalH, m_scrollLayer->getContentSize().height);
    content->setContentSize({listW, contentH});

    float yPos = contentH - 5.f;

    // Opciones activas
    for (int i = 0; i < activeCount; i++) {
        auto const* opt = findOptionById(allOpts, m_activeIds[i]);
        if (!opt) continue;

        yPos -= rowH;

        auto row = CCNode::create();
        row->setContentSize({listW, rowH});
        row->setPosition({0.f, yPos});
        content->addChild(row);

        // Fondo de fila
        auto rowBg = paimon::SpriteHelper::createRoundedRect(
            listW - 4.f, rowH - 2.f, 4.f,
            {0.15f, 0.15f, 0.2f, 0.7f});
        if (rowBg) {
            rowBg->setPosition({2.f, 1.f});
            row->addChild(rowBg, 0);
        }

        // Numero de posicion
        auto posLabel = CCLabelBMFont::create(
            fmt::format("{}.", i + 1).c_str(), "chatFont.fnt");
        posLabel->setScale(0.5f);
        posLabel->setAnchorPoint({0.f, 0.5f});
        posLabel->setPosition({5.f, rowH / 2.f});
        posLabel->setColor(opt->color);
        row->addChild(posLabel, 1);

        // Icono
        auto icon = CCSprite::createWithSpriteFrameName(opt->icon.c_str());
        if (icon) {
            icon->setScale(0.3f);
            icon->setPosition({25.f, rowH / 2.f});
            icon->setColor(opt->color);
            row->addChild(icon, 1);
        }

        // Nombre
        auto nameLabel = CCLabelBMFont::create(opt->name.c_str(), "bigFont.fnt");
        nameLabel->setAnchorPoint({0.f, 0.5f});
        nameLabel->limitLabelWidth(listW - 38.f - 52.f, 0.26f, 0.1f);
        nameLabel->setPosition({38.f, rowH / 2.f});
        row->addChild(nameLabel, 1);

        // Menu para botones de la fila
        auto rowMenu = CCMenu::create();
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setContentSize({listW, rowH});
        row->addChild(rowMenu, 2);

        // Capturar indice para lambdas
        int idx = i;

        // Boton subir
        if (i > 0) {
            auto upSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
            if (upSpr) {
                upSpr->setScale(0.28f);
                upSpr->setRotation(90.f);
                auto upBtn = CCMenuItemExt::createSpriteExtra(upSpr, [this, idx](CCMenuItemSpriteExtra*) {
                    this->onMoveUp(idx);
                });
                upBtn->setPosition({listW - 42.f, rowH / 2.f});
                rowMenu->addChild(upBtn);
            }
        }

        // Boton bajar
        if (i < activeCount - 1) {
            auto downSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
            if (downSpr) {
                downSpr->setScale(0.28f);
                downSpr->setRotation(-90.f);
                auto downBtn = CCMenuItemExt::createSpriteExtra(downSpr, [this, idx](CCMenuItemSpriteExtra*) {
                    this->onMoveDown(idx);
                });
                downBtn->setPosition({listW - 28.f, rowH / 2.f});
                rowMenu->addChild(downBtn);
            }
        }

        // Boton quitar (X)
        auto removeSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (removeSpr) {
            removeSpr->setScale(0.35f);
            auto removeBtn = CCMenuItemExt::createSpriteExtra(removeSpr, [this, idx](CCMenuItemSpriteExtra*) {
                this->onRemoveOption(idx);
            });
            removeBtn->setPosition({listW - 12.f, rowH / 2.f});
            rowMenu->addChild(removeBtn);
        }

        yPos -= gap;
    }

    // Separador "Disponibles"
    if (availCount > 0) {
        yPos -= rowH;

        auto sepLabel = CCLabelBMFont::create("Toca + para anadir", "bigFont.fnt");
        sepLabel->setScale(0.22f);
        sepLabel->setColor({255, 222, 120});
        sepLabel->setPosition({listW / 2.f, yPos + rowH / 2.f});
        content->addChild(sepLabel, 1);

        yPos -= gap;

        // Opciones disponibles (no activas)
        for (int j = 0; j < availCount; j++) {
            auto const* opt = available[j];
            yPos -= rowH;

            auto row = CCNode::create();
            row->setContentSize({listW, rowH});
            row->setPosition({0.f, yPos});
            content->addChild(row);

            auto rowBg = paimon::SpriteHelper::createRoundedRect(
                listW - 4.f, rowH - 2.f, 4.f,
                {0.08f, 0.12f, 0.08f, 0.5f});
            if (rowBg) {
                rowBg->setPosition({2.f, 1.f});
                row->addChild(rowBg, 0);
            }

            // Icono
            auto icon = CCSprite::createWithSpriteFrameName(opt->icon.c_str());
            if (icon) {
                icon->setScale(0.3f);
                icon->setPosition({15.f, rowH / 2.f});
                icon->setColor(opt->color);
                row->addChild(icon, 1);
            }

            // Nombre
            auto nameLabel = CCLabelBMFont::create(opt->name.c_str(), "bigFont.fnt");
            nameLabel->setAnchorPoint({0.f, 0.5f});
            nameLabel->limitLabelWidth(listW - 30.f - 24.f, 0.26f, 0.1f);
            nameLabel->setPosition({30.f, rowH / 2.f});
            nameLabel->setColor({160, 160, 160});
            row->addChild(nameLabel, 1);

            // Menu para boton agregar
            auto rowMenu = CCMenu::create();
            rowMenu->setPosition({0.f, 0.f});
            rowMenu->setContentSize({listW, rowH});
            row->addChild(rowMenu, 2);

            int availIdx = j;

            // Boton agregar (+)
            if (static_cast<int>(m_activeIds.size()) < MAX_RADIAL_OPTIONS) {
                auto addSpr = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
                if (addSpr) {
                    addSpr->setScale(0.35f);
                    auto addBtn = CCMenuItemExt::createSpriteExtra(addSpr, [this, availIdx](CCMenuItemSpriteExtra*) {
                        this->onAddOption(availIdx);
                    });
                    addBtn->setPosition({listW - 15.f, rowH / 2.f});
                    rowMenu->addChild(addBtn);
                }
            } else {
                // Indicador de "lleno"
                auto fullLabel = CCLabelBMFont::create("MAX", "chatFont.fnt");
                fullLabel->setScale(0.35f);
                fullLabel->setColor({255, 100, 100});
                fullLabel->setPosition({listW - 15.f, rowH / 2.f});
                row->addChild(fullLabel, 2);
            }

            yPos -= gap;
        }
    }

    m_scrollLayer->moveToTop();
}

// Actions

void RadialConfigPopup::onMoveUp(int idx) {
    if (idx <= 0 || idx >= static_cast<int>(m_activeIds.size())) return;

    std::swap(m_activeIds[idx], m_activeIds[idx - 1]);
    rebuildList();
    rebuildPreview();
}

void RadialConfigPopup::onMoveDown(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_activeIds.size()) - 1) return;

    std::swap(m_activeIds[idx], m_activeIds[idx + 1]);
    rebuildList();
    rebuildPreview();
}

void RadialConfigPopup::onRemoveOption(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_activeIds.size())) return;

    // Minimo 1 opcion activa
    if (m_activeIds.size() <= 1) {
        PaimonNotify::create("Necesitas al menos 1 opcion.", NotificationIcon::Warning)->show();
        return;
    }

    m_activeIds.erase(m_activeIds.begin() + idx);
    rebuildList();
    rebuildPreview();
}

void RadialConfigPopup::onAddOption(int availIdx) {
    // Recalcular disponibles para obtener el ID correcto
    auto allOpts = QuickHubManager::get().getAllRadialOptions();
    std::vector<RadialOptionDef const*> available;
    for (auto const& opt : allOpts) {
        bool isActive = false;
        for (auto const& id : m_activeIds) {
            if (id == opt.id) { isActive = true; break; }
        }
        if (!isActive) available.push_back(&opt);
    }

    if (availIdx < 0 || availIdx >= static_cast<int>(available.size())) return;
    if (static_cast<int>(m_activeIds.size()) >= MAX_RADIAL_OPTIONS) {
        PaimonNotify::create(
            fmt::format("Maximo {} opciones.", MAX_RADIAL_OPTIONS).c_str(),
            NotificationIcon::Warning
        )->show();
        return;
    }

    m_activeIds.push_back(available[availIdx]->id);
    rebuildList();
    rebuildPreview();
}

void RadialConfigPopup::onSave(CCObject*) {
    QuickHubManager::get().setActiveOptions(m_activeIds);
    PaimonNotify::create("Quick Hub guardado!", NotificationIcon::Success)->show();
    this->keyBackClicked();
}

void RadialConfigPopup::onReset(CCObject*) {
    m_activeIds = getDefaultRadialOrder();
    rebuildList();
    rebuildPreview();
    PaimonNotify::create("Restaurado a valores por defecto.", NotificationIcon::Success)->show();
}

} // namespace paimon::quickhub
