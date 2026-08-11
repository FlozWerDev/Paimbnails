#include "CursorShopDetailPopup.hpp"
#include "../services/CursorManager.hpp"
#include "../services/CursorShopImages.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/CursorIcoDecoder.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::cursorshop;

namespace {

namespace kit = paimon::configkit;

constexpr float kPopupWidth  = 420.f;
constexpr float kPopupHeight = 285.f;
constexpr float kCellSize    = 44.f;
constexpr float kCellGap     = 6.f;

constexpr std::array<CursorState, CURSOR_STATE_COUNT> kStates = {
    CursorState::Idle, CursorState::Move, CursorState::Hover,
    CursorState::Click, CursorState::Text, CursorState::Disabled
};

} // namespace

char const* CursorShopDetailPopup::stateName(CursorState state) {
    switch (state) {
        case CursorState::Move:     return "Mover";
        case CursorState::Hover:    return "Boton";
        case CursorState::Click:    return "Click";
        case CursorState::Text:     return "Texto";
        case CursorState::Disabled: return "Bloqueado";
        case CursorState::Idle:
        default:                    return "Normal";
    }
}

ccColor3B CursorShopDetailPopup::stateColor(CursorState state) {
    switch (state) {
        case CursorState::Move:     return ccc3(0, 120, 255);
        case CursorState::Hover:    return ccc3(255, 140, 0);
        case CursorState::Click:    return ccc3(200, 60, 255);
        case CursorState::Text:     return ccc3(0, 220, 220);
        case CursorState::Disabled: return ccc3(255, 70, 70);
        case CursorState::Idle:
        default:                    return ccc3(0, 200, 0);
    }
}

CursorShopDetailPopup* CursorShopDetailPopup::create(Listing listing,
                                                     std::function<void()> onInstalled) {
    auto ret = new CursorShopDetailPopup();
    ret->m_listing = std::move(listing);
    ret->m_onInstalled = std::move(onInstalled);
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CursorShopDetailPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;

    this->setTitle(m_listing.name.empty() ? "Cursor" : m_listing.name.c_str());
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

    m_subtitle = CCLabelBMFont::create("Cargando ficha...", "chatFont.fnt");
    m_subtitle->setScale(0.45f);
    m_subtitle->setColor(kit::kDescColor);
    m_subtitle->setPosition({content.width / 2.f, content.height - 44.f});
    m_mainLayer->addChild(m_subtitle, 5);

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(0.45f);
    m_statusLabel->setColor(kit::kDescColor);
    m_statusLabel->setPosition({content.width / 2.f + 60.f, 26.f});
    m_mainLayer->addChild(m_statusLabel, 5);

    this->schedule(schedule_selector(CursorShopDetailPopup::updateSmoothScroll));

    fetchDetail();

    paimon::markDynamicPopup(this);
    return true;
}

void CursorShopDetailPopup::onExit() {
    m_alive = false;
    this->unschedule(schedule_selector(CursorShopDetailPopup::updateSmoothScroll));
    Popup::onExit();
}

void CursorShopDetailPopup::scrollWheel(float x, float y) {
    kit::queueWheelScroll(m_grid, x, y, m_gridScrollTargetY, m_gridScrollTargetSet, 6.f);
}

void CursorShopDetailPopup::updateSmoothScroll(float dt) {
    kit::stepWheelScroll(m_grid, m_gridScrollTargetY, m_gridScrollTargetSet, dt);
}

void CursorShopDetailPopup::fetchDetail() {
    WeakRef<CursorShopDetailPopup> self = this;
    ShopClient::fetchDetail(m_listing, [self](Result<Detail> res) {
        auto locked = self.lock();
        if (!locked) return;
        auto* popup = static_cast<CursorShopDetailPopup*>(locked.data());
        if (!popup->m_alive) return;

        if (!res) {
            if (popup->m_subtitle) {
                popup->m_subtitle->setString(res.unwrapErr().c_str());
                popup->m_subtitle->setColor({255, 120, 120});
            }
            return;
        }

        popup->m_detail = res.unwrap();
        popup->m_loaded = true;
        popup->buildBody();
    });
}

void CursorShopDetailPopup::buildBody() {
    auto content = m_mainLayer->getContentSize();

    std::string subtitle;
    if (!m_detail.author.empty()) subtitle = "por " + m_detail.author;
    if (!m_detail.description.empty()) {
        if (!subtitle.empty()) subtitle += "  -  ";
        subtitle += m_detail.description;
    }
    if (subtitle.empty()) {
        subtitle = fmt::format("{} cursor(es) disponibles", m_detail.cursors.size());
    }
    if (m_subtitle) {
        m_subtitle->setString(subtitle.c_str());
        m_subtitle->setColor(kit::kDescColor);
        float width = m_subtitle->getContentSize().width * 0.45f;
        float maxWidth = content.width - 30.f;
        m_subtitle->setScale(width > maxWidth ? 0.45f * maxWidth / width : 0.45f);
    }

    float gridW = 238.f;
    float gridBottom = 58.f;
    float gridTop = content.height - 58.f;

    m_grid = ScrollLayer::create({gridW, gridTop - gridBottom});
    m_grid->setPosition({12.f, gridBottom});
    m_mainLayer->addChild(m_grid, 4);

    m_sideMenu = CCMenu::create();
    m_sideMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_sideMenu, 6);

    float sideX = 12.f + gridW + (content.width - 24.f - gridW) / 2.f;

    m_previewBox = CCNode::create();
    m_previewBox->setContentSize({88.f, 88.f});
    m_previewBox->setPosition({sideX - 44.f, content.height - 150.f});
    m_mainLayer->addChild(m_previewBox, 5);

    auto* previewBg = paimon::SpriteHelper::createColorPanel(88.f, 88.f, ccc3(20, 24, 40), 160);
    previewBg->setPosition({0.f, 0.f});
    m_previewBox->addChild(previewBg, 0);

    // La miniatura grande vive en su propio nodo para poder vaciarlo al cambiar
    // de cursor sin tocar el fondo.
    m_previewSlot = CCNode::create();
    m_previewSlot->setContentSize(m_previewBox->getContentSize());
    m_previewSlot->setPosition({0.f, 0.f});
    m_previewBox->addChild(m_previewSlot, 1);

    m_previewName = CCLabelBMFont::create("", "bigFont.fnt");
    m_previewName->setScale(0.3f);
    m_previewName->setPosition({sideX, content.height - 168.f});
    m_mainLayer->addChild(m_previewName, 5);

    auto* animateSprite = ButtonSprite::create("Animar", "goldFont.fnt", "GJ_button_03.png", 0.7f);
    animateSprite->setScale(0.4f);
    m_animateButton = CCMenuItemSpriteExtra::create(
        animateSprite, this, menu_selector(CursorShopDetailPopup::onAnimate));
    m_animateButton->setPosition({sideX + 56.f, content.height - 106.f});
    m_animateButton->setVisible(false);
    m_sideMenu->addChild(m_animateButton);

    auto* stateTitle = CCLabelBMFont::create("Asignar a", "chatFont.fnt");
    stateTitle->setScale(0.4f);
    stateTitle->setColor(kit::kDescColor);
    stateTitle->setPosition({sideX, content.height - 186.f});
    m_mainLayer->addChild(stateTitle, 5);

    float stateRowY = content.height - 202.f;
    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png")) {
        prevSpr->setScale(0.4f);
        auto* prevBtn = CCMenuItemSpriteExtra::create(
            prevSpr, this, menu_selector(CursorShopDetailPopup::onStatePrev));
        prevBtn->setPosition({sideX - 52.f, stateRowY});
        m_sideMenu->addChild(prevBtn);
    }
    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png")) {
        nextSpr->setScale(0.4f);
        nextSpr->setFlipX(true);
        auto* nextBtn = CCMenuItemSpriteExtra::create(
            nextSpr, this, menu_selector(CursorShopDetailPopup::onStateNext));
        nextBtn->setPosition({sideX + 52.f, stateRowY});
        m_sideMenu->addChild(nextBtn);
    }

    m_stateLabel = CCLabelBMFont::create("Normal", "bigFont.fnt");
    m_stateLabel->setScale(0.4f);
    m_stateLabel->setPosition({sideX, stateRowY});
    m_mainLayer->addChild(m_stateLabel, 5);

    auto* useSpr = ButtonSprite::create("Instalar y usar", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    useSpr->setScale(0.55f);
    auto* useBtn = CCMenuItemSpriteExtra::create(
        useSpr, this, menu_selector(CursorShopDetailPopup::onInstallAssign));
    useBtn->setPosition({sideX, stateRowY - 26.f});
    m_sideMenu->addChild(useBtn);

    auto* saveSpr = ButtonSprite::create("Solo guardar", "goldFont.fnt", "GJ_button_04.png", 0.7f);
    saveSpr->setScale(0.5f);
    auto* saveBtn = CCMenuItemSpriteExtra::create(
        saveSpr, this, menu_selector(CursorShopDetailPopup::onInstallOne));
    saveBtn->setPosition({sideX, stateRowY - 50.f});
    m_sideMenu->addChild(saveBtn);

    if (m_detail.cursors.size() > 1) {
        auto* allSpr = ButtonSprite::create(
            fmt::format("Instalar set ({})", m_detail.cursors.size()).c_str(),
            "goldFont.fnt", "GJ_button_01.png", 0.7f);
        allSpr->setScale(0.55f);
        auto* allBtn = CCMenuItemSpriteExtra::create(
            allSpr, this, menu_selector(CursorShopDetailPopup::onInstallAll));
        allBtn->setPosition({80.f, 26.f});
        m_sideMenu->addChild(allBtn);
    }

    rebuildGrid();
    updateSelection();
}

void CursorShopDetailPopup::rebuildGrid() {
    if (!m_grid || !m_grid->m_contentLayer) return;

    auto* layer = m_grid->m_contentLayer;
    layer->removeAllChildren();

    auto viewSize = m_grid->getContentSize();
    int columns = std::max(1, static_cast<int>((viewSize.width - kCellGap) / (kCellSize + kCellGap)));
    int rows = (static_cast<int>(m_detail.cursors.size()) + columns - 1) / columns;
    float gridHeight = std::max(viewSize.height, rows * (kCellSize + kCellGap) + kCellGap);
    layer->setContentSize({viewSize.width, gridHeight});

    auto* holder = CCNode::create();
    holder->setContentSize({viewSize.width, gridHeight});
    holder->setLayout(
        RowLayout::create()
            ->setGap(kCellGap)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::End)
    );
    layer->addChild(holder);

    for (int i = 0; i < static_cast<int>(m_detail.cursors.size()); ++i) {
        auto const& cursor = m_detail.cursors[i];

        auto* cell = CCNode::create();
        cell->setContentSize({kCellSize, kCellSize});
        cell->setAnchorPoint({0.5f, 0.5f});
        holder->addChild(cell);

        auto color = cursor.hasSuggested ? stateColor(cursor.suggested) : ccc3(50, 50, 50);
        GLubyte alpha = cursor.hasSuggested ? 150 : 100;
        auto* bg = paimon::SpriteHelper::createColorPanel(kCellSize, kCellSize, color, alpha);
        bg->setPosition({0.f, 0.f});
        cell->addChild(bg, 0);

        mountThumb(cell, cursor.previewUrl, kCellSize - 12.f, kCellSize - 12.f);

        if (cursor.animated) {
            auto* badge = CCLabelBMFont::create("ANI", "bigFont.fnt");
            badge->setScale(0.18f);
            badge->setColor({255, 100, 100});
            badge->setAnchorPoint({0.f, 0.f});
            badge->setPosition({3.f, 3.f});
            cell->addChild(badge, 4);
        }

        auto* cellMenu = CCMenu::create();
        cellMenu->setPosition({0.f, 0.f});
        cellMenu->setContentSize({kCellSize, kCellSize});
        cell->addChild(cellMenu, 5);

        auto* hit = CCSprite::create();
        hit->setContentSize({kCellSize, kCellSize});
        hit->setOpacity(0);
        auto* btn = CCMenuItemSpriteExtra::create(
            hit, this, menu_selector(CursorShopDetailPopup::onCursorCell));
        btn->setContentSize({kCellSize, kCellSize});
        btn->setPosition({kCellSize / 2.f, kCellSize / 2.f});
        btn->setTag(i);
        cellMenu->addChild(btn);
    }

    holder->updateLayout();
    m_grid->scrollToTop();
    m_gridScrollTargetSet = false;
}

void CursorShopDetailPopup::updateSelection() {
    if (m_detail.cursors.empty()) return;
    m_selected = std::clamp(m_selected, 0, static_cast<int>(m_detail.cursors.size()) - 1);
    auto const& cursor = m_detail.cursors[m_selected];

    if (cursor.hasSuggested) m_assignState = cursor.suggested;
    updateStateLabel();

    if (m_previewName) {
        m_previewName->setString(cursor.name.empty() ? "Cursor" : cursor.name.c_str());
        float width = m_previewName->getContentSize().width * 0.3f;
        if (width > 120.f) m_previewName->setScale(0.3f * 120.f / width);
        else m_previewName->setScale(0.3f);
    }

    if (m_previewSlot) {
        m_animating = false;
        m_previewSlot->removeAllChildren();

        // Si ya se bajo antes, se reanuda sin volver a pedirlo.
        if (cursor.animated && m_animations.count(cursor.downloadUrl)) {
            showAnimation(cursor.downloadUrl);
        } else {
            mountThumb(m_previewSlot, cursor.previewUrl, 62.f, 62.f);
        }
    }

    if (m_animateButton) {
        bool offer = cursor.animated && !m_animating;
        m_animateButton->setVisible(offer);
        m_animateButton->setEnabled(offer);
    }
}

void CursorShopDetailPopup::onAnimate(CCObject*) {
    if (m_busy || !m_loaded || m_detail.cursors.empty()) return;

    auto const& cursor = m_detail.cursors[m_selected];
    if (!cursor.animated || cursor.downloadUrl.empty()) return;

    if (m_animations.count(cursor.downloadUrl)) {
        showAnimation(cursor.downloadUrl);
        return;
    }
    playAnimation(cursor.downloadUrl);
}

void CursorShopDetailPopup::playAnimation(std::string const& url) {
    setStatus("Cargando animacion...", kit::kValueColor);
    if (m_animateButton) m_animateButton->setEnabled(false);

    WeakRef<CursorShopDetailPopup> self = this;
    ShopClient::download(url, [self, url](Result<std::vector<std::uint8_t>> res) {
        auto locked = self.lock();
        if (!locked) return;
        auto* popup = static_cast<CursorShopDetailPopup*>(locked.data());
        if (!popup->m_alive) return;

        if (popup->m_animateButton) popup->m_animateButton->setEnabled(true);

        if (!res) {
            popup->setStatus("No se pudo cargar", {255, 120, 120});
            return;
        }

        auto const& bytes = res.unwrap();
        auto decoded = paimon::cursor_ico::decode(bytes.data(), bytes.size());
        if (!decoded.success || decoded.frames.empty()) {
            popup->setStatus("Animacion ilegible", {255, 120, 120});
            return;
        }

        Animation animation;
        animation.frames.reserve(decoded.frames.size());
        int totalDelay = 0;
        for (auto const& frame : decoded.frames) {
            if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) continue;
            auto image = ImageLoadHelper::createFromRGBA(
                frame.rgba.data(), frame.width, frame.height, false);
            if (!image.success || !image.texture) continue;
            animation.frames.emplace_back(image.texture);
            // El Ref se queda con su propia referencia.
            image.texture->release();
            totalDelay += std::max(10, frame.delayMs);
        }
        if (animation.frames.empty()) {
            popup->setStatus("Animacion ilegible", {255, 120, 120});
            return;
        }

        // Un ritmo unico para todos: los .ani rara vez cambian de cadencia y
        // asi basta un CCAnimation normal.
        animation.step = std::clamp(
            static_cast<float>(totalDelay) / animation.frames.size() / 1000.f, 0.03f, 0.5f);

        popup->setStatus(fmt::format("{} fotogramas", animation.frames.size()), kit::kOnColor);
        popup->m_animations[url] = std::move(animation);
        popup->showAnimation(url);
    });
}

void CursorShopDetailPopup::showAnimation(std::string const& url) {
    auto found = m_animations.find(url);
    if (found == m_animations.end() || found->second.frames.empty() || !m_previewSlot) return;

    auto const& animation = found->second;
    m_previewSlot->removeAllChildren();

    auto* sprite = CCSprite::createWithTexture(animation.frames.front().data());
    if (!sprite) return;

    auto size = sprite->getContentSize();
    float biggest = std::max(size.width, size.height);
    if (biggest > 0.f) sprite->setScale(62.f / biggest);
    auto box = m_previewSlot->getContentSize();
    sprite->setPosition({box.width / 2.f, box.height / 2.f});
    m_previewSlot->addChild(sprite, 1);

    m_animating = true;
    if (m_animateButton) {
        m_animateButton->setVisible(false);
        m_animateButton->setEnabled(false);
    }

    if (animation.frames.size() < 2) return;

    auto* frames = CCArray::create();
    for (auto const& texture : animation.frames) {
        auto textureSize = texture->getContentSize();
        auto* frame = CCSpriteFrame::createWithTexture(
            texture.data(), CCRectMake(0.f, 0.f, textureSize.width, textureSize.height));
        if (frame) frames->addObject(frame);
    }
    if (frames->count() < 2) return;

    if (auto* sequence = CCAnimation::createWithSpriteFrames(frames, animation.step)) {
        sprite->runAction(CCRepeatForever::create(CCAnimate::create(sequence)));
    }
}

void CursorShopDetailPopup::updateStateLabel() {
    if (!m_stateLabel) return;
    m_stateLabel->setString(stateName(m_assignState));
    m_stateLabel->setColor(stateColor(m_assignState));
}

void CursorShopDetailPopup::setStatus(std::string const& text, ccColor3B color) {
    if (!m_statusLabel) return;
    m_statusLabel->setString(text.c_str());
    m_statusLabel->setColor(color);
}

void CursorShopDetailPopup::setBusy(bool busy) {
    m_busy = busy;
    if (m_sideMenu) m_sideMenu->setVisible(!busy);
}

void CursorShopDetailPopup::onCursorCell(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_selected = btn->getTag();
    updateSelection();
}

void CursorShopDetailPopup::onStatePrev(CCObject*) {
    auto at = std::find(kStates.begin(), kStates.end(), m_assignState);
    int index = at == kStates.end() ? 0 : static_cast<int>(at - kStates.begin());
    index = (index + CURSOR_STATE_COUNT - 1) % CURSOR_STATE_COUNT;
    m_assignState = kStates[index];
    updateStateLabel();
}

void CursorShopDetailPopup::onStateNext(CCObject*) {
    auto at = std::find(kStates.begin(), kStates.end(), m_assignState);
    int index = at == kStates.end() ? 0 : static_cast<int>(at - kStates.begin());
    index = (index + 1) % CURSOR_STATE_COUNT;
    m_assignState = kStates[index];
    updateStateLabel();
}

void CursorShopDetailPopup::onInstallOne(CCObject*) {
    if (m_busy || !m_loaded || m_detail.cursors.empty()) return;
    startQueue({m_selected}, "", false);
}

void CursorShopDetailPopup::onInstallAssign(CCObject*) {
    if (m_busy || !m_loaded || m_detail.cursors.empty()) return;
    startQueue({m_selected}, "", true);
}

void CursorShopDetailPopup::onInstallAll(CCObject*) {
    if (m_busy || !m_loaded || m_detail.cursors.empty()) return;

    std::vector<int> all;
    all.reserve(m_detail.cursors.size());
    for (int i = 0; i < static_cast<int>(m_detail.cursors.size()); ++i) all.push_back(i);

    auto pack = CursorManager::get().createPack(
        m_detail.name.empty() ? m_listing.name : m_detail.name);
    if (pack.empty()) {
        setStatus("No se pudo crear el pack", {255, 120, 120});
        return;
    }
    startQueue(std::move(all), pack, true);
}

void CursorShopDetailPopup::startQueue(std::vector<int> indices, std::string const& packName, bool assign) {
    m_queue = std::move(indices);
    m_queueIndex = 0;
    m_queuePack = packName;
    m_queueDone = 0;
    m_queueAssign = assign;
    setBusy(true);
    stepQueue();
}

void CursorShopDetailPopup::stepQueue() {
    if (m_queueIndex >= m_queue.size()) {
        finishQueue();
        return;
    }

    int index = m_queue[m_queueIndex];
    if (index < 0 || index >= static_cast<int>(m_detail.cursors.size())) {
        ++m_queueIndex;
        stepQueue();
        return;
    }

    auto const& cursor = m_detail.cursors[index];
    if (m_queue.size() > 1) {
        setStatus(fmt::format("Descargando {}/{}", m_queueIndex + 1, m_queue.size()),
                  kit::kValueColor);
    } else {
        setStatus("Descargando...", kit::kValueColor);
    }

    WeakRef<CursorShopDetailPopup> self = this;
    ShopClient::download(cursor.downloadUrl, cursor.fallbackUrl,
                         [self, index](Result<std::vector<std::uint8_t>> res) {
        auto locked = self.lock();
        if (!locked) return;
        auto* popup = static_cast<CursorShopDetailPopup*>(locked.data());
        if (!popup->m_alive) return;

        if (res) {
            auto const& source = popup->m_detail.cursors[index];
            auto filename = ShopClient::filenameFor(source.downloadUrl, source.name);
            auto imported = CursorManager::get().importData(
                res.unwrap(), filename, popup->m_queuePack);

            if (!imported.empty()) {
                ++popup->m_queueDone;
                bool assign = popup->m_queueAssign;
                // En una cola larga solo se asignan los roles que la tienda marca.
                if (assign && popup->m_queue.size() > 1 && !source.hasSuggested) assign = false;
                if (assign) {
                    auto state = popup->m_queue.size() > 1 ? source.suggested : popup->m_assignState;
                    CursorManager::get().setImageForState(state, imported);
                }
            }
        }

        ++popup->m_queueIndex;
        popup->stepQueue();
    });
}

void CursorShopDetailPopup::finishQueue() {
    setBusy(false);

    int total = static_cast<int>(m_queue.size());
    m_queue.clear();
    m_queueIndex = 0;

    if (m_queueDone == 0) {
        // Sin nada dentro, la carpeta reservada solo ensucia la galeria.
        if (!m_queuePack.empty()) {
            CursorManager::get().removePack(m_queuePack);
            m_queuePack.clear();
        }
        setStatus("No se pudo instalar", {255, 120, 120});
        PaimonNotify::show("La descarga fallo. Reintenta en un momento.", NotificationIcon::Error);
        return;
    }

    if (m_onInstalled) m_onInstalled();

    if (total > 1) {
        setStatus(fmt::format("{} cursores instalados", m_queueDone), kit::kOnColor);
        PaimonNotify::show(
            fmt::format("Pack '{}' instalado con {} cursores!", m_queuePack, m_queueDone),
            NotificationIcon::Success);
    } else {
        setStatus("Instalado!", kit::kOnColor);
        PaimonNotify::show(
            m_queueAssign
                ? fmt::format("Cursor asignado a {}!", stateName(m_assignState))
                : std::string("Cursor guardado en la galeria!"),
            NotificationIcon::Success);
    }
    m_queuePack.clear();
}
