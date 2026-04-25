#include "CommentBgSettingsPopup.hpp"
#include "../services/ProfileThumbs.hpp"
#include <Geode/ui/ColorPickPopup.hpp>
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// ── Constants ──
static constexpr float kPreviewWidth  = 360.f;
static constexpr float kPreviewHeight = 60.f;
static constexpr float kPreviewRadius = 5.f;

CommentBgSettingsPopup* CommentBgSettingsPopup::create(int accountID, ProfileConfig const& config) {
    auto ret = new CommentBgSettingsPopup();
    if (ret && ret->init(accountID, config)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CommentBgSettingsPopup::~CommentBgSettingsPopup() {
    delete m_configPtr;
    m_configPtr = nullptr;
}

bool CommentBgSettingsPopup::init(int accountID, ProfileConfig const& config) {
    if (!Popup::init(420.f, 300.f)) return false;

    m_accountID = accountID;
    m_configPtr = new ProfileConfig(config);

    this->setTitle("Comment Background");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    float y = content.height - 55.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 5);

    // Etiqueta del tipo
    m_typeLabel = CCLabelBMFont::create("Type: None", "bigFont.fnt");
    m_typeLabel->setScale(0.32f);
    m_typeLabel->setPosition({cx, y});
    m_mainLayer->addChild(m_typeLabel);

    y -= 22.f;

    // Botones de seleccion de tipo
    float btnSpacing = 85.f;
    float startX = cx - btnSpacing * 1.5f;

    auto makeTypeBtn = [&](const char* label, const char* primaryFrame, const char* fallbackFrame, float x, CCMenuItemSpriteExtra*& outBtn) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName(primaryFrame);
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName(fallbackFrame);
        if (!spr) spr = CCSprite::create();
        float maxDim = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (maxDim > 0) spr->setScale(22.f / maxDim);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CommentBgSettingsPopup::onSelectType));
        btn->setPosition({x, y});
        menu->addChild(btn);

        auto lbl = CCLabelBMFont::create(label, "bigFont.fnt");
        lbl->setScale(0.24f);
        lbl->setPosition({x, y - 16.f});
        m_mainLayer->addChild(lbl);

        outBtn = btn;
    };

    makeTypeBtn("None",   "GJ_deleteBtn_001.png",  "GJ_deleteIcon_001.png", startX,                  m_btnNone);
    makeTypeBtn("Thumb",  "GJ_colorBtn_001.png",   "GJ_paintBtn_001.png",  startX + btnSpacing,     m_btnThumbnail);
    makeTypeBtn("Banner", "GJ_paintBtn_001.png",   "GJ_colorBtn_001.png",  startX + btnSpacing * 2, m_btnBanner);
    makeTypeBtn("Solid",  "GJ_colorBtn_001.png",   "GJ_paintBtn_001.png",  startX + btnSpacing * 3, m_btnSolid);

    y -= 40.f;

    // Input de ID de nivel para miniatura
    m_thumbnailIdRow = CCNode::create();
    m_thumbnailIdRow->setPosition({0, 0});
    m_mainLayer->addChild(m_thumbnailIdRow);

    {
        auto idLabel = CCLabelBMFont::create("Level ID:", "bigFont.fnt");
        idLabel->setScale(0.28f);
        idLabel->setPosition({cx - 155.f, y});
        m_thumbnailIdRow->addChild(idLabel);

        m_inputField = TextInput::create(80.f, "ID...");
        m_inputField->setPosition({cx - 60.f, y});
        m_inputField->setCommonFilter(CommonFilter::Uint);
        m_inputField->setMaxCharCount(10);
        if (!m_configPtr->commentBgThumbnailId.empty()) {
            m_inputField->setString(m_configPtr->commentBgThumbnailId);
        }
        m_thumbnailIdRow->addChild(m_inputField);

        auto posMenu = CCMenu::create();
        posMenu->setPosition({0, 0});
        m_thumbnailIdRow->addChild(posMenu);

        auto prevSpr = paimon::SpriteHelper::safeCreateWithFrameName("edit_leftBtn_001.png");
        if (!prevSpr) prevSpr = CCSprite::create();
        m_btnThumbPrev = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(CommentBgSettingsPopup::onThumbPrev));
        m_btnThumbPrev->setPosition({cx + 10.f, y});
        m_btnThumbPrev->setScale(0.45f);
        posMenu->addChild(m_btnThumbPrev);

        m_thumbPosLabel = CCLabelBMFont::create("1/1", "bigFont.fnt");
        m_thumbPosLabel->setScale(0.24f);
        m_thumbPosLabel->setPosition({cx + 45.f, y});
        m_thumbnailIdRow->addChild(m_thumbPosLabel);

        auto nextSpr = paimon::SpriteHelper::safeCreateWithFrameName("edit_rightBtn_001.png");
        if (!nextSpr) nextSpr = CCSprite::create();
        m_btnThumbNext = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(CommentBgSettingsPopup::onThumbNext));
        m_btnThumbNext->setPosition({cx + 80.f, y});
        m_btnThumbNext->setScale(0.45f);
        posMenu->addChild(m_btnThumbNext);

        // Preview de miniatura
        float miniW = 50.f, miniH = 25.f;
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(miniW, miniH, 3.f);
        m_thumbPreviewClip = CCClippingNode::create(stencil);
        m_thumbPreviewClip->setContentSize({miniW, miniH});
        m_thumbPreviewClip->setAnchorPoint({0.5f, 0.5f});
        m_thumbPreviewClip->setPosition({cx + 140.f, y});
        m_thumbnailIdRow->addChild(m_thumbPreviewClip);

        m_thumbPreviewSprite = CCSprite::create();
        m_thumbPreviewSprite->setAnchorPoint({0.5f, 0.5f});
        m_thumbPreviewSprite->setPosition({miniW / 2.f, miniH / 2.f});
        m_thumbPreviewClip->addChild(m_thumbPreviewSprite);

        auto border = paimon::SpriteHelper::createDarkPanel(miniW, miniH, 200, 2.f);
        border->setAnchorPoint({0.5f, 0.5f});
        border->setPosition({cx + 140.f, y});
        border->setColor({100, 100, 100});
        m_thumbnailIdRow->addChild(border);
    }

    // Opciones de banner
    m_bannerRow = CCNode::create();
    m_bannerRow->setPosition({0, 0});
    m_mainLayer->addChild(m_bannerRow);

    {
        auto bannerMenu = CCMenu::create();
        bannerMenu->setPosition({0, 0});
        m_bannerRow->addChild(bannerMenu);

        auto bgOffSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png");
        if (!bgOffSpr) bgOffSpr = CCSprite::create();
        bgOffSpr->setScale(0.4f);
        auto bgOnSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png");
        if (!bgOnSpr) bgOnSpr = CCSprite::create();
        bgOnSpr->setScale(0.4f);
        m_toggleBannerBg = CCMenuItemToggler::create(bgOffSpr, bgOnSpr, this, menu_selector(CommentBgSettingsPopup::onToggleBannerBg));
        m_toggleBannerBg->setPosition({cx - 60.f, y});
        bannerMenu->addChild(m_toggleBannerBg);

        auto bgLabel = CCLabelBMFont::create("Background", "bigFont.fnt");
        bgLabel->setScale(0.22f);
        bgLabel->setPosition({cx - 60.f, y - 14.f});
        m_bannerRow->addChild(bgLabel);

        auto imgOffSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_paintBtn_001.png");
        if (!imgOffSpr) imgOffSpr = CCSprite::create();
        imgOffSpr->setScale(0.4f);
        auto imgOnSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_paintBtn_001.png");
        if (!imgOnSpr) imgOnSpr = CCSprite::create();
        imgOnSpr->setScale(0.4f);
        m_toggleBannerImg = CCMenuItemToggler::create(imgOffSpr, imgOnSpr, this, menu_selector(CommentBgSettingsPopup::onToggleBannerImg));
        m_toggleBannerImg->setPosition({cx + 60.f, y});
        bannerMenu->addChild(m_toggleBannerImg);

        auto imgLabel = CCLabelBMFont::create("Image", "bigFont.fnt");
        imgLabel->setScale(0.22f);
        imgLabel->setPosition({cx + 60.f, y - 14.f});
        m_bannerRow->addChild(imgLabel);
    }

    // Selector de color
    m_solidColorRow = CCNode::create();
    m_solidColorRow->setPosition({0, 0});
    m_mainLayer->addChild(m_solidColorRow);

    {
        auto colorLabel = CCLabelBMFont::create("Color:", "bigFont.fnt");
        colorLabel->setScale(0.28f);
        colorLabel->setPosition({cx - 130.f, y});
        m_solidColorRow->addChild(colorLabel);

        m_colorPreviewSprite = CCSprite::create("GJ_colorBtn_001.png");
        if (!m_colorPreviewSprite) m_colorPreviewSprite = CCSprite::create();
        float maxDim = std::max(m_colorPreviewSprite->getContentWidth(), m_colorPreviewSprite->getContentHeight());
        if (maxDim > 0) m_colorPreviewSprite->setScale(20.f / maxDim);
        m_colorPreviewSprite->setColor(m_configPtr->commentBgSolidColor);

        auto pickBtn = CCMenuItemSpriteExtra::create(m_colorPreviewSprite, this, menu_selector(CommentBgSettingsPopup::onPickColor));
        pickBtn->setPosition({cx + 10.f, y});
        auto colorMenu = CCMenu::create();
        colorMenu->setPosition({0, 0});
        colorMenu->addChild(pickBtn);
        m_solidColorRow->addChild(colorMenu);

        auto pickLabel = CCLabelBMFont::create("Pick", "bigFont.fnt");
        pickLabel->setScale(0.22f);
        pickLabel->setPosition({cx + 10.f, y - 14.f});
        m_solidColorRow->addChild(pickLabel);
    }

    y -= 32.f;

    // Sliders de configuracion
    {
        float leftX = cx - 95.f;
        float rightX = cx + 95.f;

        // Blur (left half)
        auto blurLbl = CCLabelBMFont::create("Blur:", "bigFont.fnt");
        blurLbl->setScale(0.24f);
        blurLbl->setPosition({leftX - 80.f, y});
        m_mainLayer->addChild(blurLbl);

        m_blurSlider = Slider::create(this, menu_selector(CommentBgSettingsPopup::onBlurChanged), 1.0f);
        m_blurSlider->setPosition({leftX, y});
        m_blurSlider->setValue(m_configPtr->commentBgBlur / 10.f);
        m_mainLayer->addChild(m_blurSlider);

        m_blurLabel = CCLabelBMFont::create(fmt::format("{:.1f}", m_configPtr->commentBgBlur).c_str(), "bigFont.fnt");
        m_blurLabel->setScale(0.22f);
        m_blurLabel->setPosition({leftX + 80.f, y});
        m_mainLayer->addChild(m_blurLabel);

        // Darkness (right half)
        auto darkLbl = CCLabelBMFont::create("Dark:", "bigFont.fnt");
        darkLbl->setScale(0.24f);
        darkLbl->setPosition({rightX - 80.f, y});
        m_mainLayer->addChild(darkLbl);

        m_darknessSlider = Slider::create(this, menu_selector(CommentBgSettingsPopup::onDarknessChanged), 1.0f);
        m_darknessSlider->setPosition({rightX, y});
        m_darknessSlider->setValue(m_configPtr->commentBgDarkness);
        m_mainLayer->addChild(m_darknessSlider);

        m_darknessLabel = CCLabelBMFont::create(fmt::format("{:.2f}", m_configPtr->commentBgDarkness).c_str(), "bigFont.fnt");
        m_darknessLabel->setScale(0.22f);
        m_darknessLabel->setPosition({rightX + 80.f, y});
        m_mainLayer->addChild(m_darknessLabel);
    }

    y -= 28.f;

    // Selector de tipo de blur
    {
        auto* blurMenu = CCMenu::create();
        blurMenu->setPosition({0, 0});
        m_mainLayer->addChild(blurMenu);

        auto blurTypeLbl = CCLabelBMFont::create("Blur Type:", "bigFont.fnt");
        blurTypeLbl->setScale(0.22f);
        blurTypeLbl->setPosition({cx - 110.f, y});
        m_mainLayer->addChild(blurTypeLbl);

        auto gaussOffSpr = ButtonSprite::create("Gaussian", "bigFont.fnt", "GJ_button_05.png", 0.65f);
        auto gaussOnSpr  = ButtonSprite::create("Gaussian", "bigFont.fnt", "GJ_button_02.png", 0.65f);
        m_toggleBlurGaussian = CCMenuItemToggler::create(gaussOffSpr, gaussOnSpr, this, menu_selector(CommentBgSettingsPopup::onToggleBlurGaussian));
        m_toggleBlurGaussian->setScale(0.55f);
        m_toggleBlurGaussian->setPosition({cx - 25.f, y});
        blurMenu->addChild(m_toggleBlurGaussian);

        auto paimonOffSpr = ButtonSprite::create("Paimon", "bigFont.fnt", "GJ_button_05.png", 0.65f);
        auto paimonOnSpr  = ButtonSprite::create("Paimon", "bigFont.fnt", "GJ_button_02.png", 0.65f);
        m_toggleBlurPaimon = CCMenuItemToggler::create(paimonOffSpr, paimonOnSpr, this, menu_selector(CommentBgSettingsPopup::onToggleBlurPaimon));
        m_toggleBlurPaimon->setScale(0.55f);
        m_toggleBlurPaimon->setPosition({cx + 55.f, y});
        blurMenu->addChild(m_toggleBlurPaimon);

        // Set initial toggle state
        if (m_configPtr->commentBgBlurType == "paimon") {
            m_toggleBlurGaussian->toggle(true);
        } else {
            m_toggleBlurPaimon->toggle(true);
        }
    }

    y -= 35.f;

    // Preview en tiempo real
    {
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(kPreviewWidth, kPreviewHeight, kPreviewRadius);
        m_previewClip = CCClippingNode::create(stencil);
        m_previewClip->setContentSize({kPreviewWidth, kPreviewHeight});
        m_previewClip->setAnchorPoint({0.5f, 0.5f});
        m_previewClip->setPosition({cx, y - kPreviewHeight / 2.f});
        m_previewClip->setAlphaThreshold(0.5f);
        m_mainLayer->addChild(m_previewClip);

        // Background container (inside clip)
        m_previewBgContainer = CCNode::create();
        m_previewBgContainer->setContentSize({kPreviewWidth, kPreviewHeight});
        m_previewBgContainer->setAnchorPoint({0, 0});
        m_previewBgContainer->setPosition({0, 0});
        m_previewClip->addChild(m_previewBgContainer);

        // Foreground container (outside clip, on top)
        m_previewNode = CCNode::create();
        m_previewNode->setContentSize({kPreviewWidth, kPreviewHeight});
        m_previewNode->setAnchorPoint({0.5f, 0.5f});
        m_previewNode->setPosition({cx, y - kPreviewHeight / 2.f});
        m_mainLayer->addChild(m_previewNode);

        // User icon (circle)
        auto iconBg = CCSprite::create("GJ_playerIcon.png");
        if (!iconBg) iconBg = CCSprite::create("GJ_button_05.png");
        if (iconBg) {
            float iconSize = 18.f;
            float maxDim = std::max(iconBg->getContentWidth(), iconBg->getContentHeight());
            if (maxDim > 0) iconBg->setScale(iconSize / maxDim);
            iconBg->setAnchorPoint({0, 0.5f});
            iconBg->setPosition({-kPreviewWidth / 2.f + 8.f, 4.f});
            m_previewIcon = iconBg;
            m_previewNode->addChild(iconBg);
        }

        // Username
        m_previewUsername = CCLabelBMFont::create("Player", "goldFont.fnt");
        m_previewUsername->setScale(0.28f);
        m_previewUsername->setAnchorPoint({0, 0.5f});
        m_previewUsername->setPosition({-kPreviewWidth / 2.f + 30.f, 8.f});
        m_previewUsername->setColor({255, 255, 100});
        m_previewNode->addChild(m_previewUsername);

        // Comment text
        m_previewComment = CCLabelBMFont::create("hola mundo w", "chatFont.fnt");
        m_previewComment->setScale(0.35f);
        m_previewComment->setAnchorPoint({0, 0.5f});
        m_previewComment->setPosition({-kPreviewWidth / 2.f + 30.f, -8.f});
        m_previewNode->addChild(m_previewComment);

        // Border around preview (outline-only, doesn't cover content)
        auto border = paimon::SpriteHelper::createRoundedRectOutline(
            kPreviewWidth, kPreviewHeight, kPreviewRadius,
            {60.f / 255.f, 60.f / 255.f, 60.f / 255.f, 200.f / 255.f}, 1.5f);
        border->setAnchorPoint({0.5f, 0.5f});
        border->setPosition({cx, y - kPreviewHeight / 2.f});
        m_mainLayer->addChild(border);
    }

    y -= (kPreviewHeight + 16.f);

    // ── Save button ──
    {
        auto saveBtnSpr = ButtonSprite::create("Save", "bigFont.fnt", "GJ_button_05.png");
        auto saveBtn = CCMenuItemSpriteExtra::create(saveBtnSpr, this, menu_selector(CommentBgSettingsPopup::onSave));
        saveBtn->setPosition({cx, y});
        menu->addChild(saveBtn);
    }

    // Initial state
    updateTypeSelection();
    updateConditionalRows();
    refreshPreview();

    // Fetch thumbnails if we have a level ID
    if (m_configPtr->commentBgType == "thumbnail" && !m_configPtr->commentBgThumbnailId.empty()) {
        fetchThumbnailsForLevel();
    }

    this->setZOrder(10600);
    this->setID("comment-bg-settings-popup"_spr);
    return true;
}

// ── Type selection ──

void CommentBgSettingsPopup::onSelectType(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;

    if (btn == m_btnNone)            m_configPtr->commentBgType = "none";
    else if (btn == m_btnThumbnail)  m_configPtr->commentBgType = "thumbnail";
    else if (btn == m_btnBanner)     m_configPtr->commentBgType = "banner";
    else if (btn == m_btnSolid)      m_configPtr->commentBgType = "solid";

    updateTypeSelection();
    updateConditionalRows();
    refreshPreview();

    if (m_configPtr->commentBgType == "thumbnail" && !m_configPtr->commentBgThumbnailId.empty()) {
        fetchThumbnailsForLevel();
    }
}

void CommentBgSettingsPopup::updateTypeSelection() {
    auto highlight = [](CCMenuItemSpriteExtra* btn, bool selected) {
        if (!btn) return;
        auto* spr = typeinfo_cast<CCSprite*>(btn->getNormalImage());
        if (spr) spr->setColor(selected ? ccWHITE : ccc3(120, 120, 120));
    };

    highlight(m_btnNone,      m_configPtr->commentBgType == "none");
    highlight(m_btnThumbnail, m_configPtr->commentBgType == "thumbnail");
    highlight(m_btnBanner,    m_configPtr->commentBgType == "banner");
    highlight(m_btnSolid,     m_configPtr->commentBgType == "solid");

    std::string typeName = "None";
    if (m_configPtr->commentBgType == "thumbnail") typeName = "Thumbnail";
    else if (m_configPtr->commentBgType == "banner") typeName = "Banner";
    else if (m_configPtr->commentBgType == "solid") typeName = "Solid Color";

    if (m_typeLabel) {
        m_typeLabel->setString(fmt::format("Type: {}", typeName).c_str());
    }
}

void CommentBgSettingsPopup::updateConditionalRows() {
    if (m_thumbnailIdRow) m_thumbnailIdRow->setVisible(m_configPtr->commentBgType == "thumbnail");
    if (m_bannerRow)      m_bannerRow->setVisible(m_configPtr->commentBgType == "banner");
    if (m_solidColorRow)  m_solidColorRow->setVisible(m_configPtr->commentBgType == "solid");

    // Update banner toggles
    if (m_toggleBannerBg)  m_toggleBannerBg->toggle(m_configPtr->commentBgBannerMode != "background");
    if (m_toggleBannerImg) m_toggleBannerImg->toggle(m_configPtr->commentBgBannerMode != "image");
}

// ── Thumbnail position navigation ──

void CommentBgSettingsPopup::onThumbPrev(CCObject*) {
    if (m_configPtr->commentBgThumbnailPos > 1) {
        m_configPtr->commentBgThumbnailPos--;
        updateThumbnailPreview();
        refreshPreview();
    }
}

void CommentBgSettingsPopup::onThumbNext(CCObject*) {
    if (m_configPtr->commentBgThumbnailPos < static_cast<int>(m_cachedThumbnails.size())) {
        m_configPtr->commentBgThumbnailPos++;
        updateThumbnailPreview();
        refreshPreview();
    }
}

void CommentBgSettingsPopup::fetchThumbnailsForLevel() {
    if (!m_inputField) return;
    std::string idStr = m_inputField->getString();
    if (idStr.empty()) {
        m_cachedThumbnails.clear();
        updateThumbnailPreview();
        return;
    }

    int levelId = 0;
    auto levelIdRes = geode::utils::numFromString<int>(idStr);
    if (!levelIdRes) return;
    levelId = levelIdRes.unwrap();

    int token = ++m_thumbRequestToken;
    WeakRef<CommentBgSettingsPopup> weakSelf = this;

    ThumbnailAPI::get().getThumbnails(levelId,
        [weakSelf, token, this](bool success, std::vector<ThumbnailInfo> const& thumbs) {
            if (token != m_thumbRequestToken) return;
            auto self = weakSelf.lock();
            if (!self) return;
            if (success && !thumbs.empty()) {
                m_cachedThumbnails = thumbs;
                if (m_configPtr->commentBgThumbnailPos > static_cast<int>(thumbs.size())) {
                    m_configPtr->commentBgThumbnailPos = 1;
                }
            } else {
                m_cachedThumbnails.clear();
            }
            updateThumbnailPreview();
            refreshPreview();
        }
    );
}

void CommentBgSettingsPopup::updateThumbnailPreview() {
    int total = static_cast<int>(m_cachedThumbnails.size());
    if (m_thumbPosLabel) {
        if (total > 0) {
            m_thumbPosLabel->setString(fmt::format("{}/{}", m_configPtr->commentBgThumbnailPos, total).c_str());
        } else {
            m_thumbPosLabel->setString("0/0");
        }
    }

    // Update mini preview sprite
    if (m_thumbPreviewSprite) {
        m_thumbPreviewSprite->setTexture(nullptr);
        m_thumbPreviewSprite->setVisible(false);

        if (total > 0 && m_configPtr->commentBgThumbnailPos >= 1 && m_configPtr->commentBgThumbnailPos <= total) {
            auto& thumb = m_cachedThumbnails[m_configPtr->commentBgThumbnailPos - 1];
            if (!thumb.url.empty()) {
                // Load thumbnail from URL
                WeakRef<CommentBgSettingsPopup> weakSelf = this;
                HttpClient::get().downloadFromUrl(thumb.url,
                    [weakSelf](bool success, std::vector<uint8_t> const& data, int, int) {
                        auto self = weakSelf.lock();
                        if (!self || !self->m_thumbPreviewSprite) return;
                        if (success && !data.empty()) {
                            auto* tex = ThumbnailTransportClient::bytesToTexture(data);
                            if (tex) {
                                float w = tex->getContentSize().width;
                                float h = tex->getContentSize().height;
                                self->m_thumbPreviewSprite->setTexture(tex);
                                self->m_thumbPreviewSprite->setTextureRect({0, 0, w, h});
                                float scaleX = 60.f / std::max(1.f, w);
                                float scaleY = 30.f / std::max(1.f, h);
                                self->m_thumbPreviewSprite->setScale(std::max(scaleX, scaleY));
                                self->m_thumbPreviewSprite->setVisible(true);
                            }
                        }
                    }
                );
            }
        }
    }
}

// ── Color picker ──

void CommentBgSettingsPopup::onPickColor(CCObject*) {
    auto popup = geode::ColorPickPopup::create(m_configPtr->commentBgSolidColor);
    if (!popup) return;

    popup->setCallback([this](cocos2d::ccColor4B const& color) {
        onColorSelected({color.r, color.g, color.b});
    });

    popup->show();
}

void CommentBgSettingsPopup::onColorSelected(cocos2d::ccColor3B color) {
    m_configPtr->commentBgSolidColor = color;
    if (m_colorPreviewSprite) {
        m_colorPreviewSprite->setColor(color);
    }
    refreshPreview();
}

// ── Banner toggles ──

void CommentBgSettingsPopup::onToggleBannerBg(CCObject*) {
    m_configPtr->commentBgBannerMode = "background";
    if (m_toggleBannerBg)  m_toggleBannerBg->toggle(false);
    if (m_toggleBannerImg) m_toggleBannerImg->toggle(true);
    refreshPreview();
}

void CommentBgSettingsPopup::onToggleBannerImg(CCObject*) {
    m_configPtr->commentBgBannerMode = "image";
    if (m_toggleBannerBg)  m_toggleBannerBg->toggle(true);
    if (m_toggleBannerImg) m_toggleBannerImg->toggle(false);
    refreshPreview();
}

// ── Sliders ──

void CommentBgSettingsPopup::onBlurChanged(CCObject* sender) {
    auto* slider = typeinfo_cast<Slider*>(sender);
    if (!slider) return;
    m_configPtr->commentBgBlur = slider->getValue() * 10.f;
    refreshSliderLabels();
    refreshPreview();
}

void CommentBgSettingsPopup::onDarknessChanged(CCObject* sender) {
    auto* slider = typeinfo_cast<Slider*>(sender);
    if (!slider) return;
    m_configPtr->commentBgDarkness = slider->getValue();
    refreshSliderLabels();
    refreshPreview();
}

void CommentBgSettingsPopup::onToggleBlurGaussian(CCObject*) {
    m_configPtr->commentBgBlurType = "gaussian";
    if (m_toggleBlurPaimon) m_toggleBlurPaimon->toggle(true);
    refreshPreview();
}

void CommentBgSettingsPopup::onToggleBlurPaimon(CCObject*) {
    m_configPtr->commentBgBlurType = "paimon";
    if (m_toggleBlurGaussian) m_toggleBlurGaussian->toggle(true);
    refreshPreview();
}

void CommentBgSettingsPopup::refreshSliderLabels() {
    if (m_blurLabel)    m_blurLabel->setString(fmt::format("{:.1f}", m_configPtr->commentBgBlur).c_str());
    if (m_darknessLabel) m_darknessLabel->setString(fmt::format("{:.2f}", m_configPtr->commentBgDarkness).c_str());
}

// ── Real-time preview ──

void CommentBgSettingsPopup::refreshPreview() {
    if (!m_previewBgContainer) return;

    // Limpia el fondo previo
    m_previewBgContainer->removeAllChildren();

    // Invalida callbacks anteriores
    int token = ++m_previewToken;

    float pw = kPreviewWidth;
    float ph = kPreviewHeight;

    if (m_configPtr->commentBgType == "none") {
        // Panel oscuro por defecto
        auto panel = paimon::SpriteHelper::createDarkPanel(pw, ph, 92, kPreviewRadius);
        if (panel) {
            panel->setAnchorPoint({0, 0});
            panel->setPosition({0, 0});
            m_previewBgContainer->addChild(panel);
        }
    }
    else if (m_configPtr->commentBgType == "solid") {
        auto panel = paimon::SpriteHelper::createColorPanel(pw, ph,
            m_configPtr->commentBgSolidColor,
            static_cast<GLubyte>(m_configPtr->commentBgSolidOpacity),
            kPreviewRadius);
        if (panel) {
            panel->setAnchorPoint({0, 0});
            panel->setPosition({0, 0});
            m_previewBgContainer->addChild(panel);
        }
    }
    else if (m_configPtr->commentBgType == "thumbnail") {
        // Muestra preview de miniatura
        if (!m_cachedThumbnails.empty() && m_configPtr->commentBgThumbnailPos >= 1
            && m_configPtr->commentBgThumbnailPos <= static_cast<int>(m_cachedThumbnails.size())) {
            auto& thumb = m_cachedThumbnails[m_configPtr->commentBgThumbnailPos - 1];
            if (!thumb.url.empty()) {
                WeakRef<CommentBgSettingsPopup> weakSelf = this;
                HttpClient::get().downloadFromUrl(thumb.url,
                    [weakSelf, token, pw, ph](bool success, std::vector<uint8_t> const& data, int, int) {
                        auto self = weakSelf.lock();
                        if (!self || !self->m_previewBgContainer) return;
                        // Callback obsoleto, lo ignora
                        if (self->m_previewToken != token) return;
                        if (success && !data.empty()) {
                            auto* tex = ThumbnailTransportClient::bytesToTexture(data);
                            if (tex) {
                                bool usePaimon = (self->m_configPtr->commentBgBlurType == "paimon");
                                auto blurred = usePaimon
                                    ? BlurSystem::getInstance()->createPaimonBlurSprite(
                                        tex, {std::max(pw, 512.f), std::max(ph, 256.f)}, self->m_configPtr->commentBgBlur)
                                    : BlurSystem::getInstance()->createBlurredSprite(
                                        tex, {std::max(pw, 512.f), std::max(ph, 256.f)}, self->m_configPtr->commentBgBlur);
                                if (blurred) {
                                    float scaleX = pw / blurred->getContentWidth();
                                    float scaleY = ph / blurred->getContentHeight();
                                    blurred->setScale(std::max(scaleX, scaleY));
                                    blurred->setAnchorPoint({0.5f, 0.5f});
                                    blurred->setPosition({pw / 2.f, ph / 2.f});
                                    self->m_previewBgContainer->addChild(blurred);

                                    if (self->m_configPtr->commentBgDarkness > 0.f) {
                                        auto overlay = CCLayerColor::create(
                                            {0, 0, 0, static_cast<GLubyte>(self->m_configPtr->commentBgDarkness * 255)});
                                        overlay->setContentSize({pw, ph});
                                        overlay->setAnchorPoint({0, 0});
                                        overlay->setPosition({0, 0});
                                        self->m_previewBgContainer->addChild(overlay);
                                    }
                                }
                            }
                        }
                    }
                );
            }
        } else {
            // Sin miniatura cargada: placeholder
            auto panel = paimon::SpriteHelper::createDarkPanel(pw, ph, 92, kPreviewRadius);
            if (panel) {
                panel->setAnchorPoint({0, 0});
                panel->setPosition({0, 0});
                m_previewBgContainer->addChild(panel);
            }
        }
    }
    else if (m_configPtr->commentBgType == "banner") {
        // Usa el fondo de perfil como preview
        auto& thumbs = ProfileThumbs::get();
        if (auto* cached = thumbs.getCachedProfile(m_accountID)) {
            CCTexture2D* tex = cached->texture.data();
            if (tex) {
                bool usePaimon = (m_configPtr->commentBgBlurType == "paimon");
                auto blurred = usePaimon
                    ? BlurSystem::getInstance()->createPaimonBlurSprite(
                        tex, {std::max(pw, 512.f), std::max(ph, 256.f)}, m_configPtr->commentBgBlur)
                    : BlurSystem::getInstance()->createBlurredSprite(
                        tex, {std::max(pw, 512.f), std::max(ph, 256.f)}, m_configPtr->commentBgBlur);
                if (blurred) {
                    float scaleX = pw / blurred->getContentWidth();
                    float scaleY = ph / blurred->getContentHeight();
                    blurred->setScale(std::max(scaleX, scaleY));
                    blurred->setAnchorPoint({0.5f, 0.5f});
                    blurred->setPosition({pw / 2.f, ph / 2.f});
                    m_previewBgContainer->addChild(blurred);
                }
            }
            if (!cached->gifKey.empty()) {
                auto gifLabel = CCLabelBMFont::create("GIF", "bigFont.fnt");
                gifLabel->setScale(0.3f);
                gifLabel->setPosition({pw / 2.f, ph / 2.f});
                gifLabel->setColor(ccc3(150, 150, 150));
                m_previewBgContainer->addChild(gifLabel);
            }
        } else {
            // Sin perfil en cache: placeholder
            auto panel = paimon::SpriteHelper::createDarkPanel(pw, ph, 92, kPreviewRadius);
            if (panel) {
                panel->setAnchorPoint({0, 0});
                panel->setPosition({0, 0});
                m_previewBgContainer->addChild(panel);
            }
        }

        // Oscurece el fondo
        if (m_configPtr->commentBgDarkness > 0.f) {
            auto overlay = CCLayerColor::create(
                {0, 0, 0, static_cast<GLubyte>(m_configPtr->commentBgDarkness * 255)});
            overlay->setContentSize({pw, ph});
            overlay->setAnchorPoint({0, 0});
            overlay->setPosition({0, 0});
            m_previewBgContainer->addChild(overlay);
        }
    }
}

// ── Save ──

void CommentBgSettingsPopup::onSave(CCObject*) {
    // Lee el ID del input si aplica
    if (m_configPtr->commentBgType == "thumbnail" && m_inputField) {
        m_configPtr->commentBgThumbnailId = m_inputField->getString();
        if (m_configPtr->commentBgThumbnailId.empty()) {
            PaimonNotify::show("Please enter a Level ID for the thumbnail", NotificationIcon::Warning);
            return;
        }
    }

    // Sube la config al servidor
    WeakRef<CommentBgSettingsPopup> weakSelf = this;
    int accountID = m_accountID;
    ProfileConfig config = *m_configPtr;

    ThumbnailAPI::get().uploadProfileConfig(accountID, config,
        [weakSelf](bool success, std::string const& msg) {
            auto self = weakSelf.lock();
            if (!self) return;
            if (success) {
                PaimonNotify::show("Comment background saved!", NotificationIcon::Success);
                self->onClose(nullptr);
            } else {
                PaimonNotify::show(("Failed to save: " + msg).c_str(), NotificationIcon::Error);
            }
        }
    );
}
