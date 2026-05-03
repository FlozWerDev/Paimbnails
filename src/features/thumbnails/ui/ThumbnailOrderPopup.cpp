#include "ThumbnailOrderPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../services/ThumbnailLoader.hpp"

#include <Geode/binding/ButtonSprite.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
constexpr float kPopupWidth = 388.f;
constexpr float kPopupHeight = 284.f;
constexpr float kScrollInset = 9.f;
constexpr float kScrollBottom = 66.f;
constexpr float kCellWidth = 82.f;
constexpr float kCellHeight = 76.f;
constexpr float kCellGap = 8.f;
constexpr float kPreviewWidth = 72.f;
constexpr float kPreviewHeight = 42.f;

constexpr ccColor4F kCellBorderColor = {0.28f, 0.31f, 0.38f, 0.74f};
constexpr ccColor4F kCellFillColor = {0.03f, 0.03f, 0.06f, 0.86f};
constexpr ccColor4F kCellSelectedFill = {0.20f, 0.15f, 0.05f, 0.92f};
constexpr ccColor4F kCellMainBorder = {0.16f, 0.86f, 1.00f, 0.94f};
constexpr ccColor4F kCellMainFill = {0.05f, 0.19f, 0.24f, 0.90f};
constexpr ccColor4F kCellSelectedAccent = {0.98f, 0.82f, 0.28f, 0.96f};

ButtonSprite* createSmallTextButton(char const* text, char const* bg, float scale = 0.5f, int width = 70) {
    return ButtonSprite::create(text, width, true, "bigFont.fnt", bg, 26.f, scale);
}

std::string buildPreviewUrl(ThumbnailInfo const& thumbnail) {
    std::string url = thumbnail.url;
    if (!thumbnail.id.empty()) {
        auto sep = (url.find('?') == std::string::npos) ? "?" : "&";
        url += fmt::format("{}_pv={}", sep, thumbnail.id);
    }
    return url;
}

std::string mediaLabelForThumbnail(ThumbnailInfo const& thumbnail) {
    if (thumbnail.isVideo()) return "MP4";
    if (thumbnail.isGif()) return "GIF";
    if (!thumbnail.format.empty()) {
        std::string text = thumbnail.format;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        if (text.size() > 4) {
            text.resize(4);
        }
        return text;
    }
    return "IMG";
}

void fitPreviewNode(CCNode* node) {
    if (!node) return;

    CCSize sourceSize = node->getContentSize();
    if (auto* videoSprite = typeinfo_cast<VideoThumbnailSprite*>(node)) {
        sourceSize = videoSprite->getVideoSize();
    }

    float safeWidth = std::max(1.f, sourceSize.width);
    float safeHeight = std::max(1.f, sourceSize.height);
    float scale = std::max(kPreviewWidth / safeWidth, kPreviewHeight / safeHeight);

    node->setAnchorPoint({0.5f, 0.5f});
    node->setScale(scale);
    node->setPosition({kPreviewWidth * 0.5f, kPreviewHeight * 0.5f});
}
}

ThumbnailOrderPopup* ThumbnailOrderPopup::create(
    int levelID,
    std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbnails,
    std::string const& selectedId
) {
    auto ret = new ThumbnailOrderPopup();
    if (ret && ret->init(levelID, thumbnails, selectedId)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ThumbnailOrderPopup::init(
    int levelID,
    std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbnails,
    std::string const& selectedId
) {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    if (thumbnails.size() < 2) return false;

    m_levelID = levelID;
    m_thumbnails = thumbnails;
    m_originalOrderIds = getOrderIds();

    int preferredIndex = indexForThumbnail(selectedId);
    m_selectedIndex = preferredIndex >= 0 ? preferredIndex : 0;

    this->setTitle("Thumbnail Order");

    auto content = m_mainLayer->getContentSize();

    auto subtitle = CCLabelBMFont::create(
        "Select a thumbnail, move it with arrows, then save. Cyan = main.",
        "chatFont.fnt"
    );
    subtitle->setScale(0.6f);
    subtitle->setColor({190, 205, 220});
    subtitle->setPosition({content.width * 0.5f, content.height - 38.f});
    m_mainLayer->addChild(subtitle, 2);

    float scrollWidth = content.width - kScrollInset * 2.f;
    float scrollHeight = content.height - kScrollBottom - 58.f;

    auto scrollBg = paimon::SpriteHelper::createDarkPanel(scrollWidth, scrollHeight, 115, 8.f);
    scrollBg->setPosition({kScrollInset, kScrollBottom});
    m_mainLayer->addChild(scrollBg, 1);

    m_scrollLayer = ScrollLayer::create({scrollWidth, scrollHeight});
    m_scrollLayer->setPosition({kScrollInset, kScrollBottom});
    m_mainLayer->addChild(m_scrollLayer, 2);

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({scrollWidth, scrollHeight});
    m_scrollLayer->m_contentLayer->setContentSize({scrollWidth, scrollHeight});
    m_scrollLayer->m_contentLayer->addChild(m_contentNode);

    m_gridMenu = CCMenu::create();
    m_gridMenu->setPosition({0.f, 0.f});
    m_contentNode->addChild(m_gridMenu);

    m_selectedLabel = CCLabelBMFont::create("Selected: #1", "bigFont.fnt");
    m_selectedLabel->setScale(0.33f);
    m_selectedLabel->setPosition({content.width * 0.5f, 48.f});
    m_mainLayer->addChild(m_selectedLabel, 3);

    m_hintLabel = CCLabelBMFont::create("Main thumbnail is always the first slot.", "chatFont.fnt");
    m_hintLabel->setScale(0.55f);
    m_hintLabel->setColor({140, 220, 255});
    m_hintLabel->setPosition({content.width * 0.5f, 34.f});
    m_mainLayer->addChild(m_hintLabel, 3);

    auto actionMenu = CCMenu::create();
    actionMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(actionMenu, 4);

    if (auto leftSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        leftSpr->setScale(0.62f);
        m_moveLeftBtn = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(ThumbnailOrderPopup::onMoveLeft));
        m_moveLeftBtn->setPosition({content.width * 0.5f - 92.f, 18.f});
        actionMenu->addChild(m_moveLeftBtn);
    }

    if (auto rightSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        rightSpr->setFlipX(true);
        rightSpr->setScale(0.62f);
        m_moveRightBtn = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(ThumbnailOrderPopup::onMoveRight));
        m_moveRightBtn->setPosition({content.width * 0.5f - 48.f, 18.f});
        actionMenu->addChild(m_moveRightBtn);
    }

    if (auto cancelSpr = createSmallTextButton("Cancel", "GJ_button_05.png", 0.46f)) {
        m_cancelBtn = CCMenuItemSpriteExtra::create(cancelSpr, this, menu_selector(ThumbnailOrderPopup::onCancel));
        m_cancelBtn->setPosition({content.width * 0.5f + 56.f, 18.f});
        actionMenu->addChild(m_cancelBtn);
    }

    if (auto saveSpr = createSmallTextButton("Save", "GJ_button_01.png", 0.48f)) {
        m_saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(ThumbnailOrderPopup::onSave));
        m_saveBtn->setPosition({content.width * 0.5f + 122.f, 18.f});
        actionMenu->addChild(m_saveBtn);
    }

    buildCells();
    layoutCells(true);
    updateUiState();

    paimon::markDynamicPopup(this);
    return true;
}

void ThumbnailOrderPopup::buildCells() {
    m_cells.clear();
    if (m_gridMenu) {
        m_gridMenu->removeAllChildren();
    }

    m_cells.reserve(m_thumbnails.size());

    for (size_t index = 0; index < m_thumbnails.size(); ++index) {
        auto container = CCNode::create();
        container->setContentSize({kCellWidth, kCellHeight});

        auto previewClip = CCClippingNode::create(paimon::SpriteHelper::createRoundedRectStencil(kPreviewWidth, kPreviewHeight, 6.f));
        previewClip->setContentSize({kPreviewWidth, kPreviewHeight});
        previewClip->setAnchorPoint({0.f, 0.f});
        previewClip->setPosition({(kCellWidth - kPreviewWidth) * 0.5f, 20.f});
        container->addChild(previewClip, 2);

        auto previewBg = paimon::SpriteHelper::createDarkPanel(kPreviewWidth, kPreviewHeight, 150, 5.f);
        previewBg->setPosition({0.f, 0.f});
        previewClip->addChild(previewBg, -1);

        auto placeholder = CCLabelBMFont::create("Loading", "chatFont.fnt");
        placeholder->setScale(0.45f);
        placeholder->setColor({165, 170, 185});
        placeholder->setPosition({kPreviewWidth * 0.5f, kPreviewHeight * 0.5f});
        previewClip->addChild(placeholder, 2);

        auto orderLabel = CCLabelBMFont::create("#1", "goldFont.fnt");
        orderLabel->setScale(0.34f);
        orderLabel->setAnchorPoint({0.f, 0.5f});
        orderLabel->setPosition({7.f, kCellHeight - 10.f});
        container->addChild(orderLabel, 3);

        auto typeLabel = CCLabelBMFont::create("IMG", "chatFont.fnt");
        typeLabel->setScale(0.45f);
        typeLabel->setAnchorPoint({1.f, 0.5f});
        typeLabel->setPosition({kCellWidth - 7.f, 9.f});
        container->addChild(typeLabel, 3);

        auto mainLabel = CCLabelBMFont::create("MAIN", "chatFont.fnt");
        mainLabel->setScale(0.45f);
        mainLabel->setPosition({kCellWidth * 0.5f, 9.f});
        container->addChild(mainLabel, 3);

        auto button = CCMenuItemSpriteExtra::create(container, this, menu_selector(ThumbnailOrderPopup::onSelectThumbnail));
        button->setTag(static_cast<int>(index));
        button->setSizeMult(0.98f);
        m_gridMenu->addChild(button);

        CellWidgets cell;
        cell.button = button;
        cell.container = container;
        cell.previewClip = previewClip;
        cell.orderLabel = orderLabel;
        cell.typeLabel = typeLabel;
        cell.mainLabel = mainLabel;
        cell.placeholderLabel = placeholder;

        m_cells.push_back(std::move(cell));
    }
}

void ThumbnailOrderPopup::layoutCells(bool resetScrollPosition) {
    if (!m_scrollLayer || !m_contentNode || !m_gridMenu) return;

    float scrollWidth = m_scrollLayer->getContentSize().width;
    float scrollHeight = m_scrollLayer->getContentSize().height;
    int columns = std::max(1, static_cast<int>((scrollWidth + kCellGap) / (kCellWidth + kCellGap)));
    int rows = static_cast<int>((m_cells.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
    float contentHeight = std::max(scrollHeight, rows * (kCellHeight + kCellGap) + kCellGap);

    m_contentNode->setContentSize({scrollWidth, contentHeight});
    m_gridMenu->setContentSize({scrollWidth, contentHeight});
    m_scrollLayer->m_contentLayer->setContentSize({scrollWidth, contentHeight});

    for (size_t index = 0; index < m_cells.size(); ++index) {
        int row = static_cast<int>(index / static_cast<size_t>(columns));
        int col = static_cast<int>(index % static_cast<size_t>(columns));
        float x = kCellGap + col * (kCellWidth + kCellGap) + kCellWidth * 0.5f;
        float y = contentHeight - kCellGap - row * (kCellHeight + kCellGap) - kCellHeight * 0.5f;

        auto& cell = m_cells[index];
        if (cell.button) {
            cell.button->setTag(static_cast<int>(index));
            cell.button->setPosition({x, y});
        }

        updateCellVisual(static_cast<int>(index));
        requestPreview(static_cast<int>(index));
    }

    if (resetScrollPosition) {
        m_scrollLayer->moveToTop();
    }
}

void ThumbnailOrderPopup::updateUiState() {
    bool canMoveLeft = !m_isSaving && m_selectedIndex > 0 && m_selectedIndex < static_cast<int>(m_thumbnails.size());
    bool canMoveRight = !m_isSaving && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_thumbnails.size()) - 1;
    bool canSave = !m_isSaving && hasUnsavedChanges();

    auto updateButton = [this](CCMenuItemSpriteExtra* button, bool enabled) {
        if (!button) return;
        button->setEnabled(enabled);
        button->setOpacity(enabled ? 255 : 105);
    };

    updateButton(m_moveLeftBtn, canMoveLeft);
    updateButton(m_moveRightBtn, canMoveRight);
    updateButton(m_saveBtn, canSave);
    updateButton(m_cancelBtn, !m_isSaving);

    if (m_closeBtn) {
        m_closeBtn->setEnabled(!m_isSaving);
        m_closeBtn->setVisible(!m_isSaving);
    }

    if (m_selectedLabel) {
        std::string text = m_selectedIndex == 0
            ? "Selected: #1 (Main)"
            : fmt::format("Selected: #{}", m_selectedIndex + 1);
        if (m_isSaving) {
            text += " | Saving...";
        }
        m_selectedLabel->setString(text.c_str());
    }

    if (m_hintLabel) {
        if (m_isSaving) {
            m_hintLabel->setString("Saving thumbnail order to the server...");
            m_hintLabel->setColor({255, 220, 120});
        } else if (hasUnsavedChanges()) {
            m_hintLabel->setString("Unsaved changes. Press Save to update the server.");
            m_hintLabel->setColor({255, 220, 120});
        } else {
            m_hintLabel->setString("Main thumbnail is always the first slot.");
            m_hintLabel->setColor({140, 220, 255});
        }
    }
}

void ThumbnailOrderPopup::updateCellVisual(int index) {
    if (index < 0 || index >= static_cast<int>(m_cells.size()) || index >= static_cast<int>(m_thumbnails.size())) return;

    auto& cell = m_cells[index];
    auto const& thumbnail = m_thumbnails[index];
    if (!cell.container) return;

    if (auto node = cell.container->getChildByID("order-cell-border"_spr)) node->removeFromParent();
    if (auto node = cell.container->getChildByID("order-cell-fill"_spr)) node->removeFromParent();
    if (auto node = cell.container->getChildByID("order-cell-accent"_spr)) node->removeFromParent();

    bool isSelected = index == m_selectedIndex;
    bool isMain = index == 0;

    ccColor4F borderColor = isMain ? kCellMainBorder : kCellBorderColor;
    ccColor4F fillColor = kCellFillColor;
    if (isMain) {
        fillColor = kCellMainFill;
    }
    if (isSelected) {
        fillColor = isMain ? ccColor4F{0.08f, 0.24f, 0.30f, 0.92f} : kCellSelectedFill;
    }

    auto border = paimon::SpriteHelper::createRoundedRect(kCellWidth, kCellHeight, 8.f, borderColor);
    border->setID("order-cell-border"_spr);
    border->setPosition({0.f, 0.f});
    cell.container->addChild(border, 0);

    auto fill = paimon::SpriteHelper::createRoundedRect(kCellWidth - 2.f, kCellHeight - 2.f, 7.f, fillColor);
    fill->setID("order-cell-fill"_spr);
    fill->setPosition({1.f, 1.f});
    cell.container->addChild(fill, 1);

    if (isSelected) {
        auto accent = paimon::SpriteHelper::createRoundedRect(kCellWidth - 12.f, 3.f, 1.5f, kCellSelectedAccent);
        accent->setID("order-cell-accent"_spr);
        accent->setPosition({6.f, kCellHeight - 6.f});
        cell.container->addChild(accent, 3);
    }

    if (cell.orderLabel) {
        cell.orderLabel->setString(fmt::format("#{}", index + 1).c_str());
        cell.orderLabel->setColor(isMain ? ccColor3B{110, 245, 255} : ccColor3B{255, 220, 120});
    }

    if (cell.typeLabel) {
        cell.typeLabel->setString(mediaLabelForThumbnail(thumbnail).c_str());
        if (thumbnail.isVideo()) {
            cell.typeLabel->setColor({160, 210, 255});
        } else if (thumbnail.isGif()) {
            cell.typeLabel->setColor({255, 170, 245});
        } else {
            cell.typeLabel->setColor({200, 205, 215});
        }
    }

    if (cell.mainLabel) {
        cell.mainLabel->setVisible(isMain);
        cell.mainLabel->setColor({110, 245, 255});
    }

    if (cell.button) {
        cell.button->setScale(isSelected ? 1.03f : 1.0f);
    }
}

void ThumbnailOrderPopup::requestPreview(int index) {
    if (index < 0 || index >= static_cast<int>(m_cells.size()) || index >= static_cast<int>(m_thumbnails.size())) return;

    auto& cell = m_cells[index];
    if (cell.previewRequested) return;
    cell.previewRequested = true;

    auto const& thumbnail = m_thumbnails[index];
    std::string thumbnailKey = keyForThumbnail(thumbnail);

    if (thumbnail.isVideo()) {
        auto cacheToken = thumbnailKey.empty() ? fmt::format("{}-{}", m_levelID, index) : thumbnailKey;
        auto cacheKey = fmt::format("order_video_{}_{}", m_levelID, std::hash<std::string>{}(cacheToken));
        WeakRef<ThumbnailOrderPopup> self = this;

        VideoThumbnailSprite::createAsync(buildPreviewUrl(thumbnail), cacheKey, [self, thumbnailKey](VideoThumbnailSprite* videoSprite) {
            auto popup = self.lock();
            if (!popup || !videoSprite) return;

            int currentIndex = popup->indexForThumbnail(thumbnailKey);
            if (currentIndex < 0 || currentIndex >= static_cast<int>(popup->m_cells.size())) {
                if (videoSprite->getParent()) videoSprite->removeFromParent();
                return;
            }

            auto& cell = popup->m_cells[currentIndex];
            if (!cell.previewClip) {
                if (videoSprite->getParent()) videoSprite->removeFromParent();
                return;
            }

            videoSprite->setVolume(0.0f);
            videoSprite->setLoop(true);
            videoSprite->setVisible(false);
            videoSprite->setOpacity(0);

            if (videoSprite->getParent() != cell.previewClip) {
                if (videoSprite->getParent()) {
                    videoSprite->removeFromParent();
                }
                cell.previewClip->addChild(videoSprite, 1);
            }

            fitPreviewNode(videoSprite);
            videoSprite->play();
            videoSprite->setOnFirstVisibleFrame([self, thumbnailKey](VideoThumbnailSprite* readySprite) {
                auto popup = self.lock();
                if (!popup) {
                    if (readySprite->getParent()) readySprite->removeFromParent();
                    return;
                }

                readySprite->pause();
                readySprite->setVisible(true);
                readySprite->setOpacity(255);
                popup->applyVideoPreview(thumbnailKey, readySprite);
            });
        });
        return;
    }

    WeakRef<ThumbnailOrderPopup> self = this;
    ThumbnailLoader::get().requestUrlLoad(buildPreviewUrl(thumbnail), [self, thumbnailKey](CCTexture2D* texture, bool success) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success || !texture) {
            int currentIndex = popup->indexForThumbnail(thumbnailKey);
            if (currentIndex >= 0 && currentIndex < static_cast<int>(popup->m_cells.size())) {
                if (auto* label = popup->m_cells[currentIndex].placeholderLabel.data()) {
                    label->setString("No preview");
                }
            }
            return;
        }

        popup->applyImagePreview(thumbnailKey, texture);
    }, ThumbnailLoader::PriorityVisiblePrefetch);
}

void ThumbnailOrderPopup::applyImagePreview(std::string const& thumbnailKey, CCTexture2D* texture) {
    int index = indexForThumbnail(thumbnailKey);
    if (index < 0 || index >= static_cast<int>(m_cells.size()) || !texture) return;

    auto& cell = m_cells[index];
    if (!cell.previewClip) return;

    if (auto node = cell.previewClip->getChildByID("preview-node"_spr)) {
        node->removeFromParent();
    }

    auto sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return;

    sprite->setID("preview-node"_spr);
    fitPreviewNode(sprite);
    cell.previewClip->addChild(sprite, 1);

    if (cell.placeholderLabel) {
        cell.placeholderLabel->setVisible(false);
    }
}

void ThumbnailOrderPopup::applyVideoPreview(std::string const& thumbnailKey, VideoThumbnailSprite* videoSprite) {
    int index = indexForThumbnail(thumbnailKey);
    if (index < 0 || index >= static_cast<int>(m_cells.size()) || !videoSprite) {
        if (videoSprite && videoSprite->getParent()) {
            videoSprite->removeFromParent();
        }
        return;
    }

    auto& cell = m_cells[index];
    if (!cell.previewClip) {
        if (videoSprite->getParent()) {
            videoSprite->removeFromParent();
        }
        return;
    }

    if (videoSprite->getParent() != cell.previewClip) {
        if (videoSprite->getParent()) {
            videoSprite->removeFromParent();
        }
        cell.previewClip->addChild(videoSprite, 1);
    }

    videoSprite->setID("preview-node"_spr);
    fitPreviewNode(videoSprite);
    videoSprite->setVisible(true);
    videoSprite->setOpacity(255);

    if (cell.placeholderLabel) {
        cell.placeholderLabel->setVisible(false);
    }
}

int ThumbnailOrderPopup::indexForThumbnail(std::string const& thumbnailKey) const {
    if (thumbnailKey.empty()) return -1;

    auto it = std::find_if(m_thumbnails.begin(), m_thumbnails.end(), [this, &thumbnailKey](ThumbnailAPI::ThumbnailInfo const& thumbnail) {
        return keyForThumbnail(thumbnail) == thumbnailKey;
    });
    if (it == m_thumbnails.end()) return -1;
    return static_cast<int>(std::distance(m_thumbnails.begin(), it));
}

std::string ThumbnailOrderPopup::keyForThumbnail(ThumbnailAPI::ThumbnailInfo const& thumbnail) const {
    return thumbnail.id.empty() ? thumbnail.url : thumbnail.id;
}

std::string ThumbnailOrderPopup::getSelectedThumbnailKey() const {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_thumbnails.size())) return "";
    return keyForThumbnail(m_thumbnails[m_selectedIndex]);
}

std::vector<std::string> ThumbnailOrderPopup::getOrderIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_thumbnails.size());
    for (auto const& thumbnail : m_thumbnails) {
        ids.push_back(thumbnail.id);
    }
    return ids;
}

bool ThumbnailOrderPopup::hasUnsavedChanges() const {
    return getOrderIds() != m_originalOrderIds;
}

void ThumbnailOrderPopup::onSelectThumbnail(CCObject* sender) {
    if (m_isSaving) return;

    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;

    int index = button->getTag();
    if (index < 0 || index >= static_cast<int>(m_thumbnails.size())) return;

    m_selectedIndex = index;
    for (int i = 0; i < static_cast<int>(m_cells.size()); ++i) {
        updateCellVisual(i);
    }
    updateUiState();
}

void ThumbnailOrderPopup::onMoveLeft(CCObject*) {
    if (m_isSaving || m_selectedIndex <= 0 || m_selectedIndex >= static_cast<int>(m_thumbnails.size())) return;

    std::swap(m_thumbnails[m_selectedIndex], m_thumbnails[m_selectedIndex - 1]);
    std::swap(m_cells[m_selectedIndex], m_cells[m_selectedIndex - 1]);
    --m_selectedIndex;

    layoutCells(false);
    updateUiState();
}

void ThumbnailOrderPopup::onMoveRight(CCObject*) {
    if (m_isSaving || m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_thumbnails.size()) - 1) return;

    std::swap(m_thumbnails[m_selectedIndex], m_thumbnails[m_selectedIndex + 1]);
    std::swap(m_cells[m_selectedIndex], m_cells[m_selectedIndex + 1]);
    ++m_selectedIndex;

    layoutCells(false);
    updateUiState();
}

void ThumbnailOrderPopup::onSave(CCObject*) {
    if (m_isSaving) return;

    auto orderIds = getOrderIds();
    if (orderIds == m_originalOrderIds) {
        PaimonNotify::show("No order changes to save", NotificationIcon::Info);
        return;
    }

    m_isSaving = true;
    updateUiState();

    auto spinner = PaimonLoadingOverlay::create("Saving...", 30.f);
    spinner->show(m_mainLayer, 100);
    Ref<PaimonLoadingOverlay> loading = spinner;

    WeakRef<ThumbnailOrderPopup> self = this;
    std::string selectedKey = getSelectedThumbnailKey();

    ThumbnailAPI::get().reorderThumbnails(m_levelID, orderIds, [self, loading, selectedKey](bool success, std::string const& message) {
        if (loading) loading->dismiss();

        auto popup = self.lock();
        if (!popup) return;

        popup->m_isSaving = false;

        if (!success) {
            popup->updateUiState();
            PaimonNotify::create(message.c_str(), NotificationIcon::Error)->show();
            return;
        }

        ThumbnailTransportClient::get().invalidateGalleryMetadata(popup->m_levelID);
        ThumbnailLoader::get().invalidateLevel(popup->m_levelID);

        for (size_t i = 0; i < popup->m_thumbnails.size(); ++i) {
            popup->m_thumbnails[i].position = static_cast<int>(i) + 1;
        }

        popup->m_originalOrderIds = popup->getOrderIds();

        if (popup->m_onSaved) {
            popup->m_onSaved(popup->m_thumbnails, selectedKey);
        }

        PaimonNotify::show("Thumbnail order saved", NotificationIcon::Success);
        popup->Popup::onClose(nullptr);
    });
}

void ThumbnailOrderPopup::onCancel(CCObject*) {
    if (m_isSaving) return;
    Popup::onClose(nullptr);
}

void ThumbnailOrderPopup::onClose(CCObject* sender) {
    if (m_isSaving) return;

    for (auto const& cell : m_cells) {
        if (!cell.previewClip) continue;
        for (auto* child : CCArrayExt<CCNode*>(cell.previewClip->getChildren())) {
            if (auto* videoSprite = typeinfo_cast<VideoThumbnailSprite*>(child)) {
                videoSprite->pause();
            }
        }
    }

    Popup::onClose(sender);
}
