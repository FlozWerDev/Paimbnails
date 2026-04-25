#include "LocalThumbnailViewPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../framework/state/SessionState.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/platform/platform.hpp>
#include <sstream>
#include <thread>

#include "../services/LocalThumbs.hpp"
#include "../../moderation/services/PendingQueue.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../services/ThumbnailLoader.hpp"
#include "../services/ThumbnailCache.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/Assets.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/Constants.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/RenderTexture.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../utils/UIBorderHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../profiles/ui/RatePopup.hpp"
#include "ReportInputPopup.hpp"
#include "ThumbnailSettingsPopup.hpp"
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
constexpr float kThumbnailRefreshCooldown = 5.f;
constexpr GLubyte kRefreshBtnEnabledOpacity = 255;
constexpr GLubyte kRefreshBtnDisabledOpacity = 105;
}

void LocalThumbnailViewPopup::updateRefreshButtonState() {
    if (!m_refreshBtn) return;

    bool enabled = !m_refreshCooldownActive && !m_isDownloading;
    m_refreshBtn->setEnabled(enabled);
    m_refreshBtn->setOpacity(enabled ? kRefreshBtnEnabledOpacity : kRefreshBtnDisabledOpacity);

    if (auto image = typeinfo_cast<CCSprite*>(m_refreshBtn->getNormalImage())) {
        image->setOpacity(enabled ? kRefreshBtnEnabledOpacity : kRefreshBtnDisabledOpacity);
    }
}

void LocalThumbnailViewPopup::startRefreshCooldown() {
    m_refreshCooldownActive = true;
    updateRefreshButtonState();

    WeakRef<LocalThumbnailViewPopup> self = this;
    paimon::scheduleMainThreadDelay(kThumbnailRefreshCooldown, [self]() {
        auto popup = self.lock();
        if (!popup || !popup->getParent()) return;
        popup->m_refreshCooldownActive = false;
        popup->updateRefreshButtonState();
    });
}

bool LocalThumbnailViewPopup::isUiAlive() {
    return !m_isExiting && this->getParent() && m_mainLayer;
}


void LocalThumbnailViewPopup::onPrev(CCObject*) {
    m_navDirection = NavDirection::Left;
    if (m_viewingLocal && !m_localThumbPaths.empty()) {
        int count = static_cast<int>(m_localThumbPaths.size());
        m_localCurrentIndex--;
        if (m_localCurrentIndex < 0) m_localCurrentIndex = count - 1;

        auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
        if (tex) {
            auto content = m_mainLayer->getContentSize();
            float maxWidth = content.width - 40.f;
            float maxHeight = content.height - 80.f;
            displayThumbnail(tex, maxWidth, maxHeight, content, false);
        }
        if (m_counterLabel) {
            m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, count).c_str());
        }
        return;
    }

    if (m_thumbnails.empty() || m_isDownloading) return;
    m_currentIndex--;
    if (m_currentIndex < 0) m_currentIndex = static_cast<int>(m_thumbnails.size()) - 1;
    loadThumbnailAt(m_currentIndex);
}

void LocalThumbnailViewPopup::onNext(CCObject*) {
    m_navDirection = NavDirection::Right;
    if (m_viewingLocal && !m_localThumbPaths.empty()) {
        int count = static_cast<int>(m_localThumbPaths.size());
        m_localCurrentIndex++;
        if (m_localCurrentIndex >= count) m_localCurrentIndex = 0;

        auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
        if (tex) {
            auto content = m_mainLayer->getContentSize();
            float maxWidth = content.width - 40.f;
            float maxHeight = content.height - 80.f;
            displayThumbnail(tex, maxWidth, maxHeight, content, false);
        }
        if (m_counterLabel) {
            m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, count).c_str());
        }
        return;
    }

    if (m_thumbnails.empty() || m_isDownloading) return;
    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_thumbnails.size())) m_currentIndex = 0;
    loadThumbnailAt(m_currentIndex);
}

void LocalThumbnailViewPopup::onInfo(CCObject*) {
    std::string resStr = "Unknown";
    if (m_thumbnailTexture) {
        resStr = fmt::format("{} x {}", m_thumbnailTexture->getPixelsWide(), m_thumbnailTexture->getPixelsHigh());
    } else if (auto* sprite = typeinfo_cast<CCSprite*>(m_thumbnailSprite)) {
        // Fallback para GIF/AnimatedGIFSprite y otros sprites con textura
        if (auto* tex = sprite->getTexture()) {
            resStr = fmt::format("{} x {}", tex->getPixelsWide(), tex->getPixelsHigh());
        }
    } else if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite)) {
        auto videoSize = videoSprite->getVideoSize();
        if (videoSize.width > 1.f && videoSize.height > 1.f) {
            resStr = fmt::format("{} x {} (video)", static_cast<int>(videoSize.width), static_cast<int>(videoSize.height));
        }
    }

    std::string id = "Unknown";
    std::string type = "Static";
    std::string format = "Unknown";
    std::string creator = "Unknown";
    std::string date = "Unknown";

    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
         auto& item = m_thumbnails[m_currentIndex];
         id = item.id;
         type = item.type;
         format = item.format;
         if (!item.creator.empty()) creator = item.creator;
         if (!item.date.empty()) date = item.date;

        // fecha desde id si parece timestamp
        if (date == "Unknown" && id.length() >= 13) {
            auto numResult = geode::utils::numFromString<long long>(id);
            if (numResult.isOk()) {
                long long timestamp = numResult.unwrap();
                if (timestamp > 1600000000000) {
                    time_t timeSec = timestamp / 1000;
                    struct tm tmBuf{};
#ifdef _WIN32
                    bool tmOk = (localtime_s(&tmBuf, &timeSec) == 0);
#else
                    bool tmOk = (localtime_r(&timeSec, &tmBuf) != nullptr);
#endif
                    if (tmOk) {
                        auto tmPtr = &tmBuf;
                        date = fmt::format("{:02}/{:02}/{:02} {:02}:{:02}",
                           tmPtr->tm_mday, tmPtr->tm_mon + 1, tmPtr->tm_year % 100,
                           tmPtr->tm_hour, tmPtr->tm_min);
                    }
                }
            }
        }

         // yyyy-mm-dd -> dd/mm/aa
         if (date.length() >= 10 && date[4] == '-' && date[7] == '-') {
             std::string year = date.substr(2, 2);
             std::string month = date.substr(5, 2);
             std::string day = date.substr(8, 2);

             std::string timeStr = "";
             if (date.length() >= 16) {
                timeStr = " " + date.substr(11, 5);
             }

             date = fmt::format("{}/{}/{}{}", day, month, year, timeStr);
         }
    }

    std::string info = fmt::format(
        "<cg>ID:</c> {}\n"
        "<cy>Type:</c> {}\n"
        "<cl>Format:</c> {}\n"
        "<cp>Resolution:</c> {}\n"
        "<co>Creator:</c> {}\n"
        "<cb>Date:</c> {}",
        id, type, format, resStr, creator, date
    );

    // Store ID for copy functionality
    m_cachedInfoId = id;

    auto* alert = FLAlertLayer::create(
        this,
        "Thumbnail Info",
        info,
        "OK",
        "Copy ID",
        340.f
    );
    if (alert) alert->show();
}

void LocalThumbnailViewPopup::FLAlert_Clicked(FLAlertLayer* alert, bool btn2) {
    // btn2 = true means second button ("Copy ID") was clicked
    // btn2 = false means first button ("OK") was clicked or alert dismissed
    if (btn2 && m_cachedInfoId != "Unknown" && !m_cachedInfoId.empty()) {
        geode::utils::clipboard::write(m_cachedInfoId);
        PaimonNotify::show("ID copied to clipboard!", geode::NotificationIcon::Success);
    }
}

void LocalThumbnailViewPopup::loadThumbnailAt(int index) {
    if (index < 0 || index >= static_cast<int>(m_thumbnails.size())) return;

    m_isDownloading = true;

    int requestToken = ++m_galleryRequestToken;
    auto& thumb = m_thumbnails[index];

    // URL estable para cache: solo _pv=thumbId (sin requestToken)
    std::string url = thumb.url;
    if (!thumb.id.empty()) {
        auto sep = (url.find('?') == std::string::npos) ? "?" : "&";
        url += fmt::format("{}_pv={}", sep, thumb.id);
    }

    if (m_leftArrow) m_leftArrow->setVisible(m_thumbnails.size() > 1);
    if (m_rightArrow) m_rightArrow->setVisible(m_thumbnails.size() > 1);
    if (m_counterLabel) {
        m_counterLabel->setVisible(m_thumbnails.size() > 1);
        m_counterLabel->setString(fmt::format("{}/{}", index + 1, m_thumbnails.size()).c_str());
    }

    std::string username = "Unknown";
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    // update rating ui
    Ref<LocalThumbnailViewPopup> self = this;
    std::string requestedThumbId = thumb.id;
    ThumbnailAPI::get().getRating(m_levelID, username, requestedThumbId, [self, requestToken, requestedThumbId](bool success, float average, int count, int userVote) {
        if (!self->isUiAlive()) return;
        if (requestToken != self->m_galleryRequestToken) return;

        std::string currentThumbId = "";
        if (self->m_currentIndex >= 0 && self->m_currentIndex < static_cast<int>(self->m_thumbnails.size())) {
            currentThumbId = self->m_thumbnails[self->m_currentIndex].id;
        }
        if (currentThumbId != requestedThumbId) return;

        if (success && self->m_ratingLabel) {
            self->m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
            if (count == 0) {
                self->m_ratingLabel->setColor({255, 100, 100});
            } else {
                self->m_ratingLabel->setColor({255, 255, 255});
            }
            self->m_userVote = userVote;
            self->m_initialUserVote = userVote;
        }
    });

    // Si el thumbnail es video, descargar y reproducir con VideoThumbnailSprite
    if (thumb.isVideo() && !thumb.url.empty()) {
        log::info("[ThumbnailViewPopup] loadThumbnailAt: video detected for index={}", index);
        std::string cacheKey = fmt::format("popup_video_{}_{}", m_levelID, index);
        VideoThumbnailSprite::createAsync(thumb.url, cacheKey, [self, requestToken, index](VideoThumbnailSprite* videoSprite) {
            if (!self->isUiAlive()) return;
            if (requestToken != self->m_galleryRequestToken) return;

            self->m_isDownloading = false;

            if (!videoSprite) {
                log::warn("[ThumbnailViewPopup] loadThumbnailAt: video creation failed for index={}", index);
                auto content = self->m_mainLayer->getContentSize();
                self->showNoThumbnail(content);
                return;
            }

            auto content = self->m_mainLayer->getContentSize();
            float maxWidth = content.width - 40.f;
            float maxHeight = content.height - 80.f;

            // displayVideoThumbnail handles play/pause state and shows play button
            self->displayVideoThumbnail(videoSprite, maxWidth, maxHeight, content);

            log::info("[ThumbnailViewPopup] loadThumbnailAt: video ready for index={}", index);
        });
        return;
    }

    // Instant RAM cache check: if the URL is already cached, display immediately
    // without waiting for the async callback. This makes navigation feel instant.
    {
        auto& cache = paimon::cache::ThumbnailCache::get();
        auto ramTex = cache.getUrlFromRam(url);
        if (ramTex.has_value() && ramTex.value()) {
            log::info("[ThumbnailViewPopup] loadThumbnailAt: instant RAM cache hit for index={}", index);
            m_isDownloading = false;
            auto content = m_mainLayer->getContentSize();
            float maxWidth = content.width - 40.f;
            float maxHeight = content.height - 80.f;
            displayThumbnail(ramTex.value(), maxWidth, maxHeight, content, false);

            // Prefetch adjacent thumbnails para navegacion instantanea
            int count = static_cast<int>(m_thumbnails.size());
            if (count > 1) {
                auto prefetch = [&thumbs = m_thumbnails, count](int idx) {
                    idx = ((idx % count) + count) % count;
                    auto& t = thumbs[idx];
                    if (t.isVideo()) return;
                    std::string purl = t.url;
                    if (!t.id.empty()) {
                        auto s = (purl.find('?') == std::string::npos) ? "?" : "&";
                        purl += fmt::format("{}_pv={}", s, t.id);
                    }
                    ThumbnailLoader::get().requestUrlLoad(purl, [](CCTexture2D*, bool) {}, 0);
                };
                prefetch(index + 1);
                prefetch(index - 1);
            }
            return;
        }
    }

    // Si la URL estaba marcada como fallida, limpiar failed-cache para que
    // el usuario al abrir el popup force un reintento silencioso.
    {
        auto& cache = paimon::cache::ThumbnailCache::get();
        std::string normalizedUrl = ThumbnailLoader::normalizeUrlKey(url);
        std::string failKey = "url_" + normalizedUrl;
        if (cache.isFailed(failKey)) {
            log::info("[ThumbnailViewPopup] loadThumbnailAt: clearing failed cache for url to force retry");
            cache.clearFailed(failKey);
        }
    }

    // Usar requestUrlLoad (con cache RAM de URLs) en vez de downloadFromUrl (sin cache)
    ThumbnailLoader::get().requestUrlLoad(url, [self, requestToken, index](CCTexture2D* tex, bool success) {
        if (!self->isUiAlive()) return;
        if (requestToken != self->m_galleryRequestToken) return;

        self->m_isDownloading = false;

        if (success && tex) {
            auto content = self->m_mainLayer->getContentSize();
            float maxWidth = content.width - 40.f;
            float maxHeight = content.height - 80.f;
            self->displayThumbnail(tex, maxWidth, maxHeight, content, false);

            // Prefetch adjacent thumbnails para navegacion instantanea
            int count = static_cast<int>(self->m_thumbnails.size());
            if (count > 1) {
                auto prefetch = [&thumbs = self->m_thumbnails, count](int idx) {
                    idx = ((idx % count) + count) % count;
                    auto& t = thumbs[idx];
                    if (t.isVideo()) return; // no prefetchar videos
                    std::string purl = t.url;
                    if (!t.id.empty()) {
                        auto s = (purl.find('?') == std::string::npos) ? "?" : "&";
                        purl += fmt::format("{}_pv={}", s, t.id);
                    }
                    // Prefetch usando requestUrlLoad para que quede en cache RAM
                    ThumbnailLoader::get().requestUrlLoad(purl, [](CCTexture2D*, bool) {}, 0);
                };
                prefetch(index + 1);
                prefetch(index - 1);
            }
        } else {
            // Fallback chain antes de rendirse: local thumbs -> RAM cache por levelID
            bool fallbackApplied = false;
            if (self->m_levelID > 0) {
                auto localTex = LocalThumbs::get().loadTexture(self->m_levelID);
                if (localTex) {
                    auto content = self->m_mainLayer->getContentSize();
                    float maxWidth = content.width - 40.f;
                    float maxHeight = content.height - 80.f;
                    log::info("[ThumbnailViewPopup] Fallback to LocalThumbs for levelID={}", self->m_levelID);
                    self->displayThumbnail(localTex, maxWidth, maxHeight, content, false);
                    fallbackApplied = true;
                }
            }
            if (!fallbackApplied && self->m_levelID > 0) {
                auto& cache = paimon::cache::ThumbnailCache::get();
                auto ramTex = cache.getFromRam(self->m_levelID, false);
                if (!ramTex.has_value() || !ramTex.value()) ramTex = cache.getFromRam(self->m_levelID, true);
                if (ramTex.has_value() && ramTex.value()) {
                    auto content = self->m_mainLayer->getContentSize();
                    float maxWidth = content.width - 40.f;
                    float maxHeight = content.height - 80.f;
                    log::info("[ThumbnailViewPopup] Fallback to RAM cache for levelID={}", self->m_levelID);
                    self->displayThumbnail(ramTex.value(), maxWidth, maxHeight, content, false);
                    fallbackApplied = true;
                }
            }
            if (!fallbackApplied) {
                auto content = self->m_mainLayer->getContentSize();
                self->showNoThumbnail(content);
            }
        }
    }, 10);
}

LocalThumbnailViewPopup::~LocalThumbnailViewPopup() {
    log::info("[ThumbnailViewPopup] Destructor - liberando textura retenida");
    m_thumbnailTexture = nullptr;
    m_touches.clear();
}

void LocalThumbnailViewPopup::setSuggestions(std::vector<Suggestion> const& suggestions) {
    m_suggestions = suggestions;
    if (!m_suggestions.empty()) {
        m_currentIndex = 0;
        this->loadCurrentSuggestion();
    }
}

void LocalThumbnailViewPopup::loadCurrentSuggestion() {
    if (m_suggestions.empty()) return;

    auto& suggestion = m_suggestions[m_currentIndex];
    log::info("[ThumbnailViewPopup] Loading suggestion {}/{} - {}", m_currentIndex + 1, m_suggestions.size(), suggestion.filename);

    if (m_counterLabel) {
        m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str());
    }

    if (m_leftArrow) m_leftArrow->setVisible(m_suggestions.size() > 1);
    if (m_rightArrow) m_rightArrow->setVisible(m_suggestions.size() > 1);

    std::string url = std::string(PaimonConstants::THUMBNAIL_CDN_URL) + suggestion.filename;

    Ref<LocalThumbnailViewPopup> safeRef = this;

    ThumbnailAPI::get().downloadFromUrl(url, [safeRef](bool success, CCTexture2D* tex) {
         if (!safeRef->isUiAlive()) {
             return;
         }

         if (success && tex) {
             auto content = safeRef->m_mainLayer->getContentSize();
             float maxWidth = content.width - 40.f;
             float maxHeight = content.height - 80.f;

             safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, false);
         }
    });
}

void LocalThumbnailViewPopup::onNextSuggestion(CCObject*) {
    if (m_suggestions.empty()) return;
    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_suggestions.size())) {
        m_currentIndex = 0;
    }
    loadCurrentSuggestion();
}

void LocalThumbnailViewPopup::onPrevSuggestion(CCObject*) {
    if (m_suggestions.empty()) return;
    m_currentIndex--;
    if (m_currentIndex < 0) {
        m_currentIndex = static_cast<int>(m_suggestions.size()) - 1;
    }
    loadCurrentSuggestion();
}


void LocalThumbnailViewPopup::onExit() {
    log::info("[ThumbnailViewPopup] onExit() comenzando");

    m_touches.clear();
    resetZoomGestureState();

    if (m_isExiting) {
        log::warn("[ThumbnailViewPopup] onExit() ya fue llamado, evitando re-entrada");
        return;
    }
    m_isExiting = true;
    ++m_galleryRequestToken;
    if (m_invalidationListenerId != 0) {
        ThumbnailLoader::get().removeInvalidationListener(m_invalidationListenerId);
        m_invalidationListenerId = 0;
    }

    if (m_mainLayer) {
        m_mainLayer->removeAllChildren();
    }
    m_ratingMenu = nullptr;
    m_buttonMenu = nullptr;
    m_settingsMenu = nullptr;
    m_ratingLabel = nullptr;
    m_counterLabel = nullptr;
    m_leftArrow = nullptr;
    m_rightArrow = nullptr;

    // Stop video playback before clearing
    if (m_thumbnailSprite) {
        if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite)) {
            videoSprite->stop();
        }
    }
    m_thumbnailSprite = nullptr;
    m_clippingNode = nullptr;

    // Stop video playback and clean up play button
    m_videoPlaying = false;
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }

    log::info("[ThumbnailViewPopup] Llamando a parent onExit");
    Popup::onExit();
}


void LocalThumbnailViewPopup::setupRating() {
    if (auto node = m_mainLayer->getChildByID("rating-container"_spr)) {
        node->removeFromParent();
    }

    auto contentSize = m_mainLayer->getContentSize();

    auto ratingContainer = CCNode::create();
    ratingContainer->setID("rating-container"_spr);
    ratingContainer->setPosition({contentSize.width / 2.f, 237.f});
    m_mainLayer->addChild(ratingContainer, 100);

    auto bg = paimon::SpriteHelper::createDarkPanel(74.f, 16.f, 125);
    bg->setPosition({-37.f, -8.f});
    ratingContainer->addChild(bg, -1);

    auto starSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (!starSpr) starSpr = paimon::SpriteHelper::safeCreateWithFrameName("star_small01_001.png");
    if (starSpr) {
        starSpr->setScale(0.34f);
        starSpr->setPosition({-20.f, 0.f});
        ratingContainer->addChild(starSpr);
    }

    m_ratingLabel = CCLabelBMFont::create("...", "goldFont.fnt");
    m_ratingLabel->setScale(0.28f);
    m_ratingLabel->setPosition({8.f, 3.f});
    ratingContainer->addChild(m_ratingLabel);

    std::string username = "Unknown";
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    int requestToken = m_galleryRequestToken;
    ThumbnailAPI::get().getRating(m_levelID, username, thumbnailId, [self, requestToken, thumbnailId](bool success, float average, int count, int userVote) {
        auto popup = self.lock();
        if (!popup) return;
        if (requestToken != popup->m_galleryRequestToken) return;

        std::string currentThumbId = "";
        if (popup->m_currentIndex >= 0 && popup->m_currentIndex < static_cast<int>(popup->m_thumbnails.size())) {
            currentThumbId = popup->m_thumbnails[popup->m_currentIndex].id;
        }
        if (currentThumbId != thumbnailId) return;

        if (success) {
            log::info("[ThumbnailViewPopup] Rating found for level {}: {:.1f} ({})", popup->m_levelID, average, count);
            if (popup->m_ratingLabel) {
                popup->m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
                if (count == 0) {
                    popup->m_ratingLabel->setColor({255, 100, 100});
                } else {
                    popup->m_ratingLabel->setColor({255, 255, 255});
                }
            }
            popup->m_userVote = userVote;
            popup->m_initialUserVote = userVote;
        } else {
            log::warn("[ThumbnailViewPopup] Failed to get rating for level {}", popup->m_levelID);
        }
    });
}

void LocalThumbnailViewPopup::onRate(CCObject* sender) {
    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }
    auto popup = RatePopup::create(m_levelID, thumbnailId);
    WeakRef<LocalThumbnailViewPopup> self = this;
    popup->m_onRateCallback = [self]() {
        auto view = self.lock();
        if (!view) return;
        view->setupRating();
    };
    popup->show();
}

// Init / Setup

bool LocalThumbnailViewPopup::init(float width, float height) {
     if (!Popup::init(width, height)) return false;
     paimon::markDynamicPopup(this);
     return true;
}

void LocalThumbnailViewPopup::setup(std::pair<int32_t, bool> const& data) {
    m_levelID = data.first;
    m_canAcceptUpload = data.second;

    this->setTitle("");

    auto& vctx = paimon::SessionState::get().verification;
    bool openedFromReport    = paimon::SessionState::consumeFlag(vctx.fromReportPopup);
    int  verificationCategory = paimon::SessionState::consumeInt(vctx.verificationCategory);
    m_verificationCategory = verificationCategory;

    if (verificationCategory < 0 && m_invalidationListenerId == 0) {
        WeakRef<LocalThumbnailViewPopup> self = this;
        m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([self](int invalidLevelID) {
            auto popup = self.lock();
            if (!popup || !popup->isUiAlive() || popup->m_levelID != invalidLevelID) return;

            std::string currentId = "";
            if (popup->m_currentIndex >= 0 && popup->m_currentIndex < static_cast<int>(popup->m_thumbnails.size())) {
                currentId = popup->m_thumbnails[popup->m_currentIndex].id;
            }

            ThumbnailAPI::get().getThumbnails(popup->m_levelID, [self, currentId](bool ok, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                auto popup = self.lock();
                if (!popup || !popup->isUiAlive() || !ok || thumbs.empty()) return;

                popup->m_thumbnails = thumbs;
                int newIndex = 0;
                if (!currentId.empty()) {
                    auto it = std::find_if(
                        popup->m_thumbnails.begin(),
                        popup->m_thumbnails.end(),
                        [&currentId](ThumbnailAPI::ThumbnailInfo const& t) { return t.id == currentId; }
                    );
                    if (it != popup->m_thumbnails.end()) {
                        newIndex = static_cast<int>(std::distance(popup->m_thumbnails.begin(), it));
                    }
                }
                popup->m_currentIndex = newIndex;
                popup->loadThumbnailAt(popup->m_currentIndex);
                popup->setupRating();
            });
        });
    }

    if (m_bgSprite) {
        m_bgSprite->setVisible(false);
    }

    auto content = this->m_mainLayer->getContentSize();

    float maxWidth = content.width - 40.f;
    float maxHeight = content.height - 80.f;

    if (m_closeBtn) {
         float topY = (content.height / 2 + 5.f) + (maxHeight / 2);
         float leftX = (content.width - maxWidth) / 2;
         m_closeBtn->setPosition({leftX - 3.f, topY + 3.f});

         if (auto infoSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
             infoSpr->setScale(0.9f);
             auto infoBtn = CCMenuItemSpriteExtra::create(infoSpr, this, menu_selector(LocalThumbnailViewPopup::onInfo));

             infoBtn->setPosition({380.000f, 246.000f});

             if (auto menu = m_closeBtn->getParent()) {
                 menu->addChild(infoBtn);
             }
         }
    }

    log::info("[ThumbnailViewPopup] Content size: {}x{}, Max area: {}x{}",
        content.width, content.height, maxWidth, maxHeight);

    auto stencil = PaimonDrawNode::create();
    CCPoint rect[4] = { ccp(0,0), ccp(maxWidth,0), ccp(maxWidth,maxHeight), ccp(0,maxHeight) };
    ccColor4F white = {1,1,1,1};
    stencil->drawPolygon(rect, 4, white, 0, white);

    auto clip = CCClippingNode::create(stencil);
    m_clippingNode = clip;

    m_clippingNode->setContentSize({maxWidth, maxHeight});
    m_clippingNode->setAnchorPoint({0.5f, 0.5f});
    m_clippingNode->setPosition({content.width / 2, content.height / 2 + 5.f});
    this->m_mainLayer->addChild(m_clippingNode, 1);

    auto clippingBg = CCLayerColor::create({0, 0, 0, 255});
    clippingBg->setOpacity(255);
    clippingBg->setContentSize({maxWidth, maxHeight});
    clippingBg->ignoreAnchorPointForPosition(false);
    clippingBg->setAnchorPoint({0.5f, 0.5f});
    clippingBg->setPosition({maxWidth / 2, maxHeight / 2});

    if (m_clippingNode) {
        m_clippingNode->addChild(clippingBg, -1);
    }

    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({maxWidth + 4.f, maxHeight + 4.f});
        border->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(border, 2);
    }

    // flechas galeria
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    this->m_mainLayer->addChild(menu, 10);

    if (auto prevSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.75f);
        m_leftArrow = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LocalThumbnailViewPopup::onPrev));
        m_leftArrow->setPosition({15.f, content.height / 2 + 5.f});
        m_leftArrow->setVisible(false);
        menu->addChild(m_leftArrow);
    }

    if (auto nextSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setFlipX(true);
        nextSpr->setScale(0.75f);
        m_rightArrow = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LocalThumbnailViewPopup::onNext));
        m_rightArrow->setPosition({content.width - 15.f, content.height / 2 + 5.f});
        m_rightArrow->setVisible(false);
        menu->addChild(m_rightArrow);
    }

    m_counterLabel = CCLabelBMFont::create("1/1", "bigFont.fnt");
    m_counterLabel->setScale(0.4f);
    m_counterLabel->setPosition({content.width / 2.f, 23.f});
    m_counterLabel->setVisible(false);
    this->m_mainLayer->addChild(m_counterLabel, 11);

    this->setTouchEnabled(true);

#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
    this->setKeypadEnabled(true);
#endif

    log::info("[ThumbnailViewPopup] === INICIANDO CARGA DE THUMBNAIL ===");
    log::info("[ThumbnailViewPopup] Level ID: {}", m_levelID);
    log::info("[ThumbnailViewPopup] Verification Category: {}", verificationCategory);
    if (verificationCategory >= 0) {
        this->loadFromVerificationQueue(static_cast<PendingCategory>(verificationCategory), maxWidth, maxHeight, content, openedFromReport);
    } else {
        this->tryLoadFromMultipleSources(maxWidth, maxHeight, content, openedFromReport);

        WeakRef<LocalThumbnailViewPopup> self = this;
        ThumbnailAPI::get().getThumbnails(m_levelID, [self](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
            auto popup = self.lock();
            // Nota: no usar isUiAlive() aqui porque getThumbnails puede resolver
            // sincronamente desde cache durante setup(), cuando getParent() aun es null.
            if (!popup || popup->m_isExiting || !popup->m_mainLayer) return;

            if (!success || thumbs.empty()) return;

            popup->m_thumbnails = thumbs;
            popup->m_currentIndex = 0;
            if (popup->m_leftArrow) popup->m_leftArrow->setVisible(popup->m_thumbnails.size() > 1);
            if (popup->m_rightArrow) popup->m_rightArrow->setVisible(popup->m_thumbnails.size() > 1);
            if (popup->m_counterLabel) {
                popup->m_counterLabel->setVisible(popup->m_thumbnails.size() > 1);
                popup->m_counterLabel->setString(fmt::format("{}/{}", popup->m_currentIndex + 1, popup->m_thumbnails.size()).c_str());
            }
            popup->loadThumbnailAt(popup->m_currentIndex);
            popup->setupRating();
        });
    }

    setupRating();
}


// Carga desde multiples fuentes

void LocalThumbnailViewPopup::loadFromVerificationQueue(PendingCategory category, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] Cargando desde cola de verificacion - Categoria: {}", static_cast<int>(category));

    Ref<LocalThumbnailViewPopup> safeRef = this;

    if (category == PendingCategory::Verify) {
        ThumbnailAPI::get().downloadSuggestion(m_levelID, [safeRef, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!safeRef->getParent() || !safeRef->m_mainLayer) {
                log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar suggestion");
                return;
            }

            if (success && tex) {
                log::info("[ThumbnailViewPopup] ✓ Suggestion cargada");
                safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
            } else {
                log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar suggestion");
                safeRef->showNoThumbnail(content);
            }
        });
    } else if (category == PendingCategory::Update) {
        ThumbnailAPI::get().downloadUpdate(m_levelID, [safeRef, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!safeRef->getParent() || !safeRef->m_mainLayer) {
                log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar update");
                return;
            }

            if (success && tex) {
                log::info("[ThumbnailViewPopup] ✓ Update cargada");
                safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
            } else {
                log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar update");
                safeRef->showNoThumbnail(content);
            }
        });
    } else if (category == PendingCategory::Report) {
        ThumbnailAPI::get().downloadReported(m_levelID, [safeRef, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!safeRef->getParent() || !safeRef->m_mainLayer) {
                log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar reported");
                return;
            }

            if (success && tex) {
                log::info("[ThumbnailViewPopup] ✓ Reported cargada");
                safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
            } else {
                log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar reported");
                safeRef->showNoThumbnail(content);
            }
        });
    } else {
        log::error("[ThumbnailViewPopup] Categoria de verificacion desconocida: {}", static_cast<int>(category));
        this->showNoThumbnail(content);
    }
}

void LocalThumbnailViewPopup::tryLoadFromMultipleSources(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    // Cargar todos los thumbnails locales para galeria
    m_localThumbPaths = LocalThumbs::get().getAllThumbPaths(m_levelID);
    int localCount = static_cast<int>(m_localThumbPaths.size());

    if (localCount > 0) {
        log::info("[ThumbnailViewPopup] {} thumbnails locales encontrados para nivel {}", localCount, m_levelID);
        m_viewingLocal = true;
        m_localCurrentIndex = localCount - 1; // mostrar el mas reciente

        auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
        if (tex) {
            log::info("[ThumbnailViewPopup] Textura cargada desde LocalThumbs indice {}", m_localCurrentIndex);
            this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);

            // mostrar UI de galeria si hay multiples
            if (localCount > 1) {
                if (m_leftArrow) m_leftArrow->setVisible(true);
                if (m_rightArrow) m_rightArrow->setVisible(true);
                if (m_counterLabel) {
                    m_counterLabel->setVisible(true);
                    m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, localCount).c_str());
                }
            }
            return;
        }
        log::warn("[ThumbnailViewPopup] LocalThumbs fallo al cargar textura indice {}", m_localCurrentIndex);
    }

    // fallback: buscar cualquier thumbnail local (png/jpg only — skip MP4,
    // videos are handled by the gallery API via loadThumbnailAt to avoid
    // showing a buggy flash that gets overwritten by the API callback)
    auto localPath = LocalThumbs::get().findAnyThumbnail(m_levelID);
    if (localPath) {
        auto lowerPath = geode::utils::string::toLower(*localPath);
        if (lowerPath.ends_with(".mp4")) {
            log::info("[ThumbnailViewPopup] LocalThumbs es MP4, skipping — gallery API will handle it");
        } else {
            log::info("[ThumbnailViewPopup] Fuente 1: LocalThumbs ENCONTRADO (fallback)");
            auto tex = LocalThumbs::get().loadTexture(m_levelID);
            if (tex) {
                log::info("[ThumbnailViewPopup] Textura cargada desde LocalThumbs (fallback)");
                this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                return;
            }
            log::warn("[ThumbnailViewPopup] LocalThumbs fallo al cargar textura (fallback)");
        }
    } else {
        log::info("[ThumbnailViewPopup] Fuente 1: LocalThumbs - NO disponible");
    }

    m_viewingLocal = false;

    // Fuente 1.5: URL-based RAM cache — LevelCell stores thumbnails here via requestUrlLoad.
    // The level-based RAM cache (tryLoadFromCache) may miss them since LevelCell uses URL keys.
    {
        auto& cache = paimon::cache::ThumbnailCache::get();
        std::string mainUrl = ThumbnailAPI::get().getThumbnailURL(m_levelID);
        auto urlTex = cache.getUrlFromRam(mainUrl);
        if (urlTex.has_value() && urlTex.value()) {
            log::info("[ThumbnailViewPopup] URL-based RAM cache hit for levelID={}", m_levelID);
            this->displayThumbnail(urlTex.value(), maxWidth, maxHeight, content, openedFromReport);
            return;
        }
    }

    // Fuente 2: ThumbnailLoader (revisa RAM cache, luego disco, luego descarga)
    loadFromThumbnailLoader(maxWidth, maxHeight, content, openedFromReport);
}

bool LocalThumbnailViewPopup::tryLoadFromCache(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    // Buscar en el cache de disco real (cache/{id}.png o .gif) via ThumbnailLoader
    // ThumbnailLoader ya maneja esta logica internamente, asi que delegamos a el
    // para evitar rutas desincronizadas. Verificamos solo RAM hit rapido.
    auto ramTex = paimon::cache::ThumbnailCache::get().getFromRam(m_levelID, false);
    if (!ramTex.has_value()) {
        ramTex = paimon::cache::ThumbnailCache::get().getFromRam(m_levelID, true);
    }
    if (ramTex.has_value() && ramTex.value()) {
        log::info("[ThumbnailViewPopup] ✓ RAM cache hit directo para nivel {}", m_levelID);
        this->displayThumbnail(ramTex.value(), maxWidth, maxHeight, content, openedFromReport);
        return true;
    }
    return false;
}

void LocalThumbnailViewPopup::loadFromThumbnailLoader(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] Intentando Fuente 3: ThumbnailLoader + Descarga");
    std::string fileName = fmt::format("{}.png", m_levelID);

    Ref<LocalThumbnailViewPopup> safeRef = this;

    ThumbnailLoader::get().requestLoad(m_levelID, fileName, [safeRef, maxWidth, maxHeight, content, openedFromReport](CCTexture2D* tex, bool) {
        log::info("[ThumbnailViewPopup] === CALLBACK THUMBNAILLOADER ===");

        if (!safeRef->isUiAlive()) {
            log::warn("[ThumbnailViewPopup] Popup ya no tiene parent o mainLayer valido");
            return;
        }

        if (tex) {
            log::info("[ThumbnailViewPopup] ✓ Textura recibida ({}x{})",
                tex->getPixelsWide(), tex->getPixelsHigh());
            safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
        } else {
            log::warn("[ThumbnailViewPopup] ✗ ThumbnailLoader fallo, intentando descarga directa del servidor");
            safeRef->tryDirectServerDownload(maxWidth, maxHeight, content, openedFromReport);
        }
    }, 10, false, ThumbnailLoader::Quality::High);
}

void LocalThumbnailViewPopup::tryDirectServerDownload(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] Intentando Fuente 3: Descarga directa del servidor");

    Ref<LocalThumbnailViewPopup> safeRef = this;

    HttpClient::DownloadCallback cb = [safeRef, maxWidth, maxHeight, content, openedFromReport](bool success, std::vector<uint8_t> const& data, int w, int h) {
        if (!safeRef->getParent() || !safeRef->m_mainLayer) {
            log::warn("[ThumbnailViewPopup] Popup ya no tiene parent valido (descarga servidor)");
            return;
        }

        if (success && !data.empty()) {
            log::info("[ThumbnailViewPopup] ✓ Datos descargados del servidor ({} bytes)", data.size());

            // Detectar si los datos son MP4 (ftyp box at offset 4)
            bool isMp4 = data.size() >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p';
            if (isMp4) {
                log::info("[ThumbnailViewPopup] ✓ Datos detectados como MP4, usando VideoThumbnailSprite");
                std::string cacheKey = fmt::format("direct_video_{}", safeRef->m_levelID);
                auto* videoSprite = VideoThumbnailSprite::createFromData(
                    std::vector<uint8_t>(data.begin(), data.end()), cacheKey);
                if (videoSprite) {
                    // displayVideoThumbnail handles play/pause state and shows play button
                    safeRef->displayVideoThumbnail(videoSprite, maxWidth, maxHeight, content);
                    return;
                }
                log::warn("[ThumbnailViewPopup] ✗ VideoThumbnailSprite fallo para MP4");
            } else {
                auto image = new CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                    auto tex = new CCTexture2D();
                    if (tex->initWithImage(image)) {
                        log::info("[ThumbnailViewPopup] ✓ Textura creada desde servidor ({}x{})",
                            tex->getPixelsWide(), tex->getPixelsHigh());
                        safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                        tex->release();
                        image->release();
                        return;
                    }
                    tex->release();
                }
                image->release();
                log::error("[ThumbnailViewPopup] ✗ Error creando textura desde datos del servidor");
            }
        } else {
            log::warn("[ThumbnailViewPopup] ✗ Descarga del servidor fallo");
        }

        log::info("[ThumbnailViewPopup] === TODAS LAS FUENTES FALLARON ===");
        safeRef->showNoThumbnail(content);
    };
    HttpClient::get().downloadThumbnail(m_levelID, std::move(cb));
}


// Display / UI

void LocalThumbnailViewPopup::displayVideoThumbnail(VideoThumbnailSprite* videoSprite, float maxWidth, float maxHeight, CCSize content) {
    if (!m_mainLayer || !videoSprite) {
        return;
    }

    clearGalleryDisplay();

    // Keep video invisible until first frame is decoded — the 1x1 placeholder
    // texture stretched to video dimensions causes glitchy rendering artifacts
    m_videoPlaying = false;
    videoSprite->setVisible(false);
    videoSprite->setOpacity(0);
    videoSprite->setAnchorPoint({0.5f, 0.5f});
    videoSprite->setID("thumbnail"_spr);
    videoSprite->setLoop(true);
    videoSprite->setVolume(0.0f);

    m_viewWidth = maxWidth;
    m_viewHeight = maxHeight;

    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 5.f;

    if (m_clippingNode) {
        m_clippingNode->addChild(videoSprite, 10);
        videoSprite->setPosition({maxWidth / 2.f, maxHeight / 2.f});
    } else {
        m_mainLayer->addChild(videoSprite, 10);
        videoSprite->setPosition({centerX, centerY});
    }

    m_thumbnailSprite = videoSprite;
    m_thumbnailTexture = nullptr;
    resetZoomGestureState();

    // Play muted to start decoding — first frame callback will pause and apply scale
    videoSprite->play();

    // --- First frame callback: calculate scale from actual contentSize, make visible, pause ---
    Ref<LocalThumbnailViewPopup> safeRef = this;
    videoSprite->setOnFirstVisibleFrame([safeRef, maxWidth, maxHeight, centerX, centerY](VideoThumbnailSprite* readySprite) {
        if (!safeRef->isUiAlive()) {
            return;
        }
        if (safeRef->m_thumbnailSprite != readySprite || !readySprite->getParent()) {
            return;
        }

        safeRef->m_thumbnailTexture = readySprite->getTexture();

        // Calculate scale the same way displayThumbnail does — using getContentWidth/Height
        // (in points), not raw pixel dimensions. This matches how CCSprite renders.
        float cw = readySprite->getContentWidth();
        float ch = readySprite->getContentHeight();
        if (cw < 1.f) cw = 1.f;
        if (ch < 1.f) ch = 1.f;

        float scaleX = maxWidth / cw;
        float scaleY = maxHeight / ch;
        float scale = std::max(scaleX, scaleY);

        readySprite->setScale(scale);
        safeRef->m_initialScale = scale;
        safeRef->m_minScale = scale;
        safeRef->m_maxScale = std::max(4.0f, scale * 6.0f);
        safeRef->m_savedScale = scale;

        readySprite->setVisible(true);
        readySprite->setOpacity(255);

        // First frame decoded — pause video so play button overlay works
        if (!safeRef->m_videoPlaying) {
            readySprite->pause();
        }

        // Position play button at center of video area
        if (safeRef->m_playBtn) {
            if (safeRef->m_clippingNode) {
                safeRef->m_playBtn->setPosition({maxWidth / 2.f, maxHeight / 2.f});
            } else {
                safeRef->m_playBtn->setPosition({centerX, centerY});
            }
        }
    });

    // --- Play button overlay ---
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }

    auto playBtnMenu = CCMenu::create();
    playBtnMenu->setPosition({0, 0});
    playBtnMenu->setID("video-play-menu"_spr);

    CCNode* btnSprite = nullptr;
    CCSprite* playIcon = nullptr;
    if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn_001.png")) {
        playIcon = spr;
    } else if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png")) {
        playIcon = spr;
    }

    if (playIcon) {
        playIcon->setScale(1.2f);
        playIcon->setOpacity(200);
        btnSprite = playIcon;
    } else {
        auto btnSpr = ButtonSprite::create("▶", 32, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.7f);
        btnSpr->setScale(0.8f);
        btnSpr->setOpacity(200);
        btnSprite = btnSpr;
    }

    if (btnSprite) {
        m_playBtn = CCMenuItemSpriteExtra::create(btnSprite, this, menu_selector(LocalThumbnailViewPopup::onPlayVideo));
        m_playBtn->setID("video-play-btn"_spr);
        m_playBtn->setPosition({centerX, centerY});
        playBtnMenu->addChild(m_playBtn);
    }

    this->m_mainLayer->addChild(playBtnMenu, 20);
    m_playBtnMenu = playBtnMenu;
    updatePlayButton();
}

void LocalThumbnailViewPopup::displayThumbnail(CCTexture2D* tex, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    log::info("[ThumbnailViewPopup] === MOSTRANDO THUMBNAIL ===");
    log::info("[ThumbnailViewPopup] Textura: {}x{}", tex->getPixelsWide(), tex->getPixelsHigh());

    if (!m_mainLayer) {
        log::error("[ThumbnailViewPopup] Popup destruido antes de displayThumbnail!");
        return;
    }

    // Save old sprite for crossfade (don't nuke clipping node)
    CCNode* oldSprite = m_thumbnailSprite;
    m_thumbnailSprite = nullptr;
    m_thumbnailTexture = nullptr;

    // Remove play button when switching to static thumbnail
    m_videoPlaying = false;
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }

    // Remove "no thumbnail" container if present
    if (m_mainLayer) {
        if (auto node = m_mainLayer->getChildByID("nothumb-container"_spr)) {
            node->removeFromParent();
        }
    }

    if (!m_mainLayer) {
        log::error("[ThumbnailViewPopup] m_mainLayer es null!");
        return;
    }

    if (m_buttonMenu) {
        m_buttonMenu->removeFromParent();
        m_buttonMenu = nullptr;
    }
    if (m_settingsMenu) {
        m_settingsMenu->removeFromParent();
        m_settingsMenu = nullptr;
    }
    m_refreshBtn = nullptr;
    if (!m_suggestions.empty()) {
        if (m_leftArrow) {
            m_leftArrow->removeFromParent();
            m_leftArrow = nullptr;
        }
        if (m_rightArrow) {
            m_rightArrow->removeFromParent();
            m_rightArrow = nullptr;
        }
        if (m_counterLabel) {
            m_counterLabel->removeFromParent();
            m_counterLabel = nullptr;
        }
    }

    m_thumbnailTexture = tex;

    CCSprite* sprite = nullptr;

    sprite = CCSprite::createWithTexture(tex);

    bool canUseLevelGifCache =
        m_thumbnails.empty() &&
        !m_viewingLocal &&
        ThumbnailLoader::get().hasGIFData(m_levelID);

    if (canUseLevelGifCache) {
         auto path = ThumbnailLoader::get().getCachePath(m_levelID, true);
         Ref<LocalThumbnailViewPopup> safeRef = this;
         AnimatedGIFSprite::createAsync(geode::utils::string::pathToString(path), [safeRef, maxWidth, maxHeight](AnimatedGIFSprite* anim) {
             if (!safeRef->isUiAlive()) return;
             if (anim && safeRef->m_thumbnailSprite) {
                 auto oldSprite = safeRef->m_thumbnailSprite;
                 auto parent = oldSprite->getParent();
                 if (parent) {
                     CCPoint pos = oldSprite->getPosition();
                     oldSprite->removeFromParent();

                     anim->setAnchorPoint({0.5f, 0.5f});
                     float scaleX = maxWidth / anim->getContentWidth();
                     float scaleY = maxHeight / anim->getContentHeight();
                     float scale = std::max(scaleX, scaleY);
                     anim->setScale(scale);
                     anim->setPosition(pos);

                     parent->addChild(anim, 10);
                     safeRef->m_thumbnailSprite = anim;
                 }
             }
         });
    }

    if (!sprite) {
        log::error("[ThumbnailViewPopup] No se pudo crear sprite con textura");
        return;
    }

    log::info("[ThumbnailViewPopup] Sprite creado correctamente");
    sprite->setAnchorPoint({0.5f, 0.5f});

    m_viewWidth = maxWidth;
    m_viewHeight = maxHeight;

    float scaleX = maxWidth / sprite->getContentWidth();
    float scaleY = maxHeight / sprite->getContentHeight();
    float scale = std::max(scaleX, scaleY);

    sprite->setScale(scale);
    m_initialScale = scale;
    m_minScale = scale;
    m_maxScale = std::max(4.0f, scale * 6.0f);
    m_savedScale = scale;

    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 5.f;
    sprite->setPosition({centerX, centerY});
    sprite->setID("thumbnail"_spr);

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    tex->setTexParameters(&params);

    if (m_clippingNode) {
        m_clippingNode->addChild(sprite, 10);
        sprite->setPosition({maxWidth / 2, maxHeight / 2});
    } else {
        this->m_mainLayer->addChild(sprite, 10);
        sprite->setPosition({centerX, centerY});
    }
    m_thumbnailSprite = sprite;
    resetZoomGestureState();

    sprite->setVisible(true);

    // Apply the configured gallery transition (directional-elastic, crossfade, etc.)
    applyPopupTransition(sprite, oldSprite, maxWidth);

    log::info("[ThumbnailViewPopup] ✓ Thumbnail agregado a mainLayer");
    log::info("[ThumbnailViewPopup] Posicion: ({},{}), Scale: {}, Tamano final: {}x{}",
        centerX, centerY, scale, sprite->getContentWidth() * scale, sprite->getContentHeight() * scale);
    log::info("[ThumbnailViewPopup] Parent: {}, Visible: {}, Opacity: {}, Z-Order: {}",
        (void*)sprite->getParent(), sprite->isVisible(), sprite->getOpacity(), sprite->getZOrder());

    // flechas + contador
    if (!m_suggestions.empty()) {
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        this->m_mainLayer->addChild(menu, 20);

        if (auto leftSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
            leftSpr->setScale(0.75f);
            m_leftArrow = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(LocalThumbnailViewPopup::onPrevSuggestion));
            m_leftArrow->setPosition({centerX - maxWidth/2 - 30.f, centerY});
            menu->addChild(m_leftArrow);
        }

        if (auto rightSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
            rightSpr->setFlipX(true);
            rightSpr->setScale(0.75f);
            m_rightArrow = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(LocalThumbnailViewPopup::onNextSuggestion));
            m_rightArrow->setPosition({centerX + maxWidth/2 + 30.f, centerY});
            menu->addChild(m_rightArrow);
        }

        m_counterLabel = CCLabelBMFont::create(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str(), "bigFont.fnt");
        m_counterLabel->setScale(0.5f);
        m_counterLabel->setPosition({centerX, centerY - maxHeight/2 - 15.f});
        this->m_mainLayer->addChild(m_counterLabel, 20);

        if (m_leftArrow) m_leftArrow->setVisible(m_suggestions.size() > 1);
        if (m_rightArrow) m_rightArrow->setVisible(m_suggestions.size() > 1);
    }

    // menu botones
    m_buttonMenu = CCMenu::create();
    auto buttonMenu = m_buttonMenu;

    auto downloadSprite = Assets::loadButtonSprite(
        "popup-download",
        "frame:GJ_downloadBtn_001.png",
        [](){
            if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_downloadBtn_001.png")) return spr;
            if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png")) return spr;
            return paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
        }
    );
    downloadSprite->setScale(0.7f);
    auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSprite, this, menu_selector(LocalThumbnailViewPopup::onDownloadBtn));

    CCMenuItemSpriteExtra* centerBtn = nullptr;

    if (m_verificationCategory >= 0 && m_verificationCategory != 2) {
        auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
        acceptSpr->setScale(0.6f);
        centerBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
    } else if (openedFromReport) {
        auto delSpr = ButtonSprite::create(Localization::get().getString("level.delete_button").c_str(), 90, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.5f);
        delSpr->setScale(0.6f);
        centerBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(LocalThumbnailViewPopup::onDeleteReportedThumb));
    } else {
        auto reportSpr = ButtonSprite::create(Localization::get().getString("level.report_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.5f);
        reportSpr->setScale(0.6f);
        centerBtn = CCMenuItemSpriteExtra::create(reportSpr, this, menu_selector(LocalThumbnailViewPopup::onReportBtn));
    }

    CCMenuItemSpriteExtra* acceptBtn = nullptr;
    if (m_canAcceptUpload && m_verificationCategory < 0) {
        auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
        acceptSpr->setScale(0.6f);
        acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
    }

    if (acceptBtn) buttonMenu->addChild(acceptBtn);
    if (centerBtn) buttonMenu->addChild(centerBtn);

    if (auto rateSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starBtn_001.png")) {
        rateSpr->setScale(0.7f);
        auto rateBtn = CCMenuItemSpriteExtra::create(rateSpr, this, menu_selector(LocalThumbnailViewPopup::onRate));
        buttonMenu->addChild(rateBtn);
    }

    buttonMenu->addChild(downloadBtn);

    // YouTube button
    if (auto ytSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_ytIcon_001.png")) {
        ytSpr->setScale(0.7f);
        auto ytBtn = CCMenuItemSpriteExtra::create(ytSpr, this, menu_selector(LocalThumbnailViewPopup::onYouTubeBtn));
        ytBtn->setID("youtube-btn"_spr);
        buttonMenu->addChild(ytBtn);
    }

    // btn eliminar (mods)
    auto gm = GameManager::get();
    if (gm) {
        auto username = gm->m_playerName;
        int accountID = 0;
        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;

        WeakRef<LocalThumbnailViewPopup> self = this;
        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self](bool isMod, bool isAdmin) {
            auto popup = self.lock();
            if (!popup) return;

            if (isMod || isAdmin) {
                auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteServerBtn_001.png");
                if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteBtn_001.png");
                if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_trashBtn_001.png");

                if (spr) {
                    spr->setScale(0.6f);
                    auto btn = CCMenuItemSpriteExtra::create(
                        spr,
                        popup,
                        menu_selector(LocalThumbnailViewPopup::onDeleteThumbnail)
                    );

                    if (popup->m_buttonMenu) {
                        popup->m_buttonMenu->addChild(btn);
                        popup->m_buttonMenu->updateLayout();
                    }
                }
            }
        });
    }

    buttonMenu->ignoreAnchorPointForPosition(false);
    buttonMenu->setAnchorPoint({0.5f, 0.5f});
    buttonMenu->setContentSize({content.width - 40.f, 60.f});
    buttonMenu->setPosition({content.width / 2, 46.f});

    auto layout = RowLayout::create();
    layout->setGap(15.f);
    layout->setAxisAlignment(AxisAlignment::Center);
    layout->setCrossAxisAlignment(AxisAlignment::Center);

    buttonMenu->setLayout(layout);
    buttonMenu->updateLayout();

    this->m_mainLayer->addChild(buttonMenu, 10);

    // btn engranaje settings
    m_settingsMenu = CCMenu::create();
    auto settingsMenu = m_settingsMenu;
    settingsMenu->setPosition({0, 0});
    this->m_mainLayer->addChild(settingsMenu, 15);

    auto gearSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
    if (!gearSpr) gearSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
    if (gearSpr) {
        gearSpr->setScale(0.45f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(LocalThumbnailViewPopup::onSettings));
        gearBtn->setPosition({content.width - 22.f, 46.f});
        gearBtn->setID("settings-gear-btn"_spr);
        settingsMenu->addChild(gearBtn);
    }

    // btn actualizar miniatura
    if (m_verificationCategory < 0) {
        auto refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
        if (!refreshSpr) refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png");
        if (refreshSpr) {
            refreshSpr->setScale(0.45f);
            m_refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, this, menu_selector(LocalThumbnailViewPopup::onRefreshBtn));
            m_refreshBtn->setPosition({22.f, 46.f});
            m_refreshBtn->setID("refresh-btn"_spr);
            settingsMenu->addChild(m_refreshBtn);
            updateRefreshButtonState();
        }
    }

    // Ensure gallery arrows remain visible when there are multiple thumbnails
    if (m_suggestions.empty() && m_thumbnails.size() > 1) {
        if (m_leftArrow) m_leftArrow->setVisible(true);
        if (m_rightArrow) m_rightArrow->setVisible(true);
        if (m_counterLabel) {
            m_counterLabel->setVisible(true);
            m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_thumbnails.size()).c_str());
        }
    }
}

void LocalThumbnailViewPopup::clearGalleryDisplay() {
    if (!m_mainLayer) return;

    if (m_thumbnailSprite) {
        // Stop video playback before removing
        if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite)) {
            videoSprite->stop();
        }
        m_thumbnailSprite->removeFromParent();
        m_thumbnailSprite = nullptr;
    }
    m_thumbnailTexture = nullptr;

    // keep m_clippingNode alive — only clear children (sprites) inside it
    if (m_clippingNode) {
        m_clippingNode->removeAllChildren();
        // re-add the semi-transparent background inside the clip
        auto content = m_clippingNode->getContentSize();
        auto clippingBg = CCLayerColor::create({0, 0, 0, 255});
        clippingBg->setOpacity(25);
        clippingBg->setContentSize(content);
        clippingBg->ignoreAnchorPointForPosition(false);
        clippingBg->setAnchorPoint({0.5f, 0.5f});
        clippingBg->setPosition({content.width / 2, content.height / 2});
        m_clippingNode->addChild(clippingBg, -1);
    }

    if (auto node = m_mainLayer->getChildByID("nothumb-container"_spr)) {
        node->removeFromParent();
    }

    // Reset video playback state and remove play button
    m_videoPlaying = false;
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }
}

void LocalThumbnailViewPopup::applyPopupTransition(CCNode* newNode, CCNode* oldNode, float maxWidth) {
    if (!newNode) return;

    // These are always CCSprite* in practice — cast for opacity access
    auto* newSpr = static_cast<CCSprite*>(newNode);

    // First load (no old sprite): just show immediately, no fancy transition
    if (!oldNode || !oldNode->getParent()) {
        newSpr->setOpacity(255);
        if (oldNode) oldNode->removeFromParent();
        m_navDirection = NavDirection::None;
        return;
    }

    std::string style = Mod::get()->getSettingValue<std::string>("popup-gallery-transition");
    float dur = Mod::get()->getSettingValue<float>("popup-gallery-transition-duration");

    CCPoint targetPos = newNode->getPosition();
    float sx = newNode->getScaleX();
    float sy = newNode->getScaleY();

    // Directional: determine slide direction based on navigation
    bool goRight = (m_navDirection == NavDirection::Right);
    bool goLeft = (m_navDirection == NavDirection::Left);
    // For non-directional transitions, default to right
    bool slideFromRight = goRight || (!goLeft);

    if (style == "directional-elastic") {
        // Directional elastic: slow start, then fast — like an elastic snap
        // in the direction of navigation.  The new image starts offscreen
        // on the side we're navigating toward, and snaps into place.
        {
            // Old: slide out opposite direction with back-ease + fade
            float oldTargetX = goLeft ? (targetPos.x + maxWidth) : (targetPos.x - maxWidth);
            oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseBackIn::create(CCMoveTo::create(dur * 0.6f, {oldTargetX, targetPos.y})),
                    CCFadeTo::create(dur * 0.5f, 0),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
                nullptr));
        }
        // New: start offscreen on the incoming side, elastic snap to center
        float startX = goLeft ? (targetPos.x - maxWidth) : (targetPos.x + maxWidth);
        newNode->setPosition({startX, targetPos.y});
        newSpr->setOpacity(255);
        newNode->runAction(CCEaseElasticOut::create(
            CCMoveTo::create(dur * 1.1f, targetPos), 0.35f));

    } else if (style == "elastic-slide") {
        // Always from right with elastic ease
        oldNode->runAction(CCSequence::create(
            CCSpawn::create(
                CCEaseBackIn::create(CCMoveTo::create(dur * 0.7f, {targetPos.x - maxWidth, targetPos.y})),
                CCFadeTo::create(dur * 0.6f, 0),
                nullptr),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));
        newNode->setPosition({targetPos.x + maxWidth, targetPos.y});
        newSpr->setOpacity(255);
        newNode->runAction(CCEaseElasticOut::create(
            CCMoveTo::create(dur * 1.2f, targetPos), 0.3f));

    } else if (style == "slide-left") {
        oldNode->runAction(CCSequence::create(
            CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - maxWidth, targetPos.y}), 2.0f),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));
        newNode->setPosition({targetPos.x + maxWidth, targetPos.y});
        newSpr->setOpacity(255);
        newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));

    } else if (style == "slide-right") {
        oldNode->runAction(CCSequence::create(
            CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x + maxWidth, targetPos.y}), 2.0f),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));
        newNode->setPosition({targetPos.x - maxWidth, targetPos.y});
        newSpr->setOpacity(255);
        newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));

    } else if (style == "zoom-in") {
        newNode->setScaleX(sx * 1.3f);
        newNode->setScaleY(sy * 1.3f);
        newSpr->setOpacity(0);
        newNode->runAction(CCSpawn::create(
            CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f),
            CCFadeTo::create(dur * 0.6f, 255),
            nullptr));
        oldNode->runAction(CCSequence::create(
            CCFadeTo::create(dur * 0.5f, 0),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));

    } else if (style == "zoom-out") {
        newNode->setScaleX(sx * 0.01f);
        newNode->setScaleY(sy * 0.01f);
        newSpr->setOpacity(0);
        newNode->runAction(CCSpawn::create(
            CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f),
            CCFadeTo::create(dur * 0.6f, 255),
            nullptr));
        oldNode->runAction(CCSequence::create(
            CCFadeTo::create(dur * 0.5f, 0),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));

    } else if (style == "bounce") {
        float startX = slideFromRight ? (targetPos.x + maxWidth) : (targetPos.x - maxWidth);
        newNode->setPosition({startX, targetPos.y});
        newSpr->setOpacity(255);
        newNode->runAction(CCEaseBounceOut::create(
            CCMoveTo::create(dur * 1.1f, targetPos)));
        {
            float exitX = slideFromRight ? (targetPos.x - maxWidth) : (targetPos.x + maxWidth);
            oldNode->runAction(CCSequence::create(
                CCEaseIn::create(CCMoveTo::create(dur * 0.6f, {exitX, targetPos.y}), 2.0f),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
                nullptr));
        }

    } else if (style == "flip-horizontal") {
        newNode->setScaleX(0.01f);
        newSpr->setOpacity(0);
        newNode->runAction(CCSequence::create(
            CCEaseOut::create(CCScaleTo::create(dur * 0.5f, sx, sy), 2.5f),
            nullptr));
        newSpr->runAction(CCFadeTo::create(dur * 0.4f, 255));
        oldNode->runAction(CCSequence::create(
            CCEaseIn::create(CCScaleTo::create(dur * 0.4f, 0.01f, sy), 2.0f),
            CCFadeTo::create(dur * 0.3f, 0),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));

    } else if (style == "flip-vertical") {
        newNode->setScaleY(0.01f);
        newSpr->setOpacity(0);
        newNode->runAction(CCSequence::create(
            CCEaseOut::create(CCScaleTo::create(dur * 0.5f, sx, sy), 2.5f),
            nullptr));
        newSpr->runAction(CCFadeTo::create(dur * 0.4f, 255));
        oldNode->runAction(CCSequence::create(
            CCEaseIn::create(CCScaleTo::create(dur * 0.4f, sx, 0.01f), 2.0f),
            CCFadeTo::create(dur * 0.3f, 0),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));

    } else if (style == "dissolve") {
        // Pixelated dissolve: scale down old + scale up new with opacity
        newNode->setScaleX(sx * 0.8f);
        newNode->setScaleY(sy * 0.8f);
        newSpr->setOpacity(0);
        newNode->runAction(CCSpawn::create(
            CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.0f),
            CCFadeTo::create(dur * 0.7f, 255),
            nullptr));
        oldNode->runAction(CCSequence::create(
            CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur * 0.7f, sx * 1.15f, sy * 1.15f), 2.0f),
                CCFadeTo::create(dur * 0.6f, 0),
                nullptr),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));

    } else {
        // Default: crossfade
        newSpr->setOpacity(0);
        newSpr->runAction(CCFadeTo::create(dur, 255));
        oldNode->runAction(CCSequence::create(
            CCFadeTo::create(dur * 0.85f, 0),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));
    }

    // Reset nav direction after transition is kicked off
    m_navDirection = NavDirection::None;
}

void LocalThumbnailViewPopup::showNoThumbnail(CCSize content) {
    clearGalleryDisplay();

    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 10.f;
    float bgWidth = content.width - 60.f;
    float bgHeight = content.height - 80.f;

    auto container = CCNode::create();
    container->setContentSize(content);
    container->setPosition({0, 0});
    container->setID("nothumb-container"_spr);

    auto bg = CCLayerColor::create({0, 0, 0, 200});
    bg->setContentSize({bgWidth, bgHeight});
    bg->setPosition({centerX - bgWidth / 2, centerY - bgHeight / 2});
    container->addChild(bg);

    UIBorderHelper::createBorder(centerX, centerY, bgWidth, bgHeight, container);

    auto sadLabel = CCLabelBMFont::create(":(", "bigFont.fnt");
    sadLabel->setScale(3.0f);
    sadLabel->setOpacity(100);
    sadLabel->setPosition({centerX, centerY + 20.f});
    container->addChild(sadLabel, 2);

    auto noThumbLabel = CCLabelBMFont::create(Localization::get().getString("level.no_thumbnail_text").c_str(), "goldFont.fnt");
    noThumbLabel->setScale(0.6f);
    noThumbLabel->setOpacity(150);
    noThumbLabel->setPosition({centerX, centerY - 20.f});
    container->addChild(noThumbLabel, 2);

    this->m_mainLayer->addChild(container, 1);
}

// Acciones de botones

void LocalThumbnailViewPopup::onPlayVideo(CCObject*) {
    auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite);
    if (!videoSprite) return;

    if (m_videoPlaying) {
        // Pause
        m_videoPlaying = false;
        videoSprite->pause();
    } else {
        // Play with audio
        m_videoPlaying = true;
        videoSprite->setVolume(1.0f);
        videoSprite->play();
    }

    updatePlayButton();
}

void LocalThumbnailViewPopup::updatePlayButton() {
    if (!m_playBtn) return;

    if (m_videoPlaying) {
        // Fade out play button when video starts playing
        m_playBtn->stopAllActions();
        m_playBtn->runAction(CCSequence::create(
            CCFadeTo::create(0.3f, 0),
            CCHide::create(),
            nullptr
        ));
    } else {
        // Fade in play button when video is paused
        m_playBtn->stopAllActions();
        m_playBtn->setVisible(true);
        m_playBtn->setOpacity(0);
        m_playBtn->runAction(CCFadeTo::create(0.3f, 255));
    }
}

void LocalThumbnailViewPopup::onYouTubeBtn(CCObject*) {
    // Query the /api/ytlinks endpoint for a linked YouTube video
    std::string url = fmt::format("https://paimbnails-emote.vercel.app/api/ytlinks?id={}", m_levelID);

    auto req = geode::utils::web::WebRequest();
    req.timeout(std::chrono::seconds(10));
    req.header("Accept", "application/json");

    WeakRef<LocalThumbnailViewPopup> self = this;

    // Use dispatchOwned so the request handle is kept alive by the popup.
    // dispatch() (fire-and-forget) can lose the callback on Geode 5.4+ when
    // the TaskHandle is destroyed before the response arrives.
    WebHelper::dispatchOwned(m_ytRequestHolder, std::move(req), "GET", url, [self, levelID = m_levelID](geode::utils::web::WebResponse res) {
        auto popup = self.lock();
        if (!popup || !popup->getParent()) {
            geode::log::warn("[YTLinks] Popup gone before response for level {}", levelID);
            return;
        }

        if (res.code() == 404) {
            // No YouTube link registered
            PaimonNotify::show("No se registro un link de YouTube para este nivel", geode::NotificationIcon::Info);
            return;
        }

        if (!res.ok()) {
            geode::log::warn("[YTLinks] HTTP {} for level {}", res.code(), levelID);
            PaimonNotify::show("No se registro un link de YouTube para este nivel", geode::NotificationIcon::Info);
            return;
        }

        // Use Geode's built-in json() for more reliable body parsing
        auto jsonRes = res.json();
        if (!jsonRes.isOk()) {
            geode::log::error("[YTLinks] JSON parse error for level {}: {}", levelID, jsonRes.unwrapErr());
            PaimonNotify::show("No se registro un link de YouTube para este nivel", geode::NotificationIcon::Info);
            return;
        }

        auto json = jsonRes.unwrap();
        auto ytUrl = json["url"].asString().unwrapOr("");

        geode::log::info("[YTLinks] Level {} resolved URL: '{}'", levelID, ytUrl);

        if (!ytUrl.empty()) {
            PaimonNotify::show("Abriendo link de YouTube...", geode::NotificationIcon::Info);
            geode::Loader::get()->queueInMainThread([url = std::string(ytUrl)]() {
                geode::utils::web::openLinkInBrowser(url);
            });
        } else {
            PaimonNotify::show("No se registro un link de YouTube para este nivel", geode::NotificationIcon::Info);
        }
    });
}

void LocalThumbnailViewPopup::onRefreshBtn(CCObject*) {
    if (m_isDownloading || m_refreshCooldownActive) return;

    m_isDownloading = true;
    startRefreshCooldown();
    updateRefreshButtonState();

    log::info("[ThumbnailViewPopup] Refresh button pressed for level {}", m_levelID);

    // 1. Invalidar toda la cache del nivel
    ThumbnailLoader::get().invalidateLevel(m_levelID, false);
    ThumbnailLoader::get().invalidateLevel(m_levelID, true);
    ThumbnailTransportClient::get().invalidateGalleryMetadata(m_levelID);

    // 2. Limpiar display actual
    clearGalleryDisplay();
    m_thumbnails.clear();
    m_currentIndex = 0;
    ++m_galleryRequestToken;
    if (m_leftArrow) m_leftArrow->setVisible(false);
    if (m_rightArrow) m_rightArrow->setVisible(false);
    if (m_counterLabel) m_counterLabel->setVisible(false);

    // 3. Mostrar spinner de carga
    auto content = m_mainLayer->getContentSize();
    auto spinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    spinner->show(m_mainLayer, 5);

    // 4. Re-descargar galeria desde el servidor (forceRefresh via invalidacion previa)
    float maxWidth = content.width - 40.f;
    float maxHeight = content.height - 80.f;

    WeakRef<LocalThumbnailViewPopup> self = this;
    int requestToken = m_galleryRequestToken;

    ThumbnailAPI::get().getThumbnails(m_levelID, [self, content, maxWidth, maxHeight, requestToken](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
        auto popup = self.lock();
        if (!popup || popup->m_isExiting || !popup->m_mainLayer) return;
        if (requestToken != popup->m_galleryRequestToken) return;

        // Quitar spinner
        if (auto sp = popup->m_mainLayer->getChildByID("paimon-loading-overlay"_spr)) {
            if (auto overlay = typeinfo_cast<PaimonLoadingOverlay*>(sp)) {
                overlay->dismiss();
            }
        }

        popup->m_isDownloading = false;
        popup->updateRefreshButtonState();

        if (success && !thumbs.empty()) {
            popup->m_thumbnails = thumbs;
            popup->m_currentIndex = 0;
            if (popup->m_leftArrow) popup->m_leftArrow->setVisible(popup->m_thumbnails.size() > 1);
            if (popup->m_rightArrow) popup->m_rightArrow->setVisible(popup->m_thumbnails.size() > 1);
            if (popup->m_counterLabel) {
                popup->m_counterLabel->setVisible(popup->m_thumbnails.size() > 1);
                popup->m_counterLabel->setString(fmt::format("{}/{}", 1, popup->m_thumbnails.size()).c_str());
            }
            popup->loadThumbnailAt(0);
            popup->setupRating();
            PaimonNotify::show("Miniaturas actualizadas", geode::NotificationIcon::Success);
        } else {
            // Galeria vacia: intentar cargar miniatura principal directamente
            popup->loadFromThumbnailLoader(maxWidth, maxHeight, content, false);
            PaimonNotify::show("No se encontraron miniaturas en la galeria", geode::NotificationIcon::Info);
        }
    });
}

void LocalThumbnailViewPopup::onDownloadBtn(CCObject*) {
    if (m_isDownloading) return;
    m_isDownloading = true;

    Ref<LocalThumbnailViewPopup> safeRef = this;
    int levelID = m_levelID;

    auto notifyResult = [safeRef](bool ok, std::filesystem::path const& savePath) {
        safeRef->m_isDownloading = false;
        if (ok) {
            log::info("Image saved successfully to {}", geode::utils::string::pathToString(savePath));
            PaimonNotify::create(Localization::get().getString("level.saved").c_str(), NotificationIcon::Success)->show();
        } else {
            log::error("Failed to save image to {}", geode::utils::string::pathToString(savePath));
            PaimonNotify::create(Localization::get().getString("level.save_error").c_str(), NotificationIcon::Error)->show();
        }
    };

    auto doSave = [safeRef, levelID, notifyResult](std::filesystem::path savePath) {
        log::debug("Save path chosen: {}", geode::utils::string::pathToString(savePath));

        // 1) findAnyThumbnail incluye .rgb, .png, .webp en thumb y cache; 2) fallback getCachePath (.png/.gif)
        std::optional<std::string> pathStr = LocalThumbs::get().findAnyThumbnail(levelID);
        bool fromCache = false;
        if (!pathStr) {
            auto cachePng = ThumbnailLoader::get().getCachePath(levelID, false);
            auto cacheGif = ThumbnailLoader::get().getCachePath(levelID, true);
            std::error_code ec;
            if (std::filesystem::exists(cachePng, ec)) {
                pathStr = geode::utils::string::pathToString(cachePng);
                fromCache = true;
            } else if (std::filesystem::exists(cacheGif, ec)) {
                pathStr = geode::utils::string::pathToString(cacheGif);
                fromCache = true;
            }
        } else {
            std::filesystem::path p(*pathStr);
            fromCache = (p.extension() != ".rgb");
        }

        if (pathStr) {
            std::string srcPath = *pathStr;
            std::filesystem::path srcFs(srcPath);
            bool isRgb = (srcFs.extension() == ".rgb");

            std::thread([safeRef, srcPath, savePath, isRgb, fromCache, notifyResult]() {
                bool ok = false;
                if (isRgb) {
                    std::vector<uint8_t> rgbData;
                    uint32_t width = 0, height = 0;
                    if (ImageConverter::loadRgbFile(srcPath, rgbData, width, height)) {
                        auto rgba = ImageConverter::rgbToRgba(rgbData, width, height);
                        ok = ImageConverter::saveRGBAToPNG(rgba.data(), width, height, savePath);
                    }
                    Loader::get()->queueInMainThread([safeRef, ok, savePath, notifyResult]() {
                        if (!safeRef->getParent()) return;
                        notifyResult(ok, savePath);
                    });
                    return;
                }
                if (fromCache) {
                    std::filesystem::path srcP(srcPath);
                    std::string ext = geode::utils::string::pathToString(srcP.extension());
                    if (ext.empty()) ext = ".png";
                    std::filesystem::path destPath = savePath.parent_path() / (geode::utils::string::pathToString(savePath.stem()) + ext);
                    std::error_code ec;
                    std::filesystem::copy(srcP, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                    ok = !ec;
                    Loader::get()->queueInMainThread([safeRef, ok, destPath, notifyResult]() {
                        if (!safeRef->getParent()) return;
                        notifyResult(ok, destPath);
                    });
                    return;
                }
                Loader::get()->queueInMainThread([safeRef, savePath, notifyResult]() {
                    if (!safeRef->getParent()) return;
                    notifyResult(false, savePath);
                });
            }).detach();
            return;
        }

        // Sin ruta en disco: intentar guardar desde la textura mostrada (fallback)
        if (safeRef->m_thumbnailTexture && safeRef->m_thumbnailTexture->getPixelsWide() > 0 && safeRef->m_thumbnailTexture->getPixelsHigh() > 0) {
            int w = safeRef->m_thumbnailTexture->getPixelsWide();
            int h = safeRef->m_thumbnailTexture->getPixelsHigh();
            ::RenderTexture rt(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            rt.begin();
            auto* spr = CCSprite::createWithTexture(safeRef->m_thumbnailTexture);
            if (spr) {
                spr->setPosition({ static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f });
                spr->visit();
            }
            rt.end();
            auto data = rt.getData();
            if (data) {
                int rowSize = w * 4;
                std::vector<uint8_t> tempRow(rowSize);
                uint8_t* buf = data.get();
                for (int y = 0; y < h / 2; ++y) {
                    uint8_t* topRow = buf + y * rowSize;
                    uint8_t* bottomRow = buf + (h - y - 1) * rowSize;
                    std::memcpy(tempRow.data(), topRow, rowSize);
                    std::memcpy(topRow, bottomRow, rowSize);
                    std::memcpy(bottomRow, tempRow.data(), rowSize);
                }
                size_t dataSize = static_cast<size_t>(w) * h * 4;
                std::shared_ptr<uint8_t> buffer(new uint8_t[dataSize], std::default_delete<uint8_t[]>());
                std::memcpy(buffer.get(), data.get(), dataSize);
                std::thread([safeRef, buffer, w, h, savePath, notifyResult]() {
                    bool ok = ImageConverter::saveRGBAToPNG(buffer.get(), static_cast<uint32_t>(w), static_cast<uint32_t>(h), savePath);
                    Loader::get()->queueInMainThread([safeRef, ok, savePath, notifyResult]() {
                        notifyResult(ok, savePath);
                    });
                }).detach();
                return;
            }
        }

        log::error("Thumbnail path not found and no texture to save");
        PaimonNotify::create(Localization::get().getString("level.no_thumbnail").c_str(), NotificationIcon::Error)->show();
        safeRef->m_isDownloading = false;
    };

    auto defaultImageName = std::string("miniatura") + ".png";
    pt::saveImage(defaultImageName, [safeRef, levelID, doSave](geode::Result<std::optional<std::filesystem::path>> result) {
        if (result) {
            auto pathOpt = std::move(result).unwrap();
            if (pathOpt && !pathOpt->empty()) {
                doSave(std::move(*pathOpt));
                return;
            }
        }
        // Diálogo cancelado, error o no soportado: guardar en carpeta del mod
        auto saveDir = Mod::get()->getSaveDir() / "saved_thumbnails";
        std::error_code ec;
        if (!std::filesystem::exists(saveDir, ec)) {
            std::filesystem::create_directories(saveDir, ec);
        }
        auto savePath = saveDir / fmt::format("thumb_{}.png", levelID);
        PaimonNotify::create(Localization::get().getString("level.saving_mod_folder").c_str(), NotificationIcon::Info)->show();
        doSave(savePath);
    });
}

void LocalThumbnailViewPopup::onDeleteReportedThumb(CCObject*) {
    log::info("[ThumbnailViewPopup] Borrar miniatura reportada para levelID={}", m_levelID);

    int levelID = m_levelID;

    std::string username;
    int accountID = 0;
    auto* gm = GameManager::get();
    auto* am = GJAccountManager::get();
    if (gm) {
        username = gm->m_playerName;
        accountID = am ? am->m_accountID : 0;
    } else {
        log::warn("[ThumbnailViewPopup] GameManager::get() es null");
        username = "Unknown";
    }

    auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    spinner->show(m_mainLayer, 100);
    Ref<PaimonLoadingOverlay> loading = spinner;

    if (accountID <= 0) {
        if (loading) loading->dismiss();
        PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
        return;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, levelID, username, accountID, loading](bool isMod, bool isAdmin) {
        auto popup = self.lock();
        if (!popup) return;
        if (!isMod && !isAdmin) {
            if (loading) loading->dismiss();
            PaimonNotify::create(Localization::get().getString("level.delete_moderator_only").c_str(), NotificationIcon::Error)->show();
            return;
        }

        std::string thumbnailId = "";
        if (popup->m_currentIndex >= 0 && popup->m_currentIndex < static_cast<int>(popup->m_thumbnails.size())) {
            thumbnailId = popup->m_thumbnails[popup->m_currentIndex].id;
        }
        ThumbnailAPI::get().deleteThumbnail(levelID, thumbnailId, username, accountID, [self, levelID, loading](bool success, std::string const& msg) {
            if (loading) loading->dismiss();

            if (success) {
                PendingQueue::get().accept(levelID, PendingCategory::Report);
                PaimonNotify::create(Localization::get().getString("level.deleted_server").c_str(), NotificationIcon::Success)->show();
                log::info("[ThumbnailViewPopup] Miniatura {} eliminada del servidor", levelID);

                // Invalidar cache y refrescar galeria
                ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                ThumbnailLoader::get().invalidateLevel(levelID);

                auto popup = self.lock();
                if (popup && popup->isUiAlive()) {
                    ThumbnailAPI::get().getThumbnails(levelID, [self](bool ok, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                        auto popup = self.lock();
                        if (!popup || !popup->isUiAlive()) return;
                        if (!ok || thumbs.empty()) {
                            popup->m_thumbnails.clear();
                            popup->showNoThumbnail(popup->m_mainLayer->getContentSize());
                            if (popup->m_leftArrow) popup->m_leftArrow->setVisible(false);
                            if (popup->m_rightArrow) popup->m_rightArrow->setVisible(false);
                            if (popup->m_counterLabel) popup->m_counterLabel->setVisible(false);
                            return;
                        }
                        popup->m_thumbnails = thumbs;
                        if (popup->m_currentIndex >= static_cast<int>(thumbs.size()))
                            popup->m_currentIndex = static_cast<int>(thumbs.size()) - 1;
                        if (popup->m_currentIndex < 0) popup->m_currentIndex = 0;
                        popup->loadThumbnailAt(popup->m_currentIndex);
                    });
                }
            } else {
                PaimonNotify::create(Localization::get().getString("level.delete_error") + msg, NotificationIcon::Error)->show();
                log::error("[ThumbnailViewPopup] Error al borrar miniatura: {}", msg);
            }
        });
    });
}

void LocalThumbnailViewPopup::onAcceptThumbBtn(CCObject*) {
    log::info("Aceptar thumbnail presionado en ThumbnailViewPopup para levelID={}", m_levelID);

    if (m_verificationCategory >= 0) {
        log::info("Aceptando thumbnail desde cola de verificacion (categoria: {})", m_verificationCategory);

        std::string username;
        auto* gm = GameManager::get();
        if (gm) {
            username = gm->m_playerName;
        } else {
            log::warn("[ThumbnailViewPopup] GameManager::get() es null");
        }

        PaimonNotify::create(Localization::get().getString("level.accepting").c_str(), NotificationIcon::Info)->show();

        std::string targetFilename = "";
        if (!m_suggestions.empty() && m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_suggestions.size())) {
            targetFilename = m_suggestions[m_currentIndex].filename;
        }

        ThumbnailAPI::get().acceptQueueItem(
            m_levelID,
            static_cast<PendingCategory>(m_verificationCategory),
            username,
            [levelID = m_levelID, category = m_verificationCategory](bool success, std::string const& message) {
                if (success) {
                    PendingQueue::get().accept(levelID, static_cast<PendingCategory>(category));
                    PaimonNotify::create(Localization::get().getString("level.accepted").c_str(), NotificationIcon::Success)->show();
                    log::info("[ThumbnailViewPopup] Miniatura aceptada para nivel {}", levelID);

                    // Invalidar cache para que se actualice al volver
                    ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                    ThumbnailLoader::get().invalidateLevel(levelID);
                } else {
                    PaimonNotify::create(Localization::get().getString("level.accept_error") + message, NotificationIcon::Error)->show();
                    log::error("[ThumbnailViewPopup] Error aceptando miniatura: {}", message);
                }
            },
            targetFilename
        );

        return;
    }

    log::info("Intentando aceptar desde LocalThumbs");

    auto pathOpt = LocalThumbs::get().getThumbPath(m_levelID);
    if (!pathOpt) {
        PaimonNotify::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
        return;
    }

    std::vector<uint8_t> pngData;
    if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
        PaimonNotify::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
        return;
    }

    size_t base64Size = ((pngData.size() + 2) / 3) * 4;
    log::info("PNG size: {} bytes ({:.2f} KB), Base64 size: ~{} bytes ({:.2f} KB)",
             pngData.size(), pngData.size() / 1024.0, base64Size, base64Size / 1024.0);

    log::warn("[ThumbnailViewPopup] Server upload disabled - thumbnail saved locally only");
    PaimonNotify::create(Localization::get().getString("level.saved_local_server_disabled").c_str(), NotificationIcon::Info)->show();
}

void LocalThumbnailViewPopup::onReportBtn(CCObject*) {
    int levelID = m_levelID;

    auto popup = ReportInputPopup::create(levelID, [levelID](std::string reason) {
        std::string user;
        auto* gm = GameManager::get();
        if (gm) {
            user = gm->m_playerName;
        }

        ThumbnailAPI::get().submitReport(levelID, user, reason, [levelID, reason](bool success, std::string const& message) {
            if (success) {
                PaimonNotify::create(Localization::get().getString("report.sent_synced") + reason, NotificationIcon::Warning)->show();
                log::info("[ThumbnailViewPopup] Reporte confirmado y enviado al servidor para nivel {}", levelID);
            } else {
                PaimonNotify::create(Localization::get().getString("report.saved_local").c_str(), NotificationIcon::Info)->show();
                log::warn("[ThumbnailViewPopup] Reporte guardado solo localmente para nivel {}", levelID);
            }
        });
    });

    if (popup) {
        popup->show();
    }
}

void LocalThumbnailViewPopup::onDeleteLocalThumb(CCObject*) {
    if (!m_viewingLocal || m_localThumbPaths.empty()) return;
    int count = static_cast<int>(m_localThumbPaths.size());
    if (m_localCurrentIndex < 0 || m_localCurrentIndex >= count) return;

    log::info("[ThumbnailViewPopup] Borrando thumbnail local indice {} de nivel {}", m_localCurrentIndex, m_levelID);

    if (!LocalThumbs::get().removeThumb(m_levelID, m_localCurrentIndex)) {
        PaimonNotify::create("Error al borrar miniatura", NotificationIcon::Error)->show();
        return;
    }

    // recargar lista
    m_localThumbPaths = LocalThumbs::get().getAllThumbPaths(m_levelID);
    int newCount = static_cast<int>(m_localThumbPaths.size());

    if (newCount == 0) {
        // ya no hay locales, mostrar "sin thumbnail"
        m_viewingLocal = false;
        if (m_leftArrow) m_leftArrow->setVisible(false);
        if (m_rightArrow) m_rightArrow->setVisible(false);
        if (m_counterLabel) m_counterLabel->setVisible(false);
        auto content = m_mainLayer->getContentSize();
        showNoThumbnail(content);
        PaimonNotify::create("Miniatura borrada", NotificationIcon::Success)->show();
        return;
    }

    // ajustar indice si quedo fuera de rango
    if (m_localCurrentIndex >= newCount) {
        m_localCurrentIndex = newCount - 1;
    }

    auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
    if (tex) {
        auto content = m_mainLayer->getContentSize();
        float maxWidth = content.width - 40.f;
        float maxHeight = content.height - 80.f;
        displayThumbnail(tex, maxWidth, maxHeight, content, false);
    }

    if (m_leftArrow) m_leftArrow->setVisible(newCount > 1);
    if (m_rightArrow) m_rightArrow->setVisible(newCount > 1);
    if (m_counterLabel) {
        m_counterLabel->setVisible(newCount > 1);
        m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, newCount).c_str());
    }

    PaimonNotify::create("Miniatura borrada", NotificationIcon::Success)->show();
}

void LocalThumbnailViewPopup::onDeleteThumbnail(CCObject*) {
    int levelID = m_levelID;
    auto gm = GameManager::get();
    auto am = GJAccountManager::get();
    std::string username = gm ? gm->m_playerName : "";
    int accountID = am ? am->m_accountID : 0;

    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    ThumbnailAPI::get().getRating(levelID, username, thumbnailId, [self, levelID, username, accountID](bool /*success*/, float /*avg*/, int count, int /*userVote*/) {
        auto popup = self.lock();
        if (!popup) return;

        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, levelID, username, accountID, count](bool isMod, bool isAdmin) {
            auto popup = self.lock();
            if (!popup) return;

            if (!isMod && !isAdmin) {
                 PaimonNotify::create(Localization::get().getString("level.no_permissions"), NotificationIcon::Error)->show();
                 return;
            }

            if (count > 100 && !isAdmin) {
                PaimonNotify::create(Localization::get().getString("level.admin_only_high_votes"), NotificationIcon::Error)->show();
                return;
            }

            geode::createQuickPopup(
                Localization::get().getString("level.confirm_delete_title").c_str(),
                Localization::get().getString("level.confirm_delete_msg").c_str(),
                Localization::get().getString("general.cancel").c_str(), Localization::get().getString("level.delete_button").c_str(),
                [self, levelID, username, accountID](auto, bool btn2) {
                    auto popup = self.lock();
                    if (!popup) return;

                    if (btn2) {
                        auto spinner2 = PaimonLoadingOverlay::create("Loading...", 30.f);
                        spinner2->show(popup->m_mainLayer, 100);
                        Ref<PaimonLoadingOverlay> loading = spinner2;

                        std::string thumbnailId = "";
                        if (popup->m_currentIndex >= 0 && popup->m_currentIndex < static_cast<int>(popup->m_thumbnails.size())) {
                            thumbnailId = popup->m_thumbnails[popup->m_currentIndex].id;
                        }
                        ThumbnailAPI::get().deleteThumbnail(levelID, thumbnailId, username, accountID, [self, loading, levelID](bool success, std::string msg) {
                            if (loading) loading->dismiss();

                            auto popup = self.lock();
                            if (!popup) return;

                            if (success) {
                                PaimonNotify::create(Localization::get().getString("level.thumbnail_deleted"), NotificationIcon::Success)->show();

                                // Invalidar cache de galeria y nivel
                                ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                                ThumbnailLoader::get().invalidateLevel(levelID);

                                ThumbnailAPI::get().getThumbnails(levelID, [self](bool ok, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                                    auto popup = self.lock();
                                    if (!popup || !popup->isUiAlive()) return;
                                    if (!ok || thumbs.empty()) {
                                        popup->m_thumbnails.clear();
                                        popup->showNoThumbnail(popup->m_mainLayer->getContentSize());
                                        if (popup->m_leftArrow) popup->m_leftArrow->setVisible(false);
                                        if (popup->m_rightArrow) popup->m_rightArrow->setVisible(false);
                                        if (popup->m_counterLabel) popup->m_counterLabel->setVisible(false);
                                        return;
                                    }
                                    popup->m_thumbnails = thumbs;
                                    if (popup->m_currentIndex >= static_cast<int>(popup->m_thumbnails.size())) {
                                        popup->m_currentIndex = static_cast<int>(popup->m_thumbnails.size()) - 1;
                                    }
                                    if (popup->m_currentIndex < 0) popup->m_currentIndex = 0;
                                    popup->loadThumbnailAt(popup->m_currentIndex);
                                });
                            } else {
                                PaimonNotify::create(msg.c_str(), NotificationIcon::Error)->show();
                            }
                        });
                    }
                }
            );
        });
    });
}

// ====================================================================
// Recentrar, Clamp, Touch
// ====================================================================

void LocalThumbnailViewPopup::onRecenter(CCObject*) {
    if (!m_thumbnailSprite) return;

    m_thumbnailSprite->stopAllActions();
    resetZoomGestureState();

    auto content = this->m_mainLayer->getContentSize();
    float centerX = content.width * 0.5f;
    float centerY = content.height * 0.5f + 10.f;

    auto moveTo = CCMoveTo::create(0.3f, {centerX, centerY});
    auto scaleTo = CCScaleTo::create(0.3f, m_initialScale);
    auto easeMove = CCEaseSineOut::create(moveTo);
    auto easeScale = CCEaseSineOut::create(scaleTo);

    m_thumbnailSprite->runAction(easeMove);
    m_thumbnailSprite->runAction(easeScale);
    m_thumbnailSprite->setAnchorPoint({0.5f, 0.5f});
}

float LocalThumbnailViewPopup::clamp(float value, float min, float max) {
    return std::max(min, std::min(value, max));
}

bool LocalThumbnailViewPopup::applyZoomAtWorldPoint(float targetScale, CCPoint worldPoint) {
    if (!m_thumbnailSprite) return false;

    auto* parent = m_thumbnailSprite->getParent();
    if (!parent) return false;

    float currentScale = m_thumbnailSprite->getScale();
    float clampedScale = clamp(targetScale, m_minScale, m_maxScale);
    if (std::abs(clampedScale - currentScale) < 0.001f) {
        return false;
    }

    CCPoint focusInParent = parent->convertToNodeSpace(worldPoint);
    CCPoint position = m_thumbnailSprite->getPosition();
    CCPoint anchor = m_thumbnailSprite->getAnchorPoint();
    CCSize contentSize = m_thumbnailSprite->getContentSize();
    CCPoint anchorOffset = {
        contentSize.width * anchor.x,
        contentSize.height * anchor.y,
    };

    CCPoint localFocus = {
        ((focusInParent.x - position.x) / currentScale) + anchorOffset.x,
        ((focusInParent.y - position.y) / currentScale) + anchorOffset.y,
    };

    m_thumbnailSprite->setScale(clampedScale);
    m_thumbnailSprite->setPosition({
        focusInParent.x - (localFocus.x - anchorOffset.x) * clampedScale,
        focusInParent.y - (localFocus.y - anchorOffset.y) * clampedScale,
    });

    clampSpritePosition();
    return true;
}

void LocalThumbnailViewPopup::syncZoomGestureState() {
    m_savedScale = m_thumbnailSprite ? m_thumbnailSprite->getScale() : m_initialScale;

    if (m_touches.size() >= 2) {
        auto it = m_touches.begin();
        auto firstTouch = *it;
        ++it;
        auto secondTouch = *it;

        auto firstLoc = firstTouch->getLocation();
        auto secondLoc = secondTouch->getLocation();
        m_touchMidPoint = (firstLoc + secondLoc) / 2.0f;
        m_initialDistance = std::max(firstLoc.getDistance(secondLoc), 0.1f);
        return;
    }

    if (m_touches.size() == 1) {
        m_touchMidPoint = (*m_touches.begin())->getLocation();
    } else {
        m_touchMidPoint = getZoomFocusPoint();
    }

    m_initialDistance = 0.0f;
}

void LocalThumbnailViewPopup::resetZoomGestureState() {
    m_initialDistance = 0.0f;
    m_savedScale = m_thumbnailSprite ? m_thumbnailSprite->getScale() : m_initialScale;
    m_touchMidPoint = getZoomFocusPoint();
    m_wasZooming = false;
}

CCPoint LocalThumbnailViewPopup::getZoomFocusPoint() const {
    if (m_touches.size() >= 2) {
        auto it = m_touches.begin();
        auto firstTouch = *it;
        ++it;
        auto secondTouch = *it;
        return (firstTouch->getLocation() + secondTouch->getLocation()) / 2.0f;
    }

    if (m_touches.size() == 1) {
        return (*m_touches.begin())->getLocation();
    }

    if (m_clippingNode) {
        return m_clippingNode->convertToWorldSpace({m_viewWidth * 0.5f, m_viewHeight * 0.5f});
    }

    if (m_mainLayer) {
        auto content = m_mainLayer->getContentSize();
        return m_mainLayer->convertToWorldSpace({content.width * 0.5f, content.height * 0.5f});
    }

    return {0.f, 0.f};
}

void LocalThumbnailViewPopup::clampSpritePosition() {
    if (!m_thumbnailSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

    float scale = m_thumbnailSprite->getScale();
    float spriteW = m_thumbnailSprite->getContentWidth() * scale;
    float spriteH = m_thumbnailSprite->getContentHeight() * scale;

    CCPoint pos = m_thumbnailSprite->getPosition();
    CCPoint anchor = m_thumbnailSprite->getAnchorPoint();

    float spriteLeft = pos.x - spriteW * anchor.x;
    float spriteRight = pos.x + spriteW * (1.0f - anchor.x);
    float spriteBottom = pos.y - spriteH * anchor.y;
    float spriteTop = pos.y + spriteH * (1.0f - anchor.y);

    float newX = pos.x;
    float newY = pos.y;

    if (spriteW <= m_viewWidth) {
        newX = m_viewWidth / 2;
    } else {
        if (spriteLeft > 0) {
            newX = spriteW * anchor.x;
        }
        if (spriteRight < m_viewWidth) {
            newX = m_viewWidth - spriteW * (1.0f - anchor.x);
        }
    }

    if (spriteH <= m_viewHeight) {
        newY = m_viewHeight / 2;
    } else {
        if (spriteBottom > 0) {
            newY = spriteH * anchor.y;
        }
        if (spriteTop < m_viewHeight) {
            newY = m_viewHeight - spriteH * (1.0f - anchor.y);
        }
    }

    m_thumbnailSprite->setPosition({newX, newY});
}

void LocalThumbnailViewPopup::clampSpritePositionAnimated() {
    if (!m_thumbnailSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

    float scale = m_thumbnailSprite->getScale();
    float spriteW = m_thumbnailSprite->getContentWidth() * scale;
    float spriteH = m_thumbnailSprite->getContentHeight() * scale;

    CCPoint pos = m_thumbnailSprite->getPosition();
    CCPoint anchor = m_thumbnailSprite->getAnchorPoint();

    float spriteLeft = pos.x - spriteW * anchor.x;
    float spriteRight = pos.x + spriteW * (1.0f - anchor.x);
    float spriteBottom = pos.y - spriteH * anchor.y;
    float spriteTop = pos.y + spriteH * (1.0f - anchor.y);

    float newX = pos.x;
    float newY = pos.y;
    bool needsAnimation = false;

    if (spriteW <= m_viewWidth) {
        if (std::abs(newX - m_viewWidth / 2) > 0.5f) {
            newX = m_viewWidth / 2;
            needsAnimation = true;
        }
    } else {
        if (spriteLeft > 0) {
            newX = spriteW * anchor.x;
            needsAnimation = true;
        }
        if (spriteRight < m_viewWidth) {
            newX = m_viewWidth - spriteW * (1.0f - anchor.x);
            needsAnimation = true;
        }
    }

    if (spriteH <= m_viewHeight) {
        if (std::abs(newY - m_viewHeight / 2) > 0.5f) {
            newY = m_viewHeight / 2;
            needsAnimation = true;
        }
    } else {
        if (spriteBottom > 0) {
            newY = spriteH * anchor.y;
            needsAnimation = true;
        }
        if (spriteTop < m_viewHeight) {
            newY = m_viewHeight - spriteH * (1.0f - anchor.y);
            needsAnimation = true;
        }
    }

    if (needsAnimation) {
        m_thumbnailSprite->stopAllActions();
        auto moveTo = CCMoveTo::create(0.15f, {newX, newY});
        auto ease = CCEaseBackOut::create(moveTo);
        m_thumbnailSprite->runAction(ease);
    }
}

bool LocalThumbnailViewPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!this->isVisible()) return false;

    // Helper: buscar item tocado en un menu
    auto findTouchedItem = [](CCMenu* menu, CCTouch* touch) -> CCMenuItem* {
        if (!menu || !menu->isVisible()) return nullptr;
        auto point = menu->convertTouchToNodeSpace(touch);
        for (auto* obj : CCArrayExt<CCObject*>(menu->getChildren())) {
            auto item = typeinfo_cast<CCMenuItem*>(obj);
            if (item && item->isVisible() && item->isEnabled()) {
                if (item->boundingBox().containsPoint(point)) {
                    return item;
                }
            }
        }
        return nullptr;
    };

    // Buscar en todos los menus del popup
    CCMenuItem* hit = nullptr;
    if (!hit) hit = findTouchedItem(m_playBtnMenu, touch);
    if (!hit) hit = findTouchedItem(m_buttonMenu, touch);
    if (!hit) hit = findTouchedItem(m_ratingMenu, touch);
    if (!hit) hit = findTouchedItem(m_settingsMenu, touch);
    // Gallery arrows menu
    if (!hit && m_leftArrow) {
        hit = findTouchedItem(static_cast<CCMenu*>(m_leftArrow->getParent()), touch);
    }
    // Close button menu
    if (!hit && m_closeBtn) {
        hit = findTouchedItem(static_cast<CCMenu*>(m_closeBtn->getParent()), touch);
    }

    if (hit) {
        hit->selected();
        m_activatedItem = hit;
        return true;
    }

    // If video is playing, a single tap on the video area should pause it
    if (m_videoPlaying && m_thumbnailSprite) {
        auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite);
        if (videoSprite) {
            // Check if the touch is inside the clipping area (video area)
            if (m_clippingNode) {
                auto local = m_clippingNode->convertTouchToNodeSpace(touch);
                auto clipSize = m_clippingNode->getContentSize();
                if (local.x >= 0 && local.y >= 0 && local.x <= clipSize.width && local.y <= clipSize.height) {
                    m_videoPlaying = false;
                    videoSprite->pause();
                    updatePlayButton();
                    return true;
                }
            } else if (m_mainLayer) {
                auto local = m_mainLayer->convertTouchToNodeSpace(touch);
                auto contentSize = m_mainLayer->getContentSize();
                if (local.x >= 0 && local.y >= 0 && local.x <= contentSize.width && local.y <= contentSize.height) {
                    m_videoPlaying = false;
                    videoSprite->pause();
                    updatePlayButton();
                    return true;
                }
            }
        }
    }

    // Solo aceptar toques dentro del area del popup para zoom/pan.
    // Toques fuera se dejan pasar a otras capas/mods.
    if (m_bgSprite) {
        auto local = m_bgSprite->convertTouchToNodeSpace(touch);
        auto size = m_bgSprite->getContentSize();
        if (local.x < 0 || local.y < 0 || local.x > size.width || local.y > size.height) {
            return false;
        }
    }

    // Zoom/pan: track touch
    if (m_touches.size() == 1) {
        auto firstTouch = *m_touches.begin();
        if (firstTouch == touch) return true;
    }

    m_touches.insert(touch);
    syncZoomGestureState();
    return true;
}

void LocalThumbnailViewPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    // Si estamos rastreando un menu item, verificar si el dedo sigue encima
    if (m_activatedItem) {
        auto menu = static_cast<CCMenu*>(m_activatedItem->getParent());
        if (menu) {
            auto point = menu->convertTouchToNodeSpace(touch);
            if (!m_activatedItem->boundingBox().containsPoint(point)) {
                m_activatedItem->unselected();
                m_activatedItem = nullptr;
            }
        }
        return;
    }

    if (!m_thumbnailSprite) return;

    if (m_touches.size() == 1) {
        auto delta = touch->getDelta();
        m_thumbnailSprite->setPosition({
            m_thumbnailSprite->getPositionX() + delta.x,
            m_thumbnailSprite->getPositionY() + delta.y
        });
        clampSpritePosition();
    } else if (m_touches.size() == 2) {
        m_wasZooming = true;

        auto it = m_touches.begin();
        auto firstTouch = *it;
        ++it;
        auto secondTouch = *it;

        auto firstLoc = firstTouch->getLocation();
        auto secondLoc = secondTouch->getLocation();
        auto center = (firstLoc + secondLoc) / 2.0f;
        auto distNow = firstLoc.getDistance(secondLoc);

        if (m_initialDistance < 0.1f) m_initialDistance = 0.1f;
        if (distNow < 0.1f) distNow = 0.1f;

        auto zoom = m_savedScale * (distNow / m_initialDistance);
        applyZoomAtWorldPoint(zoom, center);
        m_touchMidPoint = center;
    }
}

void LocalThumbnailViewPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    // Si estamos rastreando un menu item, activarlo
    if (m_activatedItem) {
        m_activatedItem->unselected();
        m_activatedItem->activate();
        m_activatedItem = nullptr;
        return;
    }

    m_touches.erase(touch);

    if (!m_thumbnailSprite) return;

    if (m_wasZooming && m_touches.size() == 1) {
        auto scale = m_thumbnailSprite->getScale();

        if (scale < m_minScale) {
            m_thumbnailSprite->runAction(
                CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_minScale))
            );
        } else if (scale > m_maxScale) {
            m_thumbnailSprite->runAction(
                CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_maxScale))
            );
        }

        m_wasZooming = false;
    }

    syncZoomGestureState();

    if (m_touches.empty()) {
        clampSpritePositionAnimated();
    }
}

void LocalThumbnailViewPopup::scrollWheel(float x, float y) {
    if (!m_mainLayer || !m_thumbnailSprite) {
        return;
    }

    if (!m_thumbnailSprite->getParent()) {
        m_thumbnailSprite = nullptr;
        return;
    }

    float scrollAmount = y;
    if (std::abs(y) < 0.001f) {
        scrollAmount = -x;
    }

    float zoomFactor = scrollAmount > 0 ? 1.12f : 0.89f;

    float currentScale = m_thumbnailSprite->getScale();
    float newScale = currentScale * zoomFactor;

    newScale = clamp(newScale, m_minScale, m_maxScale);

    if (std::abs(newScale - currentScale) < 0.001f) {
        return;
    }

    m_wasZooming = true;
    applyZoomAtWorldPoint(newScale, getZoomFocusPoint());
    syncZoomGestureState();
}

void LocalThumbnailViewPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (m_activatedItem) {
        m_activatedItem->unselected();
        m_activatedItem = nullptr;
    }
    m_touches.erase(touch);
    resetZoomGestureState();
}

// ====================================================================
// Factory
// ====================================================================

LocalThumbnailViewPopup* LocalThumbnailViewPopup::create(int32_t levelID, bool canAcceptUpload) {
    auto ret = new LocalThumbnailViewPopup();
    if (ret && ret->init(400.f, 280.f)) {
        ret->setup({levelID, canAcceptUpload});
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, std::vector<Suggestion> const& suggestions) {
    auto ret = LocalThumbnailViewPopup::create(levelID, canAcceptUpload);
    if (ret) {
        ret->setSuggestions(suggestions);
    }
    return ret;
}

// NOTE: onSettings() is implemented in src/hooks/LevelInfoLayer.cpp
// because it needs access to PaimonLevelInfoLayer ($modify type).
