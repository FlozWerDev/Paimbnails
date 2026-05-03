#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <vector>
#include <cmath>
#include <filesystem>
#include <unordered_set>

#include "../../../managers/ThumbnailAPI.hpp"
#include "../../moderation/services/PendingQueue.hpp"

namespace cocos2d {
class CCTouch;
class CCEvent;
}

// Targeted type imports to avoid namespace pollution in headers
using cocos2d::CCTexture2D;
using cocos2d::CCNode;
using cocos2d::CCPoint;
using cocos2d::CCLabelBMFont;
using cocos2d::CCSize;
using cocos2d::CCObject;

/**
 * Popup de visualizacion de thumbnails con zoom/pan tactil y galeria.
 * Extraido de LevelInfoLayer.cpp para mantener los hooks ligeros
 * (patron BetterInfo-Geode).
 */
class LocalThumbnailViewPopup : public geode::Popup, public FLAlertLayerProtocol {
public:
    enum class NavDirection : uint8_t { None = 0, Left, Right };

protected:
    int32_t m_levelID = 0;
    bool m_canAcceptUpload = false;
    bool m_isAdmin = false;
    geode::Ref<CCTexture2D> m_thumbnailTexture = nullptr;
    cocos2d::CCNode* m_clippingNode = nullptr;
    CCNode* m_thumbnailSprite = nullptr;
    float m_initialScale = 1.0f;
    float m_maxScale = 4.0f;
    float m_minScale = 0.5f;
    std::unordered_set<cocos2d::CCTouch*> m_touches;
    float m_initialDistance = 0.0f;
    float m_savedScale = 1.0f;
    CCPoint m_touchMidPoint = {0, 0};
    bool m_wasZooming = false;
    bool m_isExiting = false;
    int m_verificationCategory = -1;
    cocos2d::CCMenuItem* m_activatedItem = nullptr;

    float m_viewWidth = 0.0f;
    float m_viewHeight = 0.0f;

    // votacion
    cocos2d::CCMenu* m_ratingMenu = nullptr;
    cocos2d::CCMenu* m_buttonMenu = nullptr;
    cocos2d::CCMenu* m_settingsMenu = nullptr;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    int m_userVote = 0;
    int m_initialUserVote = 0;
    bool m_isVoting = false;

    // galeria
    std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
    bool m_isDownloading = false;
    bool m_refreshCooldownActive = false;
    int m_galleryRequestToken = 0;
    int m_invalidationListenerId = 0;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_ytRequestHolder;

    // galeria local (multi-thumbnail)
    std::vector<std::string> m_localThumbPaths;
    int m_localCurrentIndex = 0;
    bool m_viewingLocal = false; // true = navegando locales, false = navegando remotos

    std::vector<Suggestion> m_suggestions;
    int m_currentIndex = 0;
    std::string m_cachedInfoId;
    CCMenuItemSpriteExtra* m_leftArrow = nullptr;
    CCMenuItemSpriteExtra* m_rightArrow = nullptr;
    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;
    CCLabelBMFont* m_counterLabel = nullptr;
    NavDirection m_navDirection = NavDirection::None;

    CCMenuItemSpriteExtra* m_orderEditBtn = nullptr;

    // video playback
    cocos2d::CCMenu* m_playBtnMenu = nullptr;
    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    bool m_videoPlaying = false;

    // --- metodos protegidos ---
    bool isUiAlive();

    void onPrev(CCObject*);
    void onNext(CCObject*);
    void onInfo(CCObject*);
    void loadThumbnailAt(int index);

    ~LocalThumbnailViewPopup();

    void loadCurrentSuggestion();
    void onNextSuggestion(CCObject*);
    void onPrevSuggestion(CCObject*);

    void onExit() override;
    void setupRating();
    void onRate(CCObject*);

    bool init(float width, float height);
    void setup(std::pair<int32_t, bool> const& data);

    void loadFromVerificationQueue(PendingCategory category, float maxWidth, float maxHeight, CCSize content, bool openedFromReport);
    void tryLoadFromMultipleSources(float maxWidth, float maxHeight, CCSize content, bool openedFromReport);
    bool tryLoadFromCache(float maxWidth, float maxHeight, CCSize content, bool openedFromReport);
    void loadFromThumbnailLoader(float maxWidth, float maxHeight, CCSize content, bool openedFromReport);
    void tryDirectServerDownload(float maxWidth, float maxHeight, CCSize content, bool openedFromReport);
    void displayThumbnail(CCTexture2D* tex, float maxWidth, float maxHeight, CCSize content, bool openedFromReport);
    void displayVideoThumbnail(class VideoThumbnailSprite* videoSprite, float maxWidth, float maxHeight, CCSize content);
    void showNoThumbnail(CCSize content);
    void clearGalleryDisplay();
    void applyPopupTransition(cocos2d::CCNode* newSprite, cocos2d::CCNode* oldSprite, float maxWidth);

    void onDownloadBtn(CCObject*);
    void onDeleteReportedThumb(CCObject*);
    void onAcceptThumbBtn(CCObject*);
    void onRejectThumbBtn(CCObject*);
    void onReportBtn(CCObject*);
    void onDeleteThumbnail(CCObject*);
    void onDeleteLocalThumb(CCObject*);
    void onYouTubeBtn(CCObject*);
    void onRefreshBtn(CCObject*);
    void updateRefreshButtonState();
    void startRefreshCooldown();
    void onPlayVideo(CCObject*);
    void updateOrderUiState();
    void replaceRemoteThumbnails(std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs, std::string const& preferredId = "");
    void ensureOrderControls(float contentWidth);
    void onOrderEdit(CCObject*);
    void updatePlayButton();

    // declarada aqui, implementada en LevelInfoLayer.cpp (necesita PaimonLevelInfoLayer)
    void onSettings(CCObject*);

    void onRecenter(CCObject*);

    // FLAlertLayerProtocol — handles Copy ID button in onInfo popup
    void FLAlert_Clicked(FLAlertLayer* alert, bool btn2) override;

    static float clamp(float value, float min, float max);
    void clampSpritePosition();
    void clampSpritePositionAnimated();
    bool applyZoomAtWorldPoint(float targetScale, CCPoint worldPoint);
    void syncZoomGestureState();
    void resetZoomGestureState();
    CCPoint getZoomFocusPoint() const;

    // touch zoom/pan
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void scrollWheel(float x, float y) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

public:
    void setSuggestions(std::vector<Suggestion> const& suggestions);

    static LocalThumbnailViewPopup* create(int32_t levelID, bool canAcceptUpload);
};

// funcion exportada, usada desde VerificationCenterLayer y otros
CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, std::vector<Suggestion> const& suggestions);
