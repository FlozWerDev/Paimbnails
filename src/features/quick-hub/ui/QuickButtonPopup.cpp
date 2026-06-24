#include "QuickButtonPopup.hpp"

#include "../services/QuickHubManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/ui/ScrollLayer.hpp>

#include <functional>
#include <vector>

using namespace geode::prelude;

namespace paimon::quickhub {
namespace {
// automatically, so this stays safe across game versions / texture packs.
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
        const float iconBox = cell - 14.f;
        std::vector<std::string> valid;
        for (auto const* frame : curatedIconFrames()) {
            if (paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
                valid.emplace_back(frame);
            }
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
            std::string frame = valid[i];
            bool isCurrent = (frame == m_current);

            auto holder = CCNode::create();
            holder->setContentSize({cell, cell});
            holder->setAnchorPoint({0.5f, 0.5f});

            auto card = paimon::SpriteHelper::createRoundedRect(
                cell - 6.f, cell - 6.f, 6.f,
                isCurrent ? ccc4f(0.18f, 0.42f, 0.55f, 0.95f)
                          : ccc4f(0.10f, 0.12f, 0.16f, 0.85f),
                isCurrent ? ccc4f(0.45f, 0.85f, 1.f, 1.f)
                          : ccc4f(0.30f, 0.35f, 0.45f, 0.7f),
                1.2f);
            if (card) {
                card->setPosition({-(cell - 6.f) * 0.5f, -(cell - 6.f) * 0.5f});
                holder->addChild(card, 0);
            }

            auto icon = paimon::SpriteHelper::safeCreateWithFrameName(frame.c_str());
            if (icon) {
                auto cs = icon->getContentSize();
                float longest = std::max(cs.width, cs.height);
                icon->setScale(longest > 0.f ? std::min(1.f, iconBox / longest) : 0.6f);
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
    if (!Popup::init(380.f, 260.f)) return false;
    s_instance = this;
    this->setTitle("Anadir a Hold Control");

    auto size = m_mainLayer->getContentSize();
    const float cx = size.width * 0.5f;
    m_preview = CCNode::create();
    m_preview->setPosition({cx, 196.f});
    m_mainLayer->addChild(m_preview, 2);
    auto iconLabel = CCLabelBMFont::create("Icono", "goldFont.fnt");
    iconLabel->setScale(0.34f);
    iconLabel->setPosition({cx + 92.f, 224.f});
    m_mainLayer->addChild(iconLabel, 2);

    auto iconSpr = ButtonSprite::create("Cambiar", "bigFont.fnt", "GJ_button_04.png", .8f);
    iconSpr->setScale(0.6f);
    m_iconButton = CCMenuItemExt::createSpriteExtra(iconSpr, [this](CCMenuItemSpriteExtra* s) {
        this->onChangeIcon(s);
    });
    m_buttonMenu->addChildAtPosition(m_iconButton, Anchor::Top, ccp(92.f, -64.f));
    auto shapeLabel = CCLabelBMFont::create("Forma", "goldFont.fnt");
    shapeLabel->setScale(0.34f);
    shapeLabel->setPosition({cx, 170.f});
    m_mainLayer->addChild(shapeLabel, 2);

    auto circleSprite = ButtonSprite::create("Circular", "bigFont.fnt", "GJ_button_01.png", .8f);
    circleSprite->setScale(0.5f);
    m_circleButton = CCMenuItemExt::createSpriteExtra(circleSprite, [this](CCMenuItemSpriteExtra*) {
        setShape(RadialButtonShape::Circle);
    });
    m_buttonMenu->addChildAtPosition(m_circleButton, Anchor::Top, ccp(-58.f, -112.f));

    auto squareSprite = ButtonSprite::create("Cuadrado", "bigFont.fnt", "GJ_button_01.png", .8f);
    squareSprite->setScale(0.5f);
    m_squareButton = CCMenuItemExt::createSpriteExtra(squareSprite, [this](CCMenuItemSpriteExtra*) {
        setShape(RadialButtonShape::Square);
    });
    m_buttonMenu->addChildAtPosition(m_squareButton, Anchor::Top, ccp(58.f, -112.f));
    auto nameLabel = CCLabelBMFont::create("Nombre", "bigFont.fnt");
    nameLabel->setScale(0.32f);
    nameLabel->setAnchorPoint({1.f, 0.5f});
    nameLabel->setPosition({96.f, 116.f});
    m_mainLayer->addChild(nameLabel, 2);

    m_nameInput = TextInput::create(200.f, "Nombre del acceso", "chatFont.fnt");
    m_nameInput->setCommonFilter(CommonFilter::Any);
    m_nameInput->setMaxCharCount(32);
    m_nameInput->setString(m_candidate.name);
    m_nameInput->setPosition({236.f, 116.f});
    m_nameInput->setScale(0.72f);
    m_mainLayer->addChild(m_nameInput, 2);
    auto idLabel = CCLabelBMFont::create("ID", "bigFont.fnt");
    idLabel->setScale(0.32f);
    idLabel->setAnchorPoint({1.f, 0.5f});
    idLabel->setPosition({96.f, 82.f});
    m_mainLayer->addChild(idLabel, 2);

    m_idInput = TextInput::create(200.f, "custom:mi-boton", "chatFont.fnt");
    m_idInput->setCommonFilter(CommonFilter::Any);
    m_idInput->setMaxCharCount(48);
    m_idInput->setString(m_candidate.id);
    m_idInput->setPosition({236.f, 82.f});
    m_idInput->setScale(0.72f);
    m_mainLayer->addChild(m_idInput, 2);
    auto saveSprite = ButtonSprite::create("Guardar", "goldFont.fnt", "GJ_button_01.png", .8f);
    saveSprite->setScale(0.7f);
    auto saveButton = CCMenuItemExt::createSpriteExtra(saveSprite, [this](CCMenuItemSpriteExtra*) {
        this->onSave(nullptr);
    });
    m_buttonMenu->addChildAtPosition(saveButton, Anchor::Bottom, ccp(0.f, 26.f));

    rebuildPreview();
    updateShapeButtons();
    return true;
}

void QuickButtonPopup::onExit() {
    if (s_instance == this) s_instance = nullptr;
    Popup::onExit();
}

void QuickButtonPopup::setShape(RadialButtonShape shape) {
    m_candidate.shape = shape;
    rebuildPreview();
    updateShapeButtons();
}

void QuickButtonPopup::setIcon(std::string frame) {
    m_candidate.icon = std::move(frame);
    rebuildPreview();
}

void QuickButtonPopup::onChangeIcon(CCObject*) {
    if (auto* picker = IconPickerPopup::create(m_candidate.icon, [this](std::string frame) {
            this->setIcon(std::move(frame));
        })) {
        picker->show();
    }
}

void QuickButtonPopup::updateShapeButtons() {
    if (m_circleButton) {
        if (auto* sprite = typeinfo_cast<CCSprite*>(m_circleButton->getNormalImage())) {
            sprite->setOpacity(m_candidate.shape == RadialButtonShape::Circle ? 255 : 120);
        }
    }
    if (m_squareButton) {
        if (auto* sprite = typeinfo_cast<CCSprite*>(m_squareButton->getNormalImage())) {
            sprite->setOpacity(m_candidate.shape == RadialButtonShape::Square ? 255 : 120);
        }
    }
}

void QuickButtonPopup::rebuildPreview() {
    if (!m_preview) return;
    m_preview->removeAllChildren();

    constexpr float size = 56.f;
    float radius = m_candidate.shape == RadialButtonShape::Circle ? size * 0.5f : 7.f;
    auto card = paimon::SpriteHelper::createRoundedRect(
        size, size, radius,
        {0.04f, 0.05f, 0.08f, 0.92f},
        {0.25f, 0.65f, 0.9f, 0.95f},
        1.5f);
    if (card) {
        card->setPosition({-size * 0.5f, -size * 0.5f});
        m_preview->addChild(card, 0);
    }

    auto icon = CCSprite::createWithSpriteFrameName(
        (m_candidate.icon.empty() ? "GJ_optionsBtn_001.png" : m_candidate.icon).c_str());
    if (icon) {
        auto content = icon->getContentSize();
        float longest = std::max(content.width, content.height);
        icon->setScale(longest > 0.f ? std::min(1.f, 38.f / longest) : 0.6f);
        m_preview->addChild(icon, 1);
    }
}

void QuickButtonPopup::onSave(CCObject*) {
    auto name = m_nameInput ? m_nameInput->getString() : std::string();
    auto id = m_idInput ? m_idInput->getString() : std::string();
    if (name.empty() || id.empty()) {
        PaimonNotify::create("Completa el nombre y el ID.", NotificationIcon::Warning)->show();
        return;
    }

    if (!id.starts_with("custom:")) id = "custom:" + id;
    auto activeOptions = QuickHubManager::get().getActiveOptions();
    bool const isActive = std::ranges::find(activeOptions, id) != activeOptions.end();
    if (!isActive && static_cast<int>(activeOptions.size()) >= MAX_RADIAL_OPTIONS) {
        PaimonNotify::create(
            fmt::format("Hold Control admite hasta {} botones.", MAX_RADIAL_OPTIONS).c_str(),
            NotificationIcon::Warning)->show();
        return;
    }

    m_candidate.name = std::move(name);
    m_candidate.id = std::move(id);
    if (!QuickHubManager::get().saveCustomButton(m_candidate)) {
        PaimonNotify::create("No se pudo guardar el boton.", NotificationIcon::Error)->show();
        return;
    }

    if (!isActive) {
        activeOptions.push_back(m_candidate.id);
        QuickHubManager::get().setActiveOptions(activeOptions);
    }

    PaimonNotify::create("Boton anadido a Hold Control!", NotificationIcon::Success)->show();
    this->keyBackClicked();
}

} // namespace paimon::quickhub
