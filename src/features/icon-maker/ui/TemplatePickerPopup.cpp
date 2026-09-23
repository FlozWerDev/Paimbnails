#include "TemplatePickerPopup.hpp"

#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../services/IconThumbs.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 280.f;
constexpr int kCols = 8;
constexpr int kRows = 4;
constexpr int kPerPage = kCols * kRows;
constexpr float kSpacing = 42.f;

}  // anonymous namespace

TemplatePickerPopup* TemplatePickerPopup::create(IconType type, PickedCallback onPicked,
                                                 ProjectCallback onProject) {
    auto* p = new TemplatePickerPopup();
    if (p->init(type, std::move(onPicked), std::move(onProject))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

int TemplatePickerPopup::iconCount() const {
    auto* gm = GameManager::get();
    if (!gm) return 1;
    return std::max(gm->countForType(m_type), 1);
}

int TemplatePickerPopup::pageCount() const {
    int const total = m_mine
        ? static_cast<int>(m_projectIds.size()) : iconCount();
    return std::max(1, (total + kPerPage - 1) / kPerPage);
}

bool TemplatePickerPopup::init(IconType type, PickedCallback onPicked,
                               ProjectCallback onProject) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    m_type = type;
    m_onPicked = std::move(onPicked);
    m_onProject = std::move(onProject);
    setTitle("Elige una forma");
    setID("icon-maker-template-popup"_spr);

    auto const size = m_mainLayer->getContentSize();

    if (m_onProject) {
        IconProjectStore::get().loadIndex();
        for (auto const& entry : IconProjectStore::get().list()) {
            if (entry.type == m_type) m_projectIds.push_back(entry.id);
        }

        auto* tabs = kit::makeTabBar(size.width - 60.f,
            {"Oficiales", "Mis iconos"}, 0,
            [this](int index) {
                Ref<TemplatePickerPopup> self = this;
                Loader::get()->queueInMainThread([self, index] {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->m_mine = index == 1;
                    self->m_page = 0;
                    self->rebuildPage();
                });
            });
        if (tabs) {
            tabs->setPosition({30.f, size.height - 62.f});
            m_mainLayer->addChild(tabs, 3);
        }
    }

    float const gridCY = size.height / 2.f + (m_onProject ? -6.f : 4.f);

    m_gridArea = CCNode::create();
    m_gridArea->setPosition({size.width / 2.f, gridCY});
    m_mainLayer->addChild(m_gridArea);

    auto* navMenu = CCMenu::create();
    navMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(navMenu);

    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.7f);
        if (auto* prevBtn = CCMenuItemExt::createSpriteExtra(prevSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int const pages = pageCount();
                    m_page = (m_page - 1 + pages) % pages;
                    rebuildPage();
                })) {
            prevBtn->setPosition({22.f, gridCY});
            navMenu->addChild(prevBtn);
        }
    }
    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setScale(0.7f);
        nextSpr->setFlipX(true);
        if (auto* nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int const pages = pageCount();
                    m_page = (m_page + 1) % pages;
                    rebuildPage();
                })) {
            nextBtn->setPosition({size.width - 22.f, gridCY});
            navMenu->addChild(nextBtn);
        }
    }

    m_pageLabel = CCLabelBMFont::create("", "goldFont.fnt");
    if (m_pageLabel) {
        m_pageLabel->setScale(0.5f);
        m_pageLabel->setPosition({size.width / 2.f - 46.f, 19.f});
        m_mainLayer->addChild(m_pageLabel);
    }

    // Con 200 y pico iconos por gamemode, pasar paginas hasta el 137 no es
    // manera: se escribe el numero y ya.
    m_idInput = TextInput::create(70.f, "id", "bigFont.fnt");
    if (m_idInput) {
        m_idInput->setCommonFilter(CommonFilter::Uint);
        m_idInput->setMaxCharCount(4);
        m_idInput->setScale(0.6f);
        m_idInput->setPosition({size.width / 2.f + 46.f, 19.f});
        m_idInput->setCallback([this](std::string const& text) {
            if (text.empty()) return;
            jumpToId(utils::numFromString<int>(text).unwrapOr(0));
        });
        m_mainLayer->addChild(m_idInput);
    }

    rebuildPage();
    return true;
}

void TemplatePickerPopup::jumpToId(int iconId) {
    if (m_mine || iconId < 1) return;
    iconId = std::min(iconId, iconCount());
    m_page = (iconId - 1) / kPerPage;
    rebuildPage();
}

void TemplatePickerPopup::rebuildPage() {
    if (!m_gridArea) return;
    m_gridArea->removeAllChildren();

    int const pages = pageCount();
    m_page = std::clamp(m_page, 0, pages - 1);
    if (m_pageLabel) {
        m_pageLabel->setString(fmt::format("Pagina {}/{}", m_page + 1, pages).c_str());
    }
    if (m_idInput) m_idInput->setVisible(!m_mine);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_gridArea->addChild(menu);

    if (m_mine) {
        rebuildProjectPage(menu);
    } else {
        rebuildVanillaPage(menu);
    }
}

void TemplatePickerPopup::rebuildVanillaPage(CCMenu* menu) {
    int const total = iconCount();
    int const first = m_page * kPerPage + 1;
    float const originX = -kSpacing * (kCols - 1) / 2.f;
    float const originY = kSpacing * (kRows - 1) / 2.f;

    for (int i = 0; i < kPerPage; ++i) {
        int const iconId = first + i;
        if (iconId > total) break;

        auto* wrap = CCNode::create();
        wrap->setContentSize({36.f, 36.f});
        wrap->setAnchorPoint({0.5f, 0.5f});

        if (auto* player = SimplePlayer::create(1)) {
            player->updatePlayerFrame(iconId, m_type);
            player->setPosition({18.f, 20.f});
            player->setScale(0.8f);
            wrap->addChild(player);
        }

        auto* idLabel = CCLabelBMFont::create(
            fmt::format("{}", iconId).c_str(), "chatFont.fnt");
        idLabel->setScale(0.3f);
        idLabel->setColor(kit::kDescColor);
        idLabel->setPosition({18.f, 3.f});
        wrap->addChild(idLabel);

        auto* btn = CCMenuItemExt::createSpriteExtra(wrap,
            [this, iconId](CCMenuItemSpriteExtra*) {
                auto callback = m_onPicked;
                onClose(nullptr);
                if (callback) callback(iconId);
            });
        if (!btn) continue;
        btn->setPosition({
            originX + kSpacing * static_cast<float>(i % kCols),
            originY - kSpacing * static_cast<float>(i / kCols),
        });
        menu->addChild(btn);
    }
}

void TemplatePickerPopup::rebuildProjectPage(CCMenu* menu) {
    if (m_projectIds.empty()) {
        if (auto* empty = mkui::makeEmptyState(kPopupW - 90.f,
                "No tienes otros iconos de este tipo",
                "Cuando hagas mas iconos de este gamemode podras reutilizar "
                "sus formas desde aqui.")) {
            m_gridArea->addChild(empty);
        }
        return;
    }

    int const first = m_page * kPerPage;
    float const originX = -kSpacing * (kCols - 1) / 2.f;
    float const originY = kSpacing * (kRows - 1) / 2.f;

    for (int i = 0; i < kPerPage; ++i) {
        int const index = first + i;
        if (index >= static_cast<int>(m_projectIds.size())) break;
        auto const id = m_projectIds[static_cast<std::size_t>(index)];

        auto* wrap = CCNode::create();
        wrap->setContentSize({36.f, 36.f});
        wrap->setAnchorPoint({0.5f, 0.5f});

        Ref<CCNode> wrapRef = wrap;
        IconThumbs::get().request(id, [wrapRef](CCTexture2D* texture) {
            if (paimon::isRuntimeShuttingDown() || !wrapRef || !texture) return;
            auto* sprite = CCSprite::createWithTexture(texture);
            if (!sprite) return;
            float const longest = std::max(sprite->getContentSize().width,
                                           sprite->getContentSize().height);
            if (longest > 0.f) sprite->setScale(32.f / longest);
            sprite->setPosition({18.f, 18.f});
            wrapRef->addChild(sprite);
        });

        auto* btn = CCMenuItemExt::createSpriteExtra(wrap,
            [this, id](CCMenuItemSpriteExtra*) {
                auto callback = m_onProject;
                onClose(nullptr);
                if (callback) callback(id);
            });
        if (!btn) continue;
        btn->setPosition({
            originX + kSpacing * static_cast<float>(i % kCols),
            originY - kSpacing * static_cast<float>(i / kCols),
        });
        menu->addChild(btn);
    }
}

}  // namespace paimon::icon_maker
