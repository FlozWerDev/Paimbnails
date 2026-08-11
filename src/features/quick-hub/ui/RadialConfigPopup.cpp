#include "RadialConfigPopup.hpp"
#include "RadialVisuals.hpp"
#include "QuickButtonPopup.hpp"
#include "../services/QuickHubManager.hpp"
#include "../data/QuickHubCategories.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::quickhub {
namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 280.f;

constexpr float kPreviewSize = 148.f;
constexpr float kPreviewCx   = 104.f;
constexpr float kPreviewCy   = 158.f;

constexpr float kListX = 200.f;
constexpr float kListW = 222.f;
constexpr float kListY = 40.f;
constexpr float kListH = 160.f;

constexpr float kRowH = 30.f;
constexpr float kRowGap = 4.f;

RadialOptionDef const* findOptionById(
    std::vector<RadialOptionDef> const& allOpts,
    std::string const& id
) {
    auto found = std::ranges::find(allOpts, id, &RadialOptionDef::id);
    return found == allOpts.end() ? nullptr : &*found;
}

CCMenuItemSpriteExtra* makeIconButton(
    char const* frame, float scale, std::function<void()> onPress, float rotation = 0.f
) {
    auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frame);
    if (!spr) return nullptr;
    spr->setScale(scale);
    spr->setRotation(rotation);
    return CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(onPress)](CCMenuItemSpriteExtra*) {
        cb();
    });
}

} // namespace

RadialConfigPopup* RadialConfigPopup::create() {
    auto ret = new RadialConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RadialConfigPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    this->setTitle("Configurar Quick Hub");

    auto size = m_mainLayer->getContentSize();
    m_activeIds = QuickHubManager::get().getActiveOptions();

    auto* subtitle = CCLabelBMFont::create(
        "Click derecho en cualquier boton del juego para anadirlo aqui.",
        "chatFont.fnt");
    subtitle->setColor({166, 176, 198});
    subtitle->limitLabelWidth(size.width - 60.f, 0.45f, 0.2f);
    subtitle->setPosition({size.width / 2.f, size.height - 38.f});
    m_mainLayer->addChild(subtitle, 2);

    // --- Columna izquierda: vista previa de la rueda ------------------------
    if (auto* panel = paimon::SpriteHelper::createDarkPanel(
            kPreviewSize + 8.f, kPreviewSize + 8.f, 95, 8.f)) {
        panel->setPosition({kPreviewCx - kPreviewSize / 2.f - 4.f,
                            kPreviewCy - kPreviewSize / 2.f - 4.f});
        m_mainLayer->addChild(panel, 0);
    }

    m_previewNode = CCNode::create();
    m_previewNode->setPosition({kPreviewCx, kPreviewCy});
    m_mainLayer->addChild(m_previewNode, 1);

    m_countLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_countLabel->setScale(0.4f);
    m_countLabel->setColor({166, 176, 198});
    m_countLabel->setPosition({kPreviewCx, kPreviewCy - kPreviewSize / 2.f - 12.f});
    m_mainLayer->addChild(m_countLabel, 2);

    auto* holdRow = paimon::configkit::makeToggleRow(kPreviewSize + 8.f,
        "Abrir con Ctrl",
        "Manten Ctrl para abrirla.",
        QuickHubManager::isHoldCtrlEnabled(),
        [](bool v) { QuickHubManager::setHoldCtrlEnabled(v); });
    holdRow->setPosition({kPreviewCx - (kPreviewSize + 8.f) / 2.f, 30.f});
    m_mainLayer->addChild(holdRow, 2);

    // --- Columna derecha: pestanas + lista ---------------------------------
    auto* tabs = paimon::configkit::makeTabBar(kListW, {"Activos", "Anadir"}, m_tab,
        [this](int index) { this->setTab(index); });
    tabs->setPosition({kListX, kListY + kListH + 6.f});
    m_mainLayer->addChild(tabs, 2);

    if (auto* listBg = paimon::SpriteHelper::createDarkPanel(kListW + 8.f, kListH + 8.f, 95, 8.f)) {
        listBg->setPosition({kListX - 4.f, kListY - 4.f});
        m_mainLayer->addChild(listBg, 0);
    }

    m_scrollLayer = ScrollLayer::create({kListW, kListH});
    m_scrollLayer->setPosition({kListX, kListY});
    m_mainLayer->addChild(m_scrollLayer, 1);

    // --- Acciones ----------------------------------------------------------
    auto* resetSpr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_06.png", .8f);
    resetSpr->setScale(0.6f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr, [this](CCMenuItemSpriteExtra*) {
        this->onReset(nullptr);
    });
    m_buttonMenu->addChildAtPosition(resetBtn, Anchor::BottomRight, ccp(-138.f, 18.f));

    auto* saveSpr = ButtonSprite::create("Guardar", "goldFont.fnt", "GJ_button_01.png", .8f);
    saveSpr->setScale(0.6f);
    auto* saveBtn = CCMenuItemExt::createSpriteExtra(saveSpr, [this](CCMenuItemSpriteExtra*) {
        this->onSave(nullptr);
    });
    m_buttonMenu->addChildAtPosition(saveBtn, Anchor::BottomRight, ccp(-58.f, 18.f));

    rebuildList();
    rebuildPreview();

    return true;
}

void RadialConfigPopup::setTab(int tab) {
    if (m_tab == tab) return;
    m_tab = tab;
    rebuildList();
}

// -------------------------------------------------------------------------
// Vista previa
// -------------------------------------------------------------------------

void RadialConfigPopup::rebuildPreview() {
    m_previewNode->removeAllChildren();

    int count = static_cast<int>(m_activeIds.size());
    if (m_countLabel) {
        m_countLabel->setString(
            fmt::format("{} de {} botones", count, MAX_RADIAL_OPTIONS).c_str());
    }

    // Misma geometria que la rueda real, a escala.
    constexpr float kBadge = 24.f;
    float radius = count > 1 ? std::clamp((kBadge + 8.f) * count / (2.f * static_cast<float>(M_PI)),
                                          46.f, 62.f)
                             : 46.f;

    float hubRadius = std::max(18.f, radius - kBadge * 0.85f);
    if (auto* hub = makeCircle(hubRadius, kRadialHubFill)) {
        m_previewNode->addChild(hub, 1);
    }

    if (count == 0) {
        auto* empty = CCLabelBMFont::create("Vacio", "chatFont.fnt");
        empty->setScale(0.4f);
        empty->setColor({150, 155, 170});
        m_previewNode->addChild(empty, 3);
        return;
    }

    auto* logo = CCLabelBMFont::create("Quick\nHub", "chatFont.fnt");
    logo->setScale(0.36f);
    logo->setAlignment(kCCTextAlignmentCenter);
    logo->setColor({200, 208, 230});
    m_previewNode->addChild(logo, 3);

    auto allOpts = QuickHubManager::get().getAllRadialOptions();
    auto customButtons = QuickHubManager::get().getCustomButtons();

    for (int i = 0; i < count; i++) {
        auto const* opt = findOptionById(allOpts, m_activeIds[i]);
        if (!opt) continue;

        auto shape = RadialButtonShape::Circle;
        if (opt->custom) {
            auto saved = std::ranges::find(customButtons, opt->id, &CustomQuickButton::id);
            if (saved != customButtons.end()) shape = saved->shape;
        }

        float angleRad = radialAngleFor(i, count) * (static_cast<float>(M_PI) / 180.f);
        auto badge = makeRadialBadge(*opt, shape, kBadge);
        badge.root->setPosition({cosf(angleRad) * radius, sinf(angleRad) * radius});
        m_previewNode->addChild(badge.root, 4);

        auto* numLabel = CCLabelBMFont::create(fmt::format("{}", i + 1).c_str(), "chatFont.fnt");
        numLabel->setScale(0.32f);
        numLabel->setColor(opt->color);
        numLabel->setPosition({cosf(angleRad) * (radius + kBadge * 0.85f),
                               sinf(angleRad) * (radius + kBadge * 0.85f)});
        m_previewNode->addChild(numLabel, 5);
    }
}

// -------------------------------------------------------------------------
// Lista
// -------------------------------------------------------------------------

void RadialConfigPopup::rebuildList() {
    auto* content = m_scrollLayer->m_contentLayer;
    content->removeAllChildren();

    auto allOpts = QuickHubManager::get().getAllRadialOptions();

    // Cada pestana muestra un conjunto: los activos en su orden, o el resto.
    std::vector<RadialOptionDef const*> rows;
    if (m_tab == 0) {
        for (auto const& id : m_activeIds) {
            if (auto const* opt = findOptionById(allOpts, id)) rows.push_back(opt);
        }
    } else {
        for (auto const& opt : allOpts) {
            if (std::ranges::find(m_activeIds, opt.id) == m_activeIds.end()) rows.push_back(&opt);
        }
    }

    float totalH = static_cast<float>(rows.size()) * (kRowH + kRowGap) + 8.f;
    float contentH = std::max(totalH, kListH);
    content->setContentSize({kListW, contentH});

    if (rows.empty()) {
        auto* empty = CCLabelBMFont::create(
            m_tab == 0 ? "Anade botones desde la otra pestana"
                       : "Todo esta ya en la rueda",
            "chatFont.fnt");
        empty->setColor({150, 155, 170});
        empty->limitLabelWidth(kListW - 24.f, 0.42f, 0.2f);
        empty->setPosition({kListW / 2.f, contentH - 24.f});
        content->addChild(empty, 1);
        m_scrollLayer->moveToTop();
        return;
    }

    float yPos = contentH - 4.f;

    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
        auto const* opt = rows[static_cast<size_t>(i)];
        yPos -= kRowH;

        auto* row = CCNode::create();
        row->setContentSize({kListW, kRowH});
        row->setPosition({0.f, yPos});
        content->addChild(row);

        if (auto* rowBg = paimon::SpriteHelper::createRoundedRect(
                kListW - 4.f, kRowH - 2.f, 5.f,
                m_tab == 0 ? ccc4f(0.11f, 0.13f, 0.20f, 0.85f)
                           : ccc4f(0.09f, 0.11f, 0.15f, 0.7f),
                accentColor(opt->color, 0.35f), 1.f)) {
            rowBg->setPosition({2.f, 1.f});
            row->addChild(rowBg, 0);
        }

        float textX = 10.f;

        if (m_tab == 0) {
            auto* posLabel = CCLabelBMFont::create(
                fmt::format("{}", i + 1).c_str(), "goldFont.fnt");
            posLabel->setScale(0.36f);
            posLabel->setAnchorPoint({0.5f, 0.5f});
            posLabel->setPosition({14.f, kRowH / 2.f});
            row->addChild(posLabel, 1);
            textX = 26.f;
        }

        if (auto* icon = makeFittedIcon(opt->icon, 19.f)) {
            icon->setPosition({textX + 10.f, kRowH / 2.f});
            row->addChild(icon, 1);
        }

        auto* nameLabel = CCLabelBMFont::create(opt->name.c_str(), "bigFont.fnt");
        nameLabel->setAnchorPoint({0.f, 0.5f});
        nameLabel->setPosition({textX + 24.f, kRowH / 2.f});
        row->addChild(nameLabel, 1);

        auto* rowMenu = CCMenu::create();
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setContentSize({kListW, kRowH});
        row->addChild(rowMenu, 2);

        // Ancho libre para el nombre: depende de cuantos botones lleve la fila.
        float controlsW = 0.f;
        std::string id = opt->id;

        if (m_tab == 0) {
            int idx = i;
            if (i > 0) {
                if (auto* up = makeIconButton("GJ_arrow_03_001.png", 0.3f,
                                              [this, idx] { this->onMoveUp(idx); }, 90.f)) {
                    up->setPosition({kListW - 46.f, kRowH / 2.f});
                    rowMenu->addChild(up);
                }
            }
            if (i < static_cast<int>(rows.size()) - 1) {
                if (auto* down = makeIconButton("GJ_arrow_03_001.png", 0.3f,
                                                [this, idx] { this->onMoveDown(idx); }, -90.f)) {
                    down->setPosition({kListW - 30.f, kRowH / 2.f});
                    rowMenu->addChild(down);
                }
            }
            if (auto* remove = makeIconButton("GJ_deleteIcon_001.png", 0.36f,
                                              [this, idx] { this->onRemoveOption(idx); })) {
                remove->setPosition({kListW - 13.f, kRowH / 2.f});
                rowMenu->addChild(remove);
            }
            controlsW = 60.f;
        } else {
            float x = kListW - 13.f;
            if (static_cast<int>(m_activeIds.size()) < MAX_RADIAL_OPTIONS) {
                if (auto* add = makeIconButton("GJ_plusBtn_001.png", 0.36f,
                                               [this, id] { this->onAddOption(id); })) {
                    add->setPosition({x, kRowH / 2.f});
                    rowMenu->addChild(add);
                }
            } else {
                auto* full = CCLabelBMFont::create("MAX", "chatFont.fnt");
                full->setScale(0.34f);
                full->setColor({255, 110, 110});
                full->setPosition({x, kRowH / 2.f});
                row->addChild(full, 2);
            }
            x -= 20.f;
            controlsW = 26.f;

            // Los botones capturados se pueden reeditar y borrar del catalogo.
            if (opt->custom) {
                if (auto* edit = makeIconButton("GJ_optionsBtn_001.png", 0.3f,
                                                [this, id] { this->onEditCustom(id); })) {
                    edit->setPosition({x, kRowH / 2.f});
                    rowMenu->addChild(edit);
                    x -= 20.f;
                    controlsW += 20.f;
                }
                if (auto* del = makeIconButton("GJ_trashBtn_001.png", 0.28f,
                                               [this, id] { this->onDeleteCustom(id); })) {
                    del->setPosition({x, kRowH / 2.f});
                    rowMenu->addChild(del);
                    controlsW += 20.f;
                }
            }
        }

        nameLabel->limitLabelWidth(kListW - (textX + 24.f) - controlsW - 6.f, 0.34f, 0.14f);

        yPos -= kRowGap;
    }

    m_scrollLayer->moveToTop();
}

// -------------------------------------------------------------------------
// Acciones
// -------------------------------------------------------------------------

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

    if (m_activeIds.size() <= 1) {
        PaimonNotify::create("Necesitas al menos 1 opcion.", NotificationIcon::Warning)->show();
        return;
    }

    m_activeIds.erase(m_activeIds.begin() + idx);
    rebuildList();
    rebuildPreview();
}

void RadialConfigPopup::onAddOption(std::string const& id) {
    if (std::ranges::find(m_activeIds, id) != m_activeIds.end()) return;
    if (static_cast<int>(m_activeIds.size()) >= MAX_RADIAL_OPTIONS) {
        std::string message = fmt::format("Maximo {} opciones.", MAX_RADIAL_OPTIONS);
        PaimonNotify::create(message.c_str(), NotificationIcon::Warning)->show();
        return;
    }

    m_activeIds.push_back(id);
    rebuildList();
    rebuildPreview();
}

void RadialConfigPopup::onEditCustom(std::string const& id) {
    auto saved = QuickHubManager::get().getCustomButton(id);
    if (!saved) return;

    auto* popup = QuickButtonPopup::create(*saved);
    if (!popup) return;
    popup->setOnSaved([this](std::string const&) {
        rebuildList();
        rebuildPreview();
    });
    popup->show();
}

void RadialConfigPopup::onDeleteCustom(std::string const& id) {
    auto saved = QuickHubManager::get().getCustomButton(id);
    if (!saved) return;

    geode::createQuickPopup(
        "Borrar boton",
        fmt::format("Quitar <cy>{}</c> del Quick Hub para siempre?", saved->name),
        "Cancelar", "Borrar",
        [this, id](auto*, bool confirmed) {
            if (!confirmed) return;
            QuickHubManager::get().deleteCustomButton(id);
            std::erase(m_activeIds, id);
            rebuildList();
            rebuildPreview();
        });
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
