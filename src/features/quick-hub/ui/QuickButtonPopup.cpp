#include "QuickButtonPopup.hpp"

#include "RadialVisuals.hpp"
#include "../services/QuickHubManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <functional>
#include <vector>

using namespace geode::prelude;

namespace paimon::quickhub {
namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 250.f;

// Los frames se validan al construir la lista, asi que un texture pack que
// borre alguno simplemente lo omite en vez de dejar un hueco roto.
std::vector<const char*> const& curatedIconFrames() {
    static const std::vector<const char*> frames = {
        "GJ_optionsBtn_001.png", "GJ_hammerIcon_001.png", "GJ_infoBtn_001.png",
        "GJ_musicOnBtn_001.png", "GJ_paintBtn_001.png", "GJ_starBtn_001.png",
        "GJ_chatBtn_001.png", "GJ_replayBtn_001.png", "gj_heartOn_001.png",
        "GJ_searchBtn_001.png", "GJ_profileButton_001.png", "GJ_storeBtn_001.png",
        "GJ_menuBtn_001.png", "GJ_creatorBtn_001.png", "GJ_likeBtn_001.png",
        "GJ_plusBtn_001.png", "GJ_deleteIcon_001.png", "GJ_trashBtn_001.png",
        "GJ_updateBtn_001.png", "GJ_reportBtn_001.png", "GJ_starsIcon_001.png",
        "GJ_coinsIcon_001.png", "GJ_moonsIcon_001.png", "GJ_downloadsIcon_001.png",
        "GJ_timeIcon_001.png", "GJ_swordIcon_001.png", "GJ_shieldIcon_001.png",
        "GJ_demonIcon_001.png", "GJ_locked_001.png", "GJ_lockGray_001.png",
        "GJ_sFavIcon_001.png", "GJ_sTrendingIcon_001.png", "GJ_sMagicIcon_001.png",
        "GJ_filterIcon_001.png", "GJ_sortIcon_001.png", "GJ_arrow_03_001.png",
        "GJ_playBtn_001.png", "GJ_editBtn_001.png", "GJ_cancelDownloadBtn_001.png",
        "GJ_homeBtn_001.png", "GJ_gauntletBtn_001.png", "GJ_mpBtn_001.png",
    };
    return frames;
}

std::vector<cocos2d::ccColor3B> const& presetColors() {
    static const std::vector<cocos2d::ccColor3B> colors = {
        {120, 200, 255}, {120, 255, 150}, {255, 214, 110}, {255, 130, 140},
        {200, 150, 255}, {255, 165, 220}, {150, 240, 230}, {230, 235, 245},
    };
    return colors;
}

class IconPickerPopup : public Popup {
public:
    static IconPickerPopup* create(std::string current, std::function<void(std::string)> onPick) {
        auto* ret = new IconPickerPopup();
        ret->m_current = std::move(current);
        ret->m_onPick = std::move(onPick);
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    std::string m_current;
    std::function<void(std::string)> m_onPick;

    bool init() {
        if (!Popup::init(360.f, 250.f)) return false;
        paimon::markDynamicPopup(this);
        this->setTitle("Elegir icono");

        auto size = m_mainLayer->getContentSize();

        constexpr float listW = 300.f;
        constexpr float listH = 168.f;
        float listX = (size.width - listW) * 0.5f;
        float listY = 26.f;

        auto panel = paimon::SpriteHelper::createDarkPanel(listW + 10.f, listH + 10.f, 90, 6.f);
        if (panel) {
            panel->setPosition({listX - 5.f, listY - 5.f});
            m_mainLayer->addChild(panel, 0);
        }

        auto scroll = ScrollLayer::create({listW, listH});
        scroll->setPosition({listX, listY});
        m_mainLayer->addChild(scroll, 1);

        auto* content = scroll->m_contentLayer;

        constexpr int cols = 5;
        constexpr float cell = listW / cols; // 60
        const float iconBox = cell - 20.f;

        // El icono capturado va primero: casi siempre es el que se quiere.
        std::vector<std::string> valid;
        if (!m_current.empty() && paimon::SpriteHelper::safeCreateWithFrameName(m_current.c_str())) {
            valid.push_back(m_current);
        }
        for (auto const* frame : curatedIconFrames()) {
            if (frame == m_current) continue;
            if (paimon::SpriteHelper::safeCreateWithFrameName(frame)) valid.emplace_back(frame);
        }

        int count = static_cast<int>(valid.size());
        int rows = (count + cols - 1) / cols;
        float contentH = std::max(static_cast<float>(rows) * cell, listH);
        content->setContentSize({listW, contentH});

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize({listW, contentH});
        content->addChild(menu);

        for (int i = 0; i < count; ++i) {
            int col = i % cols;
            int row = i / cols;
            float x = col * cell + cell * 0.5f;
            float y = contentH - (row * cell + cell * 0.5f);
            std::string frame = valid[static_cast<size_t>(i)];
            bool isCurrent = (frame == m_current);

            auto holder = CCNode::create();
            holder->setContentSize({cell, cell});

            auto card = paimon::SpriteHelper::createRoundedRect(
                cell - 6.f, cell - 6.f, 6.f,
                isCurrent ? ccc4f(0.18f, 0.42f, 0.55f, 0.95f)
                          : ccc4f(0.10f, 0.12f, 0.16f, 0.85f),
                isCurrent ? ccc4f(0.45f, 0.85f, 1.f, 1.f)
                          : ccc4f(0.30f, 0.35f, 0.45f, 0.7f),
                1.2f);
            if (card) {
                // createRoundedRect draws from its origin (0,0) to (w,h), so
                // center the (cell-6) card inside the cell box: (cell-(cell-6))/2 = 3.
                card->setPosition({3.f, 3.f});
                holder->addChild(card, 0);
            }

            if (auto* icon = makeFittedIcon(frame, iconBox)) {
                icon->setPosition({cell * 0.5f, cell * 0.5f});
                holder->addChild(icon, 1);
            }

            auto item = CCMenuItemExt::createSpriteExtra(holder, [this, frame](CCMenuItemSpriteExtra*) {
                if (m_onPick) m_onPick(frame);
                this->keyBackClicked();
            });
            item->setPosition({x, y});
            menu->addChild(item);
        }

        scroll->moveToTop();
        return true;
    }
};

} // namespace

QuickButtonPopup* QuickButtonPopup::s_instance = nullptr;

bool QuickButtonPopup::isOpen() {
    return s_instance != nullptr;
}

QuickButtonPopup* QuickButtonPopup::create(CustomQuickButton candidate) {
    auto ret = new QuickButtonPopup();
    ret->m_candidate = std::move(candidate);
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool QuickButtonPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    s_instance = this;

    m_editing = QuickHubManager::get().getCustomButton(m_candidate.id).has_value();
    this->setTitle(m_editing ? "Editar boton rapido" : "Anadir al Quick Hub");

    // --- Columna izquierda: vista previa viva -------------------------------
    constexpr float kPreviewCx = 78.f;
    constexpr float kPreviewCy = 158.f;

    if (auto* panel = paimon::SpriteHelper::createDarkPanel(104.f, 104.f, 95, 8.f)) {
        panel->setPosition({kPreviewCx - 52.f, kPreviewCy - 52.f});
        m_mainLayer->addChild(panel, 0);
    }

    m_preview = CCNode::create();
    m_preview->setPosition({kPreviewCx, kPreviewCy});
    m_mainLayer->addChild(m_preview, 2);

    auto* iconSpr = ButtonSprite::create("Icono", "bigFont.fnt", "GJ_button_04.png", .8f);
    iconSpr->setScale(0.5f);
    auto* iconButton = CCMenuItemExt::createSpriteExtra(iconSpr, [this](CCMenuItemSpriteExtra* s) {
        this->onChangeIcon(s);
    });
    iconButton->setPosition({kPreviewCx, kPreviewCy - 68.f});
    m_buttonMenu->addChild(iconButton);

    // --- Columna derecha: nombre, forma, color ------------------------------
    constexpr float kFieldX = 150.f;
    constexpr float kFieldW = 226.f;

    auto addFieldLabel = [&](char const* text, float y) {
        auto* label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.34f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({kFieldX, y});
        m_mainLayer->addChild(label, 2);
    };

    addFieldLabel("Nombre", 205.f);

    m_nameInput = TextInput::create(kFieldW / 0.8f, "Nombre del acceso", "chatFont.fnt");
    m_nameInput->setCommonFilter(CommonFilter::Any);
    m_nameInput->setMaxCharCount(32);
    m_nameInput->setString(m_candidate.name);
    m_nameInput->setScale(0.8f);
    m_nameInput->setPosition({kFieldX + kFieldW / 2.f, 185.f});
    m_mainLayer->addChild(m_nameInput, 2);

    addFieldLabel("Forma", 157.f);

    // Los menus centran sus hijos en su propia y: separarlos de la etiqueta lo
    // justo para que los botones no la tapen.
    m_shapeMenu = CCMenu::create();
    m_shapeMenu->setPosition({kFieldX, 134.f});
    m_shapeMenu->setContentSize({kFieldW, 26.f});
    m_mainLayer->addChild(m_shapeMenu, 2);

    addFieldLabel("Color", 105.f);

    m_colorMenu = CCMenu::create();
    m_colorMenu->setPosition({kFieldX, 84.f});
    m_colorMenu->setContentSize({kFieldW, 22.f});
    m_mainLayer->addChild(m_colorMenu, 2);

    buildTargetInfo();

    auto* saveSprite = ButtonSprite::create("Guardar", "goldFont.fnt", "GJ_button_01.png", .8f);
    saveSprite->setScale(0.66f);
    auto* saveButton = CCMenuItemExt::createSpriteExtra(saveSprite, [this](CCMenuItemSpriteExtra*) {
        this->onSave(nullptr);
    });
    m_buttonMenu->addChildAtPosition(saveButton, Anchor::BottomRight, ccp(-58.f, 22.f));

    rebuildPreview();
    rebuildShapeButtons();
    rebuildColorSwatches();
    return true;
}

void QuickButtonPopup::onExit() {
    if (s_instance == this) s_instance = nullptr;
    Popup::onExit();
}

// Lo que se guardo del boton original, para que se vea que el acceso apunta
// a algo concreto y no a "un boton cualquiera".
void QuickButtonPopup::buildTargetInfo() {
    // La tarjeta deja libre la esquina donde va el boton Guardar.
    constexpr float kCardX = 22.f;
    constexpr float kCardW = 278.f;
    constexpr float kTextW = kCardW - 16.f;

    if (auto* card = paimon::SpriteHelper::createDarkPanel(kCardW, 52.f, 90, 6.f)) {
        card->setPosition({kCardX, 16.f});
        m_mainLayer->addChild(card, 0);
    }

    std::string target = !m_candidate.targetNodeId.empty() ? m_candidate.targetNodeId
                       : !m_candidate.labelText.empty()    ? m_candidate.labelText
                       : !m_candidate.listenerClass.empty()
                            ? fmt::format("{} (sin id)", m_candidate.listenerClass)
                            : "sin identificador";

    bool navigable = isNavigableScreen(m_candidate.sceneClass);

    auto addLine = [&](std::string const& text, ccColor3B color, float y, float scale) {
        auto* label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(kTextW, scale, 0.16f);
        label->setPosition({kCardX + 8.f, y});
        label->setColor(color);
        m_mainLayer->addChild(label, 2);
    };

    std::string screen = friendlyScreenName(
        m_candidate.ownerClass.empty() ? m_candidate.sceneClass : m_candidate.ownerClass);

    addLine(fmt::format("Pantalla: {}", screen), {190, 200, 220}, navigable ? 56.f : 50.f, 0.42f);
    addLine(fmt::format("Boton: {}", target), {150, 160, 185}, navigable ? 42.f : 34.f, 0.42f);
    if (navigable) {
        addLine("El radial ira a esa pantalla si no estas en ella.", {140, 220, 160}, 27.f, 0.36f);
    }
}

void QuickButtonPopup::setShape(RadialButtonShape shape) {
    m_candidate.shape = shape;
    rebuildPreview();
    rebuildShapeButtons();
}

void QuickButtonPopup::setIcon(std::string frame) {
    m_candidate.icon = std::move(frame);
    rebuildPreview();
}

void QuickButtonPopup::setColor(cocos2d::ccColor3B color) {
    m_candidate.color = color;
    rebuildPreview();
    rebuildColorSwatches();
}

void QuickButtonPopup::onChangeIcon(CCObject*) {
    if (auto* picker = IconPickerPopup::create(m_candidate.icon, [this](std::string frame) {
            this->setIcon(std::move(frame));
        })) {
        picker->show();
    }
}

void QuickButtonPopup::rebuildShapeButtons() {
    if (!m_shapeMenu) return;
    m_shapeMenu->removeAllChildren();

    struct Option { char const* label; RadialButtonShape shape; };
    static const Option options[] = {
        {"Circulo",  RadialButtonShape::Circle},
        {"Cuadrado", RadialButtonShape::Square},
        {"Suelto",   RadialButtonShape::Icon},
    };

    float x = 0.f;
    for (auto const& option : options) {
        bool selected = m_candidate.shape == option.shape;
        auto* spr = ButtonSprite::create(
            option.label, "bigFont.fnt",
            selected ? "GJ_button_02.png" : "GJ_button_04.png", .8f);
        spr->setScale(0.46f);

        auto shape = option.shape;
        auto* button = CCMenuItemExt::createSpriteExtra(spr, [this, shape](CCMenuItemSpriteExtra*) {
            this->setShape(shape);
        });
        float width = button->getScaledContentSize().width;
        button->setPosition({x + width / 2.f, 0.f});
        m_shapeMenu->addChild(button);
        x += width + 6.f;
    }
}

void QuickButtonPopup::rebuildColorSwatches() {
    if (!m_colorMenu) return;
    m_colorMenu->removeAllChildren();

    constexpr float kSwatch = 20.f;
    constexpr float kGap = 6.f;

    float x = kSwatch / 2.f;
    for (auto const& color : presetColors()) {
        bool selected = color.r == m_candidate.color.r &&
                        color.g == m_candidate.color.g &&
                        color.b == m_candidate.color.b;

        auto* holder = CCNode::create();
        holder->setContentSize({kSwatch, kSwatch});

        if (auto* chip = paimon::SpriteHelper::createRoundedRect(
                kSwatch, kSwatch, 5.f, accentColor(color, 0.9f),
                selected ? ccc4f(1.f, 1.f, 1.f, 1.f) : ccc4f(0.f, 0.f, 0.f, 0.45f),
                selected ? 2.f : 1.f)) {
            holder->addChild(chip);
        }

        auto* button = CCMenuItemExt::createSpriteExtra(holder, [this, color](CCMenuItemSpriteExtra*) {
            this->setColor(color);
        });
        button->setPosition({x, 0.f});
        m_colorMenu->addChild(button);
        x += kSwatch + kGap;
    }
}

void QuickButtonPopup::rebuildPreview() {
    if (!m_preview) return;
    m_preview->removeAllChildren();

    RadialOptionDef def;
    def.id = m_candidate.id;
    def.name = m_candidate.name;
    def.icon = m_candidate.icon.empty() ? "GJ_optionsBtn_001.png" : m_candidate.icon;
    def.color = m_candidate.color;

    auto badge = makeRadialBadge(def, m_candidate.shape, 58.f);
    // En la vista previa el aro es el punto: se ensena siempre.
    if (badge.ring) badge.ring->setVisible(true);
    m_preview->addChild(badge.root);
}

void QuickButtonPopup::onSave(CCObject*) {
    std::string name = m_nameInput ? std::string(m_nameInput->getString()) : std::string();
    if (name.empty()) {
        PaimonNotify::create("Ponle un nombre al acceso.", NotificationIcon::Warning)->show();
        return;
    }

    // El id se deriva del nombre solo al crearlo; al editar se conserva para no
    // romper el orden guardado ni los accesos ya colocados en la rueda.
    if (m_candidate.id.empty()) {
        m_candidate.id = QuickHubManager::get().makeUniqueCustomId(name);
    }

    auto activeOptions = QuickHubManager::get().getActiveOptions();
    bool const isActive = std::ranges::find(activeOptions, m_candidate.id) != activeOptions.end();
    if (!isActive && static_cast<int>(activeOptions.size()) >= MAX_RADIAL_OPTIONS) {
        std::string message =
            fmt::format("El Quick Hub admite hasta {} botones.", MAX_RADIAL_OPTIONS);
        PaimonNotify::create(message.c_str(), NotificationIcon::Warning)->show();
        return;
    }

    m_candidate.name = std::move(name);
    if (!QuickHubManager::get().saveCustomButton(m_candidate)) {
        PaimonNotify::create("No se pudo guardar el boton.", NotificationIcon::Error)->show();
        return;
    }

    if (!isActive) {
        activeOptions.push_back(m_candidate.id);
        QuickHubManager::get().setActiveOptions(activeOptions);
    }

    if (m_onSaved) m_onSaved(m_candidate.id);

    PaimonNotify::create(
        m_editing ? "Boton actualizado!" : "Boton anadido al Quick Hub!",
        NotificationIcon::Success)->show();
    this->keyBackClicked();
}

} // namespace paimon::quickhub
