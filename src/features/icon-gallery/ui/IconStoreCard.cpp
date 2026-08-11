#include "IconStoreCard.hpp"

#include "../services/GalleryStore.hpp"
#include "../../icon-maker/ui/IconMakerKit.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/ui/LoadingSpinner.hpp>

#include <algorithm>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_gallery {

namespace {

constexpr cocos2d::ccColor3B kAuthorColor = {166, 195, 235};
constexpr cocos2d::ccColor3B kBadgeColor = {255, 215, 110};

// Tag del interrogante que se pinta cuando el icono no se pudo bajar.
constexpr int kFailMarkTag = 77;

}  // anonymous namespace

IconStoreCard* IconStoreCard::create(std::string slug, float width, float height,
                                     std::function<void(std::string const&)> onPress) {
    auto* ret = new IconStoreCard();
    if (ret->init(std::move(slug), width, height, std::move(onPress))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool IconStoreCard::init(std::string slug, float width, float height,
                         std::function<void(std::string const&)> onPress) {
    if (!CCNodeRGBA::init()) return false;
    setCascadeOpacityEnabled(true);

    m_slug = std::move(slug);
    m_width = width;
    m_height = height;

    setAnchorPoint({0.f, 0.f});
    setContentSize({width, height});

    buildFrame();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    addChild(menu, 5);

    // Toda la tarjeta es el boton: mas facil de acertar que un botoncito.
    auto* hit = CCNode::create();
    hit->setAnchorPoint({0.5f, 0.5f});
    hit->setContentSize({width, height});
    auto slugCopy = m_slug;
    auto* btn = CCMenuItemExt::createSpriteExtra(hit,
        [slugCopy, onPress](CCMenuItemSpriteExtra*) {
            if (onPress) onPress(slugCopy);
        });
    btn->setPosition({width / 2.f, height / 2.f});
    menu->addChild(btn);

    refresh();
    return true;
}

void IconStoreCard::buildFrame() {
    if (auto* plate = kit::makePlate(m_width, m_height)) {
        plate->setPosition({0.f, 0.f});
        addChild(plate, -1);
    }

    float const boxSize = m_width - 18.f;
    float const boxCY = m_height - 10.f - boxSize / 2.f;

    // Hueco oscuro detras de la vista previa: da profundidad y deja claro
    // donde va el icono mientras carga.
    if (auto* well = paimon::SpriteHelper::createColorPanel(
            boxSize, boxSize, {0, 0, 0}, 110, 6.f)) {
        well->setAnchorPoint({0.f, 0.f});
        well->setPosition({9.f, boxCY - boxSize / 2.f});
        addChild(well);
    }

    m_previewBox = CCNode::create();
    m_previewBox->setContentSize({boxSize, boxSize});
    m_previewBox->setAnchorPoint({0.5f, 0.5f});
    m_previewBox->setPosition({m_width / 2.f, boxCY});
    addChild(m_previewBox, 1);

    m_name = CCLabelBMFont::create("", "bigFont.fnt");
    if (m_name) {
        m_name->setAnchorPoint({0.5f, 0.5f});
        m_name->setPosition({m_width / 2.f, 26.f});
        addChild(m_name, 2);
    }

    m_author = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_author) {
        m_author->setAnchorPoint({0.5f, 0.5f});
        m_author->setScale(0.36f);
        m_author->setColor(kAuthorColor);
        m_author->setPosition({m_width / 2.f, 12.f});
        addChild(m_author, 2);
    }

    // Chapa del gamemode, sobre la esquina superior izquierda.
    m_typeBadgeBg = paimon::SpriteHelper::createColorPanel(
        44.f, 13.f, {0, 0, 0}, 140, 4.f);
    if (m_typeBadgeBg) {
        m_typeBadgeBg->setAnchorPoint({0.f, 0.5f});
        m_typeBadgeBg->setPosition({7.f, m_height - 13.f});
        m_typeBadgeBg->setVisible(false);
        addChild(m_typeBadgeBg, 3);
    }
    m_typeBadge = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_typeBadge) {
        m_typeBadge->setAnchorPoint({0.5f, 0.5f});
        m_typeBadge->setScale(0.32f);
        m_typeBadge->setColor(kBadgeColor);
        m_typeBadge->setPosition({29.f, m_height - 13.f});
        m_typeBadge->setVisible(false);
        addChild(m_typeBadge, 4);
    }

    m_installedMark = paimon::SpriteHelper::safeCreateWithFrameName(
        "GJ_completesIcon_001.png");
    if (m_installedMark) {
        m_installedMark->setScale(0.5f);
        m_installedMark->setPosition({m_width - 12.f, m_height - 12.f});
        m_installedMark->setVisible(false);
        addChild(m_installedMark, 4);
    }
}

CCPoint IconStoreCard::boxCenter() const {
    if (!m_previewBox) return {0.f, 0.f};
    auto const size = m_previewBox->getContentSize();
    return {size.width / 2.f, size.height / 2.f};
}

void IconStoreCard::setLoading(bool loading) {
    if (loading) {
        if (m_spinner || !m_previewBox) return;
        m_spinner = LoadingSpinner::create(22.f);
        if (!m_spinner) return;
        m_spinner->setPosition(boxCenter());
        // Debajo de la vista previa: si por lo que sea sobrevive un frame de
        // mas, no tapa el icono.
        m_previewBox->addChild(m_spinner, 0);
        return;
    }
    if (m_spinner) {
        m_spinner->removeFromParent();
        m_spinner = nullptr;
    }
}

void IconStoreCard::showPreview(CCTexture2D* texture) {
    if (!texture || !m_previewBox || m_previewShown) return;

    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return;

    auto const size = sprite->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return;

    float const box = m_previewBox->getContentSize().width;
    float const targetScale = std::min(box / size.width, box / size.height);
    sprite->setPosition(boxCenter());
    m_previewBox->addChild(sprite, 1);

    m_preview = sprite;
    m_previewShown = true;

    // Entrada animada con sutil rebote estilo GD
    sprite->setScale(0.f);
    sprite->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, targetScale)));
}

void IconStoreCard::refresh() {
    auto& store = GalleryStore::get();
    auto const* icon = store.find(m_slug);
    if (!icon) return;

    if (m_name) {
        m_name->setString(icon->displayName().c_str());
        m_name->limitLabelWidth(m_width - 14.f, 0.34f, 0.13f);
    }

    if (!m_previewShown) {
        showPreview(store.previewTexture(m_slug));
    }
    setLoading(!m_previewShown && store.isLoading(m_slug));

    if (!m_previewShown && store.failed(m_slug) && m_previewBox &&
        !m_previewBox->getChildByTag(kFailMarkTag)) {
        // Sin vista previa: al menos que no quede un hueco negro y mudo.
        if (auto* mark = CCLabelBMFont::create("?", "bigFont.fnt")) {
            mark->setScale(0.6f);
            mark->setOpacity(90);
            mark->setPosition(boxCenter());
            mark->setTag(kFailMarkTag);
            m_previewBox->addChild(mark, 1);
        }
    }

    if (icon->metaLoaded && !m_metaShown) {
        m_metaShown = true;
        if (m_author && !icon->author.empty()) {
            m_author->setString(("by " + icon->author).c_str());
            m_author->limitLabelWidth(m_width - 12.f, 0.36f, 0.14f);
        }
        if (m_typeBadge && m_typeBadgeBg) {
            m_typeBadge->setString(iconTypeLabel(icon->type).c_str());
            m_typeBadge->limitLabelWidth(40.f, 0.32f, 0.2f);
            m_typeBadge->setVisible(true);
            m_typeBadgeBg->setVisible(true);
        }
    }

    if (m_installedMark) {
        bool const installed = store.isInstalled(m_slug);
        bool const wasVisible = m_installedMark->isVisible();
        m_installedMark->setVisible(installed);
        if (installed && !wasVisible) {
            m_installedMark->setScale(0.f);
            m_installedMark->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 0.5f)));
        }
    }
}

}  // namespace paimon::icon_gallery
