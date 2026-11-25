#include "../../../utils/FilesystemPath.hpp"
#include "LocalThumbnailViewPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../framework/state/SessionState.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../smooth-scroll/services/SmoothScrollController.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/PopupManager.hpp>
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
#include "../services/ThumbnailExport.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/Assets.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/Constants.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../utils/UIBorderHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../utils/UrlKeyNormalize.hpp"
#include "../../profiles/ui/RatePopup.hpp"
#include "ReportInputPopup.hpp"
#include "ThumbnailOrderPopup.hpp"
#include "ThumbnailSettingsPopup.hpp"
#include "ThumbViewerChrome.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
constexpr float kThumbnailRefreshCooldown = 5.f;
constexpr GLubyte kRefreshBtnEnabledOpacity = 255;
constexpr GLubyte kRefreshBtnDisabledOpacity = 105;
void setBtnEnabled(CCMenuItemSpriteExtra* btn, bool enabled) {
    if (!btn) return;
    btn->setEnabled(enabled);
    btn->setOpacity(enabled ? kRefreshBtnEnabledOpacity : kRefreshBtnDisabledOpacity);
}
}

void LocalThumbnailViewPopup::updateOrderUiState() {
    bool canUseOrderTools = m_isAdmin && m_verificationCategory < 0 && !m_viewingLocal && m_thumbnails.size() > 1;
    if (m_orderEditBtn) {
        m_orderEditBtn->setVisible(canUseOrderTools);
        setBtnEnabled(m_orderEditBtn, canUseOrderTools);
        if (m_buttonMenu) m_buttonMenu->updateLayout();
    }
}

void LocalThumbnailViewPopup::replaceRemoteThumbnails(std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs, std::string const& preferredId) {
    m_viewingLocal = false;
    m_thumbnails = thumbs;

    if (m_thumbnails.empty()) {
        m_currentIndex = 0;
        if (m_leftArrow) m_leftArrow->setVisible(false);
        if (m_rightArrow) m_rightArrow->setVisible(false);
        if (m_counterLabel) m_counterLabel->setVisible(false);
        updateOrderUiState();
        return;
    }

    int newIndex = 0;
    if (!preferredId.empty()) {
        auto it = std::find_if(m_thumbnails.begin(), m_thumbnails.end(), [&preferredId](ThumbnailAPI::ThumbnailInfo const& thumb) {
            return thumb.id == preferredId;
        });
        if (it != m_thumbnails.end()) {
            newIndex = static_cast<int>(std::distance(m_thumbnails.begin(), it));
        }
    }

    m_currentIndex = std::clamp(newIndex, 0, static_cast<int>(m_thumbnails.size()) - 1);

    if (m_leftArrow) m_leftArrow->setVisible(m_thumbnails.size() > 1);
    if (m_rightArrow) m_rightArrow->setVisible(m_thumbnails.size() > 1);
    if (m_counterLabel) {
        m_counterLabel->setVisible(m_thumbnails.size() > 1);
        m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_thumbnails.size()).c_str());
    }

    updateOrderUiState();
    syncInfoBar();
}

void LocalThumbnailViewPopup::ensureOrderControls(float /*contentwidth*/) {
    if (!m_isAdmin || !m_buttonMenu || m_verificationCategory >= 0 || m_orderEditBtn) return;

    auto* orderSpr = paimon::ui::makeButtonSprite(Localization::get().getString("viewer.order").c_str(),
        paimon::ui::Btn::Cyan, paimon::thumbviewer::kToolWidth, 0.65f, "goldFont.fnt");
    if (orderSpr) {
        m_orderEditBtn = CCMenuItemSpriteExtra::create(orderSpr, this, menu_selector(LocalThumbnailViewPopup::onOrderEdit));
        m_orderEditBtn->setID("thumbnail-order-edit-btn"_spr);

        m_buttonMenu->addChild(m_orderEditBtn);
        m_buttonMenu->updateLayout();
    }
}

void LocalThumbnailViewPopup::updateRefreshButtonState() {
    if (!m_refreshBtn) return;

    bool enabled = !m_refreshCooldownActive && !m_isDownloading;
    setBtnEnabled(m_refreshBtn, enabled);

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
    if (!m_suggestions.empty()) {
        stepSuggestion(-1);
        return;
    }
    if (m_viewingLocal && !m_localThumbPaths.empty()) {
        int count = static_cast<int>(m_localThumbPaths.size());
        m_localCurrentIndex--;
        if (m_localCurrentIndex < 0) m_localCurrentIndex = count - 1;

        auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
        if (tex) {
            auto content = m_mainLayer->getContentSize();
            float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
            float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
            displayThumbnail(tex, maxWidth, maxHeight, content, false);
        }
        if (m_counterLabel) {
            m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, count).c_str());
        }
        syncInfoBar();
        return;
    }

    if (m_thumbnails.empty() || m_isDownloading) return;
    m_currentIndex--;
    if (m_currentIndex < 0) m_currentIndex = static_cast<int>(m_thumbnails.size()) - 1;
    loadThumbnailAt(m_currentIndex);
}

void LocalThumbnailViewPopup::onNext(CCObject*) {
    m_navDirection = NavDirection::Right;
    if (!m_suggestions.empty()) {
        stepSuggestion(1);
        return;
    }
    if (m_viewingLocal && !m_localThumbPaths.empty()) {
        int count = static_cast<int>(m_localThumbPaths.size());
        m_localCurrentIndex++;
        if (m_localCurrentIndex >= count) m_localCurrentIndex = 0;

        auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
        if (tex) {
            auto content = m_mainLayer->getContentSize();
            float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
            float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
            displayThumbnail(tex, maxWidth, maxHeight, content, false);
        }
        if (m_counterLabel) {
            m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, count).c_str());
        }
        syncInfoBar();
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

        // timestamp-looking ids derive a date.
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

    m_cachedInfoId = id;

    auto* alert = FLAlertLayer::create(
        this,
        "Thumbnail Info",
        info,
        "OK",
        "Copy ID",
        340.f
    );
    if (alert) PopupManager::get().manage(alert).showInstant();
}

void LocalThumbnailViewPopup::FLAlert_Clicked(FLAlertLayer*, bool btn2) {
    if (btn2 && m_cachedInfoId != "Unknown" && !m_cachedInfoId.empty()) {
        geode::utils::clipboard::write(m_cachedInfoId);
        PaimonNotify::show("ID copied to clipboard!", geode::NotificationIcon::Success);
    }
}

void LocalThumbnailViewPopup::applyRatingLabel() {
    if (m_infoBar) m_infoBar->setRating(m_hasRatingData, m_ratingAverage, m_ratingCount);

    if (!m_ratingLabel) return;

    if (!m_hasRatingData) {
        m_ratingLabel->setString("...");
        m_ratingLabel->setColor({255, 255, 255});
        return;
    }

    m_ratingLabel->setString(fmt::format("{:.1f} ({})", m_ratingAverage, m_ratingCount).c_str());
    if (m_ratingCount == 0) {
        m_ratingLabel->setColor({255, 100, 100});
    } else {
        m_ratingLabel->setColor({255, 255, 255});
    }
}

void LocalThumbnailViewPopup::syncInfoBar() {
    if (!m_infoBar) return;

    int index = 0;
    int total = 0;
    std::string creator;
    if (m_viewingLocal) {
        index = m_localCurrentIndex;
        total = static_cast<int>(m_localThumbPaths.size());
    } else if (!m_suggestions.empty()) {
        index = m_currentIndex;
        total = static_cast<int>(m_suggestions.size());
    } else {
        index = m_currentIndex;
        total = static_cast<int>(m_thumbnails.size());
        if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
            creator = m_thumbnails[m_currentIndex].creator;
        }
    }

    m_infoBar->setLevelId(m_levelID);
    m_infoBar->setCreator(creator);
    m_infoBar->setCounter(index, total);
    if (m_leftArrow) m_leftArrow->setVisible(m_focusTransition.progress() < 1.f && total > 1);
    if (m_rightArrow) m_rightArrow->setVisible(m_focusTransition.progress() < 1.f && total > 1);
}

void LocalThumbnailViewPopup::refreshRating() {
    std::string thumbnailId;
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }

    if (thumbnailId.empty()) {
        m_hasRatingData = false;
        m_ratingAverage = 0.f;
        m_ratingCount = 0;
        m_userVote = 0;
        m_initialUserVote = 0;
        applyRatingLabel();
        return;
    }

    m_hasRatingData = false;
    applyRatingLabel();

    std::string username = "Unknown";
    if (auto gm = GameManager::get()) username = gm->m_playerName;

    WeakRef<LocalThumbnailViewPopup> self = this;
    int requestToken = ++m_ratingRequestToken;
    ThumbnailAPI::get().getRating(m_levelID, username, thumbnailId, [self, requestToken, thumbnailId](bool success, float average, int count, int userVote) {
        auto popup = self.lock();
        if (!popup) return;
        if (requestToken != popup->m_ratingRequestToken) return;

        std::string currentThumbId;
        if (popup->m_currentIndex >= 0 && popup->m_currentIndex < static_cast<int>(popup->m_thumbnails.size())) {
            currentThumbId = popup->m_thumbnails[popup->m_currentIndex].id;
        }
        if (currentThumbId != thumbnailId) return;

        if (!success) {
            log::warn("[ThumbnailViewPopup] Failed to get rating for level {}", popup->m_levelID);
            popup->m_hasRatingData = false;
            popup->m_ratingAverage = 0.f;
            popup->m_ratingCount = 0;
            popup->m_userVote = 0;
            popup->m_initialUserVote = 0;
            popup->applyRatingLabel();
            return;
        }

        popup->m_hasRatingData = true;
        popup->m_ratingAverage = average;
        popup->m_ratingCount = count;
        popup->m_userVote = userVote;
        popup->m_initialUserVote = userVote;
        popup->applyRatingLabel();
    });
}

void LocalThumbnailViewPopup::loadThumbnailAt(int index) {
    if (index < 0 || index >= static_cast<int>(m_thumbnails.size())) return;

    m_isDownloading = true;

    int requestToken = ++m_galleryRequestToken;
    auto& thumb = m_thumbnails[index];
    updateRefreshButtonState();

    // cache key is the thumbnail id, not the request token.
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
    updateOrderUiState();

    refreshRating();
    syncInfoBar();

    Ref<LocalThumbnailViewPopup> self = this;

    if (thumb.isVideo() && !thumb.url.empty()) {
        log::info("[ThumbnailViewPopup] loadThumbnailAt: video detected for index={}", index);
        std::string videoToken = thumb.id.empty() ? thumb.url : thumb.id;
        std::string cacheKey = fmt::format("popup_video_{}_{}", m_levelID, std::hash<std::string>{}(videoToken));
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
            float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
            float maxHeight = paimon::thumbviewer::imageArea(content).size.height;

            self->displayVideoThumbnail(videoSprite, maxWidth, maxHeight, content);

            log::info("[ThumbnailViewPopup] loadThumbnailAt: video ready for index={}", index);
        });
        return;
    }

    // ram hits show at once; misses wait for the callback.
    {
        auto& cache = paimon::cache::ThumbnailCache::get();
        auto ramTex = cache.getUrlFromRam(url);
        if (ramTex.has_value() && ramTex.value()) {
            log::info("[ThumbnailViewPopup] loadThumbnailAt: instant RAM cache hit for index={}", index);
            m_isDownloading = false;
            auto content = m_mainLayer->getContentSize();
            float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
            float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
            displayThumbnail(ramTex.value(), maxWidth, maxHeight, content, false);

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

    // manual open clears the failure marker for a retry.
    {
        auto& cache = paimon::cache::ThumbnailCache::get();
        std::string normalizedUrl = paimon::cache::normalizeUrlKey(url);
        std::string failKey = "url_" + normalizedUrl;
        if (cache.isFailed(failKey)) {
            log::info("[ThumbnailViewPopup] loadThumbnailAt: clearing failed cache for url to force retry");
            cache.clearFailed(failKey);
        }
    }

    ThumbnailLoader::get().requestUrlLoad(url, [self, requestToken, index](CCTexture2D* tex, bool success) {
        if (!self->isUiAlive()) return;
        if (requestToken != self->m_galleryRequestToken) return;

        self->m_isDownloading = false;

        if (success && tex) {
            auto content = self->m_mainLayer->getContentSize();
            float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
            float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
            self->displayThumbnail(tex, maxWidth, maxHeight, content, false);

            int count = static_cast<int>(self->m_thumbnails.size());
            if (count > 1) {
                auto prefetch = [&thumbs = self->m_thumbnails, count](int idx) {
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
        } else {
            bool fallbackApplied = false;
            if (self->m_levelID > 0) {
                auto localTex = LocalThumbs::get().loadTexture(self->m_levelID);
                if (localTex) {
                    auto content = self->m_mainLayer->getContentSize();
                    float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
                    float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
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
                    float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
                    float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
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

    if (m_counterLabel) {
        m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str());
    }

    if (m_leftArrow) m_leftArrow->setVisible(m_suggestions.size() > 1);
    if (m_rightArrow) m_rightArrow->setVisible(m_suggestions.size() > 1);

    syncInfoBar();

    std::string url = HttpClient::get().buildAssetURL(suggestion.filename, "suggestions");
    int requestToken = ++m_suggestionRequestToken;
    int requestedIndex = m_currentIndex;

    Ref<LocalThumbnailViewPopup> safeRef = this;

    ThumbnailAPI::get().downloadFromUrl(url, [safeRef, requestToken, requestedIndex](bool success, CCTexture2D* tex) {
        if (!safeRef->isUiAlive()) return;
        if (requestToken != safeRef->m_suggestionRequestToken
            || requestedIndex != safeRef->m_currentIndex) return;

        auto content = safeRef->m_mainLayer->getContentSize();
        if (success && tex) {
            float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
            float maxHeight = paimon::thumbviewer::imageArea(content).size.height;

            safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, false);
        } else {
            safeRef->showNoThumbnail(content);
        }
    });
}

void LocalThumbnailViewPopup::stepSuggestion(int delta) {
    if (m_suggestions.empty()) return;
    int size = static_cast<int>(m_suggestions.size());
    m_currentIndex = (m_currentIndex + delta % size + size) % size;
    loadCurrentSuggestion();
}

void LocalThumbnailViewPopup::onNextSuggestion(CCObject*) {
    stepSuggestion(1);
}

void LocalThumbnailViewPopup::onPrevSuggestion(CCObject*) {
    stepSuggestion(-1);
}


void LocalThumbnailViewPopup::onExit() {
    m_touches.clear();
    resetZoomGestureState();

    if (m_isExiting) {
        return;
    }
    m_isExiting = true;
    this->unschedule(schedule_selector(LocalThumbnailViewPopup::tickFocus));
    ++m_galleryRequestToken;
    ++m_ratingRequestToken;
    if (m_invalidationListenerId != 0) {
        ThumbnailLoader::get().removeInvalidationListener(m_invalidationListenerId);
        m_invalidationListenerId = 0;
    }

    // stop video and detach menus before removeallchildren: m_mainlayer would free them mid-deref.
    if (m_thumbnailSprite) {
        if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite)) {
            videoSprite->stop();
        }
    }
    m_thumbnailSprite = nullptr;
    m_clippingNode = nullptr;
    m_imageStencil = nullptr;
    m_imageBorder = nullptr;
    m_imageBackdrop = nullptr;
    m_headerButtonPoses.clear();

    m_videoPlaying = false;
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }

    if (m_mainLayer) {
        m_mainLayer->removeAllChildren();
    }
    m_ratingMenu = nullptr;
    m_buttonMenu = nullptr;
    m_settingsMenu = nullptr;
    m_refreshBtn = nullptr;
    m_ratingLabel = nullptr;
    m_counterLabel = nullptr;
    m_leftArrow = nullptr;
    m_rightArrow = nullptr;
    m_orderEditBtn = nullptr;
    m_infoBar = nullptr;
    m_sidebarBg = nullptr;
    m_hideChromeBtn = nullptr;

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
    ratingContainer->setVisible(false);
    m_mainLayer->addChild(ratingContainer, 100);

    auto bg = paimon::ui::makeInset({74.f, 16.f}, 125);
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

    applyRatingLabel();
}

void LocalThumbnailViewPopup::onRate(CCObject*) {
    std::string thumbnailId = "";
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        thumbnailId = m_thumbnails[m_currentIndex].id;
    }
    auto popup = RatePopup::create(m_levelID, thumbnailId);
    WeakRef<LocalThumbnailViewPopup> self = this;
    popup->m_onRateCallback = [self]() {
        auto view = self.lock();
        if (!view) return;
        view->refreshRating();
    };
    popup->show();
}


bool LocalThumbnailViewPopup::init(float width, float height) {
     if (!PaimonPopup::init(width, height)) return false;
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

    // outside verification: restore pending moderator suggestions.
    if (verificationCategory < 0) {
        std::string modCode = geode::Mod::get()->getSavedValue<std::string>("mod-code", "");
        if (!modCode.empty()) {
            auto pendingItems = PendingQueue::get().list(PendingCategory::Verify);
            for (auto const& item : pendingItems) {
                if (item.levelID == m_levelID && item.status == PendingStatus::Open && !item.suggestions.empty()) {
                    log::info("[ThumbnailViewPopup] Found pending thumbnail for level {} in local queue ({} suggestion(s))", m_levelID, item.suggestions.size());
                    m_verificationCategory = 0;
                    m_suggestions = item.suggestions;
                    verificationCategory = 0;
                    break;
                }
            }
        }
    }

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

                popup->replaceRemoteThumbnails(thumbs, currentId);
                popup->loadThumbnailAt(popup->m_currentIndex);
                popup->setupRating();
            });
        });
    }

    if (m_bgSprite) m_bgSprite->setVisible(true);

    namespace tv = paimon::thumbviewer;
    auto const content = m_mainLayer->getContentSize();
    auto const image = tv::imageArea(content);
    auto const actions = tv::actionArea(content);
    float const maxWidth = image.size.width;
    float const maxHeight = image.size.height;

    if (m_closeBtn) {
        m_closeBtn->setScale(0.75f);
        m_closeBtn->setPosition({4.f, content.height - 4.f});
    }

    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(maxWidth, maxHeight, 6.f);
    m_imageStencil = stencil;
    auto* clip = CCClippingNode::create(stencil);
    m_clippingNode = clip;
    clip->setContentSize(image.size);
    clip->setAnchorPoint({0.5f, 0.5f});
    clip->setPosition({image.getMidX(), image.getMidY()});
    m_mainLayer->addChild(clip, 1);
    m_imageBackdrop = CCLayerColor::create({0, 0, 0, 255}, maxWidth, maxHeight);
    clip->addChild(m_imageBackdrop, -1);

    auto* border = NineSlice::create("GJ_square07.png");
    m_imageBorder = border;
    if (border) {
        border->setContentSize({maxWidth + 3.f, maxHeight + 3.f});
        border->setPosition({image.getMidX(), image.getMidY()});
        m_mainLayer->addChild(border, 2);
    }

    m_sidebarBg = paimon::ui::makeInset(actions.size, 45);
    m_sidebarBg->setPosition(actions.origin);
    m_mainLayer->addChild(m_sidebarBg);

    auto* navigation = CCMenu::create();
    navigation->setPosition({0.f, 0.f});
    m_mainLayer->addChild(navigation, 10);
    auto addArrow = [&](bool next) {
        auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png");
        if (!sprite) return;
        sprite->setScale(0.4f);
        sprite->setFlipX(next);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, next
            ? menu_selector(LocalThumbnailViewPopup::onNext)
            : menu_selector(LocalThumbnailViewPopup::onPrev));
        button->setPosition({next ? image.getMaxX() - 14.f : image.getMinX() + 14.f,
            image.getMidY()});
        button->setVisible(false);
        navigation->addChild(button);
        if (next) m_rightArrow = button;
        else m_leftArrow = button;
    };
    addArrow(false);
    addArrow(true);

    m_infoBar = tv::InfoBar::create(content.width - 156.f);
    if (m_infoBar) {
        m_infoBar->setPosition({28.f + m_infoBar->getContentWidth() / 2.f,
            content.height - 20.f});
        m_mainLayer->addChild(m_infoBar, 12);
        m_infoBar->setLevelId(m_levelID);
    }

    buildControls(openedFromReport);

    this->setTouchEnabled(true);

#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
    this->setKeypadEnabled(true);
#endif

    if (verificationCategory >= 0) {
        this->loadFromVerificationQueue(static_cast<PendingCategory>(verificationCategory), maxWidth, maxHeight, content, openedFromReport);
    } else {
        this->tryLoadFromMultipleSources(maxWidth, maxHeight, content, openedFromReport);

        WeakRef<LocalThumbnailViewPopup> self = this;
        ThumbnailAPI::get().getThumbnails(m_levelID, [self](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
            auto popup = self.lock();
            // cached results can resolve before attach.
            if (!popup || popup->m_isExiting || !popup->m_mainLayer) return;

            if (!success || thumbs.empty()) {
                return;
            }

            popup->replaceRemoteThumbnails(thumbs);
            popup->loadThumbnailAt(popup->m_currentIndex);
            popup->setupRating();
        });
    }

    setupRating();
}



void LocalThumbnailViewPopup::loadFromVerificationQueue(PendingCategory category, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    if (category == PendingCategory::Verify && !m_suggestions.empty()) {
        m_currentIndex = 0;
        loadCurrentSuggestion();
        return;
    }

    Ref<LocalThumbnailViewPopup> safeRef = this;
    ThumbnailAPI::DownloadCallback onDownloaded =
        [safeRef, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
            if (!safeRef->getParent() || !safeRef->m_mainLayer) return;
            if (success && tex) {
                safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
            } else {
                safeRef->showNoThumbnail(content);
            }
        };

    switch (category) {
        case PendingCategory::Verify:
            ThumbnailAPI::get().downloadSuggestion(m_levelID, onDownloaded);
            break;
        case PendingCategory::Update:
            ThumbnailAPI::get().downloadUpdate(m_levelID, onDownloaded);
            break;
        case PendingCategory::Report:
            ThumbnailAPI::get().downloadReported(m_levelID, onDownloaded);
            break;
        default:
            log::error("[ThumbnailViewPopup] Unknown verification category: {}",
                static_cast<int>(category));
            showNoThumbnail(content);
    }
}

void LocalThumbnailViewPopup::tryLoadFromMultipleSources(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    m_localThumbPaths = LocalThumbs::get().getAllThumbPaths(m_levelID);
    int localCount = static_cast<int>(m_localThumbPaths.size());

    if (localCount > 0) {
        m_viewingLocal = true;
        m_localCurrentIndex = localCount - 1;

        auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
        if (tex) {
            this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);

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
        log::warn("[ThumbnailViewPopup] Failed to load local thumbnail {} for level {}",
            m_localCurrentIndex, m_levelID);
    }

    // local png/jpg fallback; videos use the gallery api to avoid a flash.
    auto localPath = LocalThumbs::get().findAnyThumbnail(m_levelID);
    if (localPath) {
        auto lowerPath = geode::utils::string::toLower(*localPath);
        if (!lowerPath.ends_with(".mp4")) {
            auto tex = LocalThumbs::get().loadTexture(m_levelID);
            if (tex) {
                this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                return;
            }
        }
    }

    m_viewingLocal = false;

    // url-keyed ram cache shared with levelcell.
    {
        auto& cache = paimon::cache::ThumbnailCache::get();
        std::string mainUrl = ThumbnailAPI::get().getThumbnailURL(m_levelID);
        auto urlTex = cache.getUrlFromRam(mainUrl);
        if (urlTex.has_value() && urlTex.value()) {
            this->displayThumbnail(urlTex.value(), maxWidth, maxHeight, content, openedFromReport);
            return;
        }
    }

    loadFromThumbnailLoader(maxWidth, maxHeight, content, openedFromReport);
}

bool LocalThumbnailViewPopup::tryLoadFromCache(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    // disk-cache path lives in thumbnailloader.
    auto ramTex = paimon::cache::ThumbnailCache::get().getFromRam(m_levelID, false);
    if (!ramTex.has_value()) {
        ramTex = paimon::cache::ThumbnailCache::get().getFromRam(m_levelID, true);
    }
    if (ramTex.has_value() && ramTex.value()) {
        this->displayThumbnail(ramTex.value(), maxWidth, maxHeight, content, openedFromReport);
        return true;
    }
    return false;
}

void LocalThumbnailViewPopup::loadFromThumbnailLoader(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    std::string fileName = fmt::format("{}.png", m_levelID);

    Ref<LocalThumbnailViewPopup> safeRef = this;

    ThumbnailLoader::get().requestLoad(m_levelID, fileName, [safeRef, maxWidth, maxHeight, content, openedFromReport](CCTexture2D* tex, bool) {
        if (!safeRef->isUiAlive()) {
            return;
        }

        if (tex) {
            safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
        } else {
            safeRef->tryDirectServerDownload(maxWidth, maxHeight, content, openedFromReport);
        }
    }, 10, false, ThumbnailLoader::Quality::High);
}

void LocalThumbnailViewPopup::tryDirectServerDownload(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
    Ref<LocalThumbnailViewPopup> safeRef = this;

    HttpClient::DownloadCallback cb = [safeRef, maxWidth, maxHeight, content, openedFromReport](bool success, std::vector<uint8_t> const& data, int, int) {
        if (!safeRef->getParent() || !safeRef->m_mainLayer) {
            return;
        }

        if (success && !data.empty()) {
            // mp4 sniff: ftyp box at offset 4.
            bool isMp4 = data.size() >= 8 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p';
            if (isMp4) {
                std::string cacheKey = fmt::format("direct_video_{}", safeRef->m_levelID);
                auto* videoSprite = VideoThumbnailSprite::createFromData(
                    std::vector<uint8_t>(data.begin(), data.end()), cacheKey);
                if (videoSprite) {
                    safeRef->displayVideoThumbnail(videoSprite, maxWidth, maxHeight, content);
                    return;
                }
                log::warn("[ThumbnailViewPopup] Failed to create video thumbnail for level {}",
                    safeRef->m_levelID);
            } else {
                auto image = new CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                    auto tex = new CCTexture2D();
                    if (tex->initWithImage(image)) {
                        safeRef->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                        tex->release();
                        image->release();
                        return;
                    }
                    tex->release();
                }
                image->release();
                log::error("[ThumbnailViewPopup] Failed to decode thumbnail for level {}",
                    safeRef->m_levelID);
            }
        }

        safeRef->showNoThumbnail(content);
    };
    HttpClient::get().downloadThumbnail(m_levelID, std::move(cb));
}



void LocalThumbnailViewPopup::displayVideoThumbnail(VideoThumbnailSprite* videoSprite, float maxWidth, float maxHeight, CCSize content) {
    if (!m_mainLayer || !videoSprite) {
        return;
    }

    clearGalleryDisplay();
    if (m_clippingNode) {
        auto const area = m_clippingNode->getContentSize();
        maxWidth = area.width;
        maxHeight = area.height;
    }

    // hidden until first frame: the stretched 1x1 placeholder glitches.
    m_videoPlaying = false;
    videoSprite->setVisible(false);
    videoSprite->setOpacity(0);
    videoSprite->setAnchorPoint({0.5f, 0.5f});
    videoSprite->setID("thumbnail"_spr);
    videoSprite->setLoop(true);
    videoSprite->setVolume(0.0f);

    m_viewWidth = maxWidth;
    m_viewHeight = maxHeight;

    float centerX = paimon::thumbviewer::imageArea(content).getMidX();
    float centerY = paimon::thumbviewer::imageArea(content).getMidY();

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

    // muted play kickstarts decode; first-frame callback pauses and scales.
    videoSprite->play();

    Ref<LocalThumbnailViewPopup> safeRef = this;
    videoSprite->setOnFirstVisibleFrame([safeRef, maxWidth, maxHeight, centerX, centerY](VideoThumbnailSprite* readySprite) {
        if (!safeRef->isUiAlive()) {
            return;
        }
        if (safeRef->m_thumbnailSprite != readySprite || !readySprite->getParent()) {
            return;
        }

        safeRef->m_thumbnailTexture = readySprite->getTexture();

        float cw = readySprite->getContentWidth();
        float ch = readySprite->getContentHeight();
        if (cw < 1.f) cw = 1.f;
        if (ch < 1.f) ch = 1.f;

        float scaleX = maxWidth / cw;
        float scaleY = maxHeight / ch;
        float scale = std::min(scaleX, scaleY);

        readySprite->setScale(scale);
        safeRef->m_initialScale = scale;
        safeRef->m_minScale = scale;
        safeRef->m_maxScale = std::max(4.0f, scale * 6.0f);
        safeRef->m_savedScale = scale;

        readySprite->setVisible(true);
        readySprite->setOpacity(255);

        // first frame in: pause so the play overlay works.
        if (!safeRef->m_videoPlaying) {
            readySprite->pause();
        }

        if (safeRef->m_playBtn) {
            safeRef->m_playBtn->setPosition({centerX, centerY});
        }
        safeRef->m_hasFocusImagePose = false;
        safeRef->applyFocusLayout();
    });

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
        auto btnSpr = ButtonSprite::create(">", 32, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.7f);
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
    updateRefreshButtonState();
    applyFocusLayout();
}

void LocalThumbnailViewPopup::displayThumbnail(CCTexture2D* tex, float maxWidth, float maxHeight, CCSize content, bool /*openedFromReport*/) {
    if (!tex || !m_mainLayer) {
        return;
    }

    if (m_clippingNode) {
        auto const area = m_clippingNode->getContentSize();
        maxWidth = area.width;
        maxHeight = area.height;
    }
    m_hasFocusImagePose = false;
    CCNode* oldSprite = m_thumbnailSprite;
    m_thumbnailSprite = nullptr;
    m_thumbnailTexture = nullptr;

    m_videoPlaying = false;
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }

    if (auto node = m_mainLayer->getChildByID("nothumb-container"_spr)) {
        node->removeFromParent();
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
         AnimatedGIFSprite::createAsync(paimon::paths::toUtf8(path), [safeRef, maxWidth, maxHeight](AnimatedGIFSprite* anim) {
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
                     float scale = std::min(scaleX, scaleY);
                     anim->setScale(scale);
                     anim->setPosition(pos);

                     parent->addChild(anim, 10);
                     safeRef->m_thumbnailSprite = anim;
                     safeRef->m_hasFocusImagePose = false;
                     safeRef->applyFocusLayout();
                 }
             }
         });
    }

    if (!sprite) {
        log::error("[ThumbnailViewPopup] Failed to create thumbnail sprite for level {}",
            m_levelID);
        return;
    }

    sprite->setAnchorPoint({0.5f, 0.5f});

    m_viewWidth = maxWidth;
    m_viewHeight = maxHeight;

    float scaleX = maxWidth / sprite->getContentWidth();
    float scaleY = maxHeight / sprite->getContentHeight();
    float scale = std::min(scaleX, scaleY);

    sprite->setScale(scale);
    m_initialScale = scale;
    m_minScale = scale;
    m_maxScale = std::max(4.0f, scale * 6.0f);
    m_savedScale = scale;

    float centerX = paimon::thumbviewer::imageArea(content).getMidX();
    float centerY = paimon::thumbviewer::imageArea(content).getMidY();
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

    if (m_focusTransition.progress() > 0.f || m_focusTransition.running()) {
        if (oldSprite) oldSprite->removeFromParent();
        applyFocusLayout();
        sprite->setOpacity(0);
        sprite->runAction(CCFadeTo::create(0.18f, 255));
    } else {
        applyPopupTransition(sprite, oldSprite, maxWidth);
    }
    updateRefreshButtonState();
    updateOrderUiState();
    syncInfoBar();
    if (m_focusTransition.progress() > 0.f || m_focusTransition.running()) applyFocusLayout();
}

void LocalThumbnailViewPopup::buildControls(bool openedFromReport) {
    namespace tv = paimon::thumbviewer;
    auto const content = m_mainLayer->getContentSize();
    auto const actions = tv::actionArea(content);
    auto& localization = Localization::get();

    m_settingsMenu = CCMenu::create();
    m_settingsMenu->setPosition({0.f, 0.f});
    m_settingsMenu->setID("thumbviewer-header-menu"_spr);
    m_mainLayer->addChild(m_settingsMenu, 15);

    auto addHeaderButton = [&](char const* frame, int slot, SEL_MenuHandler callback,
                               std::string const& id) {
        auto* button = tv::makeToolButton(frame, 0.6f, nullptr, this, callback);
        if (!button) return button;
        button->setPosition({content.width - 112.f + slot * 22.f, content.height - 20.f});
        button->setID(id);
        m_settingsMenu->addChild(button);
        m_headerButtonPoses.push_back({button, button->getPosition()});
        return button;
    };

    if (m_verificationCategory < 0) {
        m_refreshBtn = addHeaderButton("GJ_updateBtn_001.png", 0,
            menu_selector(LocalThumbnailViewPopup::onRefreshBtn), "refresh-btn"_spr);
        if (!m_refreshBtn) {
            m_refreshBtn = addHeaderButton("GJ_replayBtn_001.png", 0,
                menu_selector(LocalThumbnailViewPopup::onRefreshBtn), "refresh-btn"_spr);
        }
    }
    addHeaderButton("GJ_resetBtn_001.png", 1,
        menu_selector(LocalThumbnailViewPopup::onFitReset), "fit-reset-btn"_spr);
    m_hideChromeBtn = addHeaderButton("GJ_zoomInBtn_001.png", 2,
        menu_selector(LocalThumbnailViewPopup::toggleChrome), "hide-chrome-btn"_spr);
    addHeaderButton("GJ_optionsBtn_001.png", 3,
        menu_selector(LocalThumbnailViewPopup::onSettings), "settings-gear-btn"_spr);
    addHeaderButton("GJ_infoIcon_001.png", 4,
        menu_selector(LocalThumbnailViewPopup::onInfo), "thumbnail-info-btn"_spr);

    m_buttonMenu = CCMenu::create();
    m_buttonMenu->ignoreAnchorPointForPosition(false);
    m_buttonMenu->setAnchorPoint({0.5f, 0.5f});
    m_buttonMenu->setContentSize({tv::kToolWidth, actions.size.height - 8.f});
    m_buttonMenu->setPosition({actions.getMidX(), actions.getMidY()});
    m_buttonMenu->setID("thumbviewer-actions-menu"_spr);
    m_buttonMenu->setLayout(ColumnLayout::create()
        ->setGap(4.f)->setAxisReverse(true)->setAxisAlignment(AxisAlignment::End)
        ->setCrossAxisAlignment(AxisAlignment::Center)->setGrowCrossAxis(false)
        ->setAutoScale(true)->setDefaultScaleLimits(0.65f, 1.f));
    m_mainLayer->addChild(m_buttonMenu, 10);

    auto addAction = [&](char const* frame, char const* caption, SEL_MenuHandler callback,
                         std::string const& id, char const* background = "GJ_button_01.png") {
        if (auto* button = tv::makeToolButton(frame, 0.7f, caption, this, callback,
                {255, 255, 255}, background)) {
            button->setID(id);
            m_buttonMenu->addChild(button);
        }
    };
    addAction("GJ_starBtn_001.png", localization.getString("viewer.rate").c_str(),
        menu_selector(LocalThumbnailViewPopup::onRate), "rate-btn"_spr);

    auto* downloadSprite = Assets::loadButtonSprite("popup-download",
        "frame:GJ_downloadBtn_001.png", []() {
            if (auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_downloadBtn_001.png")) return sprite;
            return paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        });
    if (auto* button = tv::makeToolButton(downloadSprite, 0.7f,
            localization.getString("level.download_button").c_str(), this,
            menu_selector(LocalThumbnailViewPopup::onDownloadBtn),
            {255, 255, 255}, "GJ_button_02.png")) {
        button->setID("download-btn"_spr);
        m_buttonMenu->addChild(button);
    }
    addAction("GJ_copyBtn_001.png", localization.getString("viewer.copy_id").c_str(),
        menu_selector(LocalThumbnailViewPopup::onCopyLevelId), "copy-id-btn"_spr, "GJ_button_02.png");
    addAction("gj_ytIcon_001.png", "YouTube",
        menu_selector(LocalThumbnailViewPopup::onYouTubeBtn), "youtube-btn"_spr);

    m_buttonMenu->addChild(tv::makeToolDivider(tv::kToolWidth));
    auto addModerationAction = [&](char const* key, paimon::ui::Btn skin,
                                    SEL_MenuHandler callback) {
        auto* sprite = paimon::ui::makeButtonSprite(localization.getString(key).c_str(),
            skin, tv::kToolWidth, 0.6f, "goldFont.fnt");
        if (sprite) {
            m_buttonMenu->addChild(CCMenuItemSpriteExtra::create(sprite, this, callback));
        }
    };
    if (m_verificationCategory >= 0 && m_verificationCategory != 2) {
        addModerationAction("level.accept_button", paimon::ui::Btn::Green,
            menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
        addModerationAction("level.reject_button", paimon::ui::Btn::Red,
            menu_selector(LocalThumbnailViewPopup::onRejectThumbBtn));
    } else if (openedFromReport) {
        addModerationAction("level.delete_button", paimon::ui::Btn::Red,
            menu_selector(LocalThumbnailViewPopup::onDeleteReportedThumb));
    } else {
        addModerationAction("level.report_button", paimon::ui::Btn::Red,
            menu_selector(LocalThumbnailViewPopup::onReportBtn));
    }
    if (m_canAcceptUpload && m_verificationCategory < 0) {
        addModerationAction("level.accept_button", paimon::ui::Btn::Green,
            menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
    }
    m_buttonMenu->updateLayout();
    updateRefreshButtonState();

    if (auto* gm = GameManager::get()) {
        int const accountID = GJAccountManager::get() ? GJAccountManager::get()->m_accountID : 0;
        WeakRef<LocalThumbnailViewPopup> self = this;
        ThumbnailAPI::get().checkModeratorAccount(gm->m_playerName, accountID,
            [self](bool isMod, bool isAdmin) {
                auto popup = self.lock();
                if (!popup || popup->m_isExiting || !popup->m_buttonMenu) return;
                popup->m_isAdmin = isAdmin;
                if (isMod || isAdmin) {
                    if (auto* button = paimon::thumbviewer::makeToolButton("GJ_deleteBtn_001.png",
                            0.6f, Localization::get().getString("viewer.delete").c_str(), popup,
                            menu_selector(LocalThumbnailViewPopup::onDeleteThumbnail),
                            {255, 255, 255}, "GJ_button_06.png")) {
                        button->setID("mod-delete-btn"_spr);
                        popup->m_buttonMenu->addChild(button);
                    }
                }
                popup->ensureOrderControls(popup->m_mainLayer->getContentWidth());
                popup->updateOrderUiState();
                popup->m_buttonMenu->updateLayout();
            });
    }
}

void LocalThumbnailViewPopup::clearGalleryDisplay() {
    if (!m_mainLayer) return;

    if (m_thumbnailSprite) {
        if (auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite)) {
            videoSprite->stop();
        }
        m_thumbnailSprite->removeFromParent();
        m_thumbnailSprite = nullptr;
    }
    m_thumbnailTexture = nullptr;
    m_hasFocusImagePose = false;

    // clip node survives; only its sprites swap.
    if (m_clippingNode) {
        m_clippingNode->removeAllChildren();
        // restore the clip background.
        auto content = m_clippingNode->getContentSize();
        m_imageBackdrop = CCLayerColor::create({0, 0, 0, 255}, content.width, content.height);
        m_clippingNode->addChild(m_imageBackdrop, -1);
    }

    if (auto node = m_mainLayer->getChildByID("nothumb-container"_spr)) {
        node->removeFromParent();
    }

    m_videoPlaying = false;
    if (m_playBtnMenu) {
        m_playBtnMenu->removeFromParent();
        m_playBtnMenu = nullptr;
        m_playBtn = nullptr;
    }
}

void LocalThumbnailViewPopup::applyPopupTransition(CCNode* newNode, CCNode* oldNode, float maxWidth) {
    if (!newNode) return;

    auto* newSpr = static_cast<CCSprite*>(newNode);

    std::string style = Mod::get()->getSavedValue<std::string>("popup-gallery-transition", "directional-elastic");
    float dur = Mod::get()->getSavedValue<float>("popup-gallery-transition-duration", 0.45f);

    if (!oldNode || !oldNode->getParent()) {
        if (oldNode) oldNode->removeFromParent();

        newSpr->setOpacity(0);
        float sx = newNode->getScaleX();
        float sy = newNode->getScaleY();
        newNode->setScaleX(sx * 0.92f);
        newNode->setScaleY(sy * 0.92f);
        
        newNode->runAction(CCSpawn::create(
            CCEaseBackOut::create(CCScaleTo::create(dur * 1.2f, sx, sy)),
            CCFadeTo::create(dur, 255),
            nullptr
        ));
        
        m_navDirection = NavDirection::None;
        return;
    }

    CCPoint targetPos = newNode->getPosition();
    float sx = newNode->getScaleX();
    float sy = newNode->getScaleY();

    bool goRight = (m_navDirection == NavDirection::Right);
    bool goLeft = (m_navDirection == NavDirection::Left);
    bool slideFromRight = goRight || (!goLeft);

    if (style == "directional-elastic") {
        {
            float oldTargetX = goLeft ? (targetPos.x + maxWidth) : (targetPos.x - maxWidth);
            oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseBackIn::create(CCMoveTo::create(dur * 0.6f, {oldTargetX, targetPos.y})),
                    CCFadeTo::create(dur * 0.5f, 0),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
                nullptr));
        }
        float startX = goLeft ? (targetPos.x - maxWidth) : (targetPos.x + maxWidth);
        newNode->setPosition({startX, targetPos.y});
        newSpr->setOpacity(255);
        newNode->runAction(CCEaseElasticOut::create(
            CCMoveTo::create(dur * 1.1f, targetPos), 0.35f));

    } else if (style == "elastic-slide") {
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

    } else if (style == "wave-slide") {
        float startX = slideFromRight ? (targetPos.x + maxWidth) : (targetPos.x - maxWidth);
        float startRot = slideFromRight ? 12.f : -12.f;
        
        newNode->setPosition({startX, targetPos.y});
        newNode->setScaleX(sx * 0.8f);
        newNode->setScaleY(sy * 0.8f);
        newNode->setRotation(startRot);
        newSpr->setOpacity(0);
        
        newNode->runAction(CCSpawn::create(
            CCEaseElasticOut::create(CCMoveTo::create(dur * 1.2f, targetPos), 0.45f),
            CCEaseBackOut::create(CCRotateTo::create(dur * 1.0f, 0.f)),
            CCEaseOut::create(CCScaleTo::create(dur * 0.8f, sx, sy), 2.0f),
            CCFadeTo::create(dur * 0.6f, 255),
            nullptr
        ));
        
        float oldTargetX = slideFromRight ? (targetPos.x - maxWidth) : (targetPos.x + maxWidth);
        float oldTargetRot = slideFromRight ? -12.f : 12.f;
        oldNode->runAction(CCSequence::create(
            CCSpawn::create(
                CCEaseIn::create(CCMoveTo::create(dur * 0.8f, {oldTargetX, targetPos.y}), 2.0f),
                CCEaseIn::create(CCRotateTo::create(dur * 0.8f, oldTargetRot), 2.0f),
                CCEaseIn::create(CCScaleTo::create(dur * 0.8f, sx * 0.8f, sy * 0.8f), 2.0f),
                CCFadeTo::create(dur * 0.6f, 0),
                nullptr
            ),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));

    } else if (style == "card-flip") {
        newNode->setScaleX(0.01f);
        newSpr->setOpacity(0);
        
        newNode->runAction(CCSequence::create(
            CCDelayTime::create(dur * 0.35f),
            CCSpawn::create(
                CCEaseElasticOut::create(CCScaleTo::create(dur * 0.85f, sx, sy), 0.5f),
                CCFadeTo::create(dur * 0.4f, 255),
                nullptr
            ),
            nullptr
        ));
        
        oldNode->runAction(CCSequence::create(
            CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur * 0.45f, 0.01f, sy), 2.0f),
                CCFadeTo::create(dur * 0.4f, 0),
                nullptr
            ),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));

    } else if (style == "spin-zoom") {
        newNode->setScaleX(0.01f);
        newNode->setScaleY(0.01f);
        newNode->setRotation(slideFromRight ? -180.f : 180.f);
        newSpr->setOpacity(0);
        
        newNode->runAction(CCSpawn::create(
            CCEaseElasticOut::create(CCScaleTo::create(dur * 1.2f, sx, sy), 0.4f),
            CCEaseElasticOut::create(CCRotateTo::create(dur * 1.2f, 0.f), 0.4f),
            CCFadeTo::create(dur * 0.6f, 255),
            nullptr
        ));
        
        oldNode->runAction(CCSequence::create(
            CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur * 0.7f, 0.01f, 0.01f), 2.0f),
                CCEaseIn::create(CCRotateBy::create(dur * 0.7f, slideFromRight ? 180.f : -180.f), 2.0f),
                CCFadeTo::create(dur * 0.6f, 0),
                nullptr
            ),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));

    } else if (style == "dissolve") {
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
        newSpr->setOpacity(0);
        newSpr->runAction(CCFadeTo::create(dur, 255));
        oldNode->runAction(CCSequence::create(
            CCFadeTo::create(dur * 0.85f, 0),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)),
            nullptr));
    }

    m_navDirection = NavDirection::None;
}

void LocalThumbnailViewPopup::showNoThumbnail(CCSize content) {
    clearGalleryDisplay();

    float centerX = paimon::thumbviewer::imageArea(content).getMidX();
    float centerY = paimon::thumbviewer::imageArea(content).getMidY();
    float bgWidth = paimon::thumbviewer::imageArea(content).size.width;
    float bgHeight = paimon::thumbviewer::imageArea(content).size.height;

    auto container = CCNode::create();
    container->setContentSize(content);
    container->setPosition({0, 0});
    container->setID("nothumb-container"_spr);

    auto bg = paimon::ui::makeInset({bgWidth, bgHeight}, 200);
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
    updateRefreshButtonState();
}


void LocalThumbnailViewPopup::onPlayVideo(CCObject*) {
    auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite);
    if (!videoSprite) return;

    if (m_videoPlaying) {
        m_videoPlaying = false;
        videoSprite->pause();
    } else {
        m_videoPlaying = true;
        videoSprite->setVolume(1.0f);
        videoSprite->play();
    }

    updatePlayButton();
}

void LocalThumbnailViewPopup::updatePlayButton() {
    if (!m_playBtn) return;

    if (m_videoPlaying) {
        m_playBtn->stopAllActions();
        m_playBtn->runAction(CCSequence::create(
            CCFadeTo::create(0.3f, 0),
            CCHide::create(),
            nullptr
        ));
    } else {
        m_playBtn->stopAllActions();
        m_playBtn->setVisible(true);
        m_playBtn->setOpacity(0);
        m_playBtn->runAction(CCFadeTo::create(0.3f, 255));
    }
}

void LocalThumbnailViewPopup::onYouTubeBtn(CCObject*) {
    // query the user-overridable ytlinks endpoint.
    std::string serverUrl;
    if (auto* mod = Mod::get()) {
        serverUrl = mod->getSavedValue<std::string>("paimon-emote-server-url", "");
    }
    if (serverUrl.empty()) {
        serverUrl = "https://paimbnailsbot.onrender.com";
    }
    while (!serverUrl.empty() && serverUrl.back() == '/') {
        serverUrl.pop_back();
    }
    std::string url = fmt::format("{}/api/ytlinks?id={}", serverUrl, m_levelID);

    auto req = geode::utils::web::WebRequest();
    req.timeout(asp::Duration::fromSecs(10));
    req.acceptEncoding("gzip, deflate");
    req.header("Accept", "application/json");

    WeakRef<LocalThumbnailViewPopup> self = this;

    // keep the request handle alive until completion.
    WebHelper::dispatchOwned(m_ytRequestHolder, std::move(req), "GET", url, [self, levelID = m_levelID](geode::utils::web::WebResponse res) {
        auto popup = self.lock();
        if (!popup || !popup->getParent()) {
            geode::log::warn("[YTLinks] Popup gone before response for level {}", levelID);
            return;
        }

        if (res.code() == 404) {
            PaimonNotify::show("No se registro un link de YouTube para este nivel", geode::NotificationIcon::Info);
            return;
        }

        if (!res.ok()) {
            geode::log::warn("[YTLinks] HTTP {} for level {}", res.code(), levelID);
            PaimonNotify::show("No se registro un link de YouTube para este nivel", geode::NotificationIcon::Info);
            return;
        }

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

    ThumbnailLoader::get().invalidateLevel(m_levelID, false);
    ThumbnailLoader::get().invalidateLevel(m_levelID, true);
    ThumbnailTransportClient::get().invalidateGalleryMetadata(m_levelID);

    clearGalleryDisplay();
    m_thumbnails.clear();
    m_currentIndex = 0;
    ++m_galleryRequestToken;
    if (m_leftArrow) m_leftArrow->setVisible(false);
    if (m_rightArrow) m_rightArrow->setVisible(false);
    if (m_counterLabel) m_counterLabel->setVisible(false);
    updateOrderUiState();

    auto content = m_mainLayer->getContentSize();
    auto spinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    spinner->show(m_mainLayer, 5);

    float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
    float maxHeight = paimon::thumbviewer::imageArea(content).size.height;

    WeakRef<LocalThumbnailViewPopup> self = this;
    int requestToken = m_galleryRequestToken;

    ThumbnailAPI::get().getThumbnails(m_levelID, [self, content, maxWidth, maxHeight, requestToken](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
        auto popup = self.lock();
        if (!popup || popup->m_isExiting || !popup->m_mainLayer) return;
        if (requestToken != popup->m_galleryRequestToken) return;

        if (auto sp = popup->m_mainLayer->getChildByID("paimon-loading-overlay"_spr)) {
            if (auto overlay = typeinfo_cast<PaimonLoadingOverlay*>(sp)) {
                overlay->dismiss();
            }
        }

        popup->m_isDownloading = false;
        popup->updateRefreshButtonState();

        if (success && !thumbs.empty()) {
            popup->replaceRemoteThumbnails(thumbs);
            popup->loadThumbnailAt(0);
            popup->setupRating();
            PaimonNotify::show("Miniaturas actualizadas", geode::NotificationIcon::Success);
        } else {
            popup->loadFromThumbnailLoader(maxWidth, maxHeight, content, false);
            PaimonNotify::show("No se encontraron miniaturas en la galeria", geode::NotificationIcon::Info);
        }
    });
}

void LocalThumbnailViewPopup::onOrderEdit(CCObject*) {
    if (!m_isAdmin || m_verificationCategory >= 0 || m_viewingLocal || m_thumbnails.size() < 2) return;

    std::string selectedId;
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_thumbnails.size())) {
        selectedId = m_thumbnails[m_currentIndex].id;
    }

    auto orderPopup = ThumbnailOrderPopup::create(m_levelID, m_thumbnails, selectedId);
    if (!orderPopup) {
        PaimonNotify::show("No se pudo abrir el editor de orden", geode::NotificationIcon::Error);
        return;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    orderPopup->setOnSaved([self](std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs, std::string const& selectedThumbId) {
        auto popup = self.lock();
        if (!popup || !popup->isUiAlive()) return;

        popup->replaceRemoteThumbnails(thumbs, selectedThumbId);

        if (!popup->m_thumbnails.empty()) {
            popup->loadThumbnailAt(popup->m_currentIndex);
            popup->setupRating();
        }
    });
    orderPopup->show();
}

void LocalThumbnailViewPopup::onDownloadBtn(CCObject*) {
    if (m_isDownloading) return;
    m_isDownloading = true;
    updateRefreshButtonState();

    Ref<CCTexture2D> selectedTexture = m_thumbnailTexture;
    if (auto* sprite = typeinfo_cast<CCSprite*>(m_thumbnailSprite)) {
        selectedTexture = sprite->isVisible() ? sprite->getTexture() : nullptr;
    }
    std::optional<std::string> selectedLocalPath;
    if (m_viewingLocal && m_localCurrentIndex >= 0
        && m_localCurrentIndex < static_cast<int>(m_localThumbPaths.size())) {
        selectedLocalPath = m_localThumbPaths[m_localCurrentIndex];
    }
    bool const allowLevelCache = m_thumbnails.empty() && m_suggestions.empty();

    Ref<LocalThumbnailViewPopup> safeRef = this;
    int levelID = m_levelID;

    WeakRef<LocalThumbnailViewPopup> weakRef = this;

    auto notifyResult = [weakRef](bool ok, std::filesystem::path const& savePath) {
        auto popup = weakRef.lock();
        if (!popup) return;
        popup->m_isDownloading = false;
        popup->updateRefreshButtonState();
        if (ok) {
            log::info("Image saved successfully to {}", paimon::paths::toUtf8(savePath));
            PaimonNotify::show(Localization::get().getString("level.saved").c_str(), NotificationIcon::Success);
        } else {
            log::error("Failed to save image to {}", paimon::paths::toUtf8(savePath));
            PaimonNotify::show(Localization::get().getString("level.save_error").c_str(), NotificationIcon::Error);
        }
    };

    auto doSave = [safeRef, weakRef, levelID, notifyResult, selectedTexture,
                   selectedLocalPath, allowLevelCache](std::filesystem::path savePath) {
        if (!safeRef->isUiAlive()) return;
        log::debug("Save path chosen: {}", paimon::paths::toUtf8(savePath));

    // prefer existing thumb/cache files.
        std::optional<std::string> pathStr = selectedLocalPath;
        if (!pathStr && allowLevelCache) pathStr = LocalThumbs::get().findAnyThumbnail(levelID);
        bool fromCache = false;
        if (!pathStr && allowLevelCache) {
            auto cachePng = ThumbnailLoader::get().getCachePath(levelID, false);
            auto cacheGif = ThumbnailLoader::get().getCachePath(levelID, true);
            std::error_code ec;
            if (std::filesystem::exists(cachePng, ec)) {
                pathStr = paimon::paths::toUtf8(cachePng);
                fromCache = true;
            } else if (std::filesystem::exists(cacheGif, ec)) {
                pathStr = paimon::paths::toUtf8(cacheGif);
                fromCache = true;
            }
        } else if (pathStr) {
            std::filesystem::path p(*pathStr);
            fromCache = (p.extension() != ".rgb");
        }

        if (pathStr) {
            std::string srcPath = *pathStr;
            std::filesystem::path srcFs(srcPath);
            bool isRgb = (srcFs.extension() == ".rgb");

            paimon::ThreadTracker::get().spawn([weakRef, srcPath, savePath, isRgb, fromCache, notifyResult]() {
                geode::utils::thread::setName("PaimonThumbExport");
                if (paimon::isRuntimeShuttingDown()) return;
                bool ok = false;
                if (isRgb) {
                    std::vector<uint8_t> rgbData;
                    uint32_t width = 0, height = 0;
                    if (ImageConverter::loadRgbFile(srcPath, rgbData, width, height)) {
                        auto rgba = ImageConverter::rgbToRgba(rgbData, width, height);
                        ok = ImageConverter::saveRGBAToPNG(rgba.data(), width, height, savePath);
                    }
                    if (paimon::isRuntimeShuttingDown()) return;
                    Loader::get()->queueInMainThread([weakRef, ok, savePath, notifyResult]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto popup = weakRef.lock();
                        if (!popup || !popup->getParent()) return;
                        notifyResult(ok, savePath);
                    });
                    return;
                }
                if (fromCache) {
                    std::filesystem::path srcP(srcPath);
                    std::string ext = paimon::paths::toUtf8(srcP.extension());
                    if (ext.empty()) ext = ".png";
                    std::filesystem::path destPath = savePath.parent_path() / (paimon::paths::toUtf8(savePath.stem()) + ext);
                    std::error_code ec;
                    std::filesystem::copy(srcP, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                    ok = !ec;
                    if (paimon::isRuntimeShuttingDown()) return;
                    Loader::get()->queueInMainThread([weakRef, ok, destPath, notifyResult]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        auto popup = weakRef.lock();
                        if (!popup || !popup->getParent()) return;
                        notifyResult(ok, destPath);
                    });
                    return;
                }
                if (paimon::isRuntimeShuttingDown()) return;
                Loader::get()->queueInMainThread([weakRef, savePath, notifyResult]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto popup = weakRef.lock();
                    if (!popup || !popup->getParent()) return;
                    notifyResult(false, savePath);
                });
            });
            return;
        }

        // read the selected texture directly: popup matrices and clipping must
        // never affect the downloaded image, even if the picker stays open.
        auto pixels = std::make_shared<paimon::thumbviewer::TexturePixels>(
            paimon::thumbviewer::readTexturePixels(selectedTexture.data()));
        if (*pixels) {
            paimon::ThreadTracker::get().spawn([weakRef, pixels, savePath, notifyResult]() {
                geode::utils::thread::setName("PaimonThumbSave");
                if (paimon::isRuntimeShuttingDown()) return;
                bool const ok = ImageConverter::saveRGBAToPNG(pixels->rgba.data(),
                    pixels->width, pixels->height, savePath);
                if (paimon::isRuntimeShuttingDown()) return;
                Loader::get()->queueInMainThread([weakRef, ok, savePath, notifyResult]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto popup = weakRef.lock();
                    if (!popup || !popup->getParent()) return;
                    notifyResult(ok, savePath);
                });
            });
            return;
        }

        log::error("Thumbnail path not found and no texture to save");
        notifyResult(false, savePath);
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
        // picker canceled or unsupported: save in the mod folder.
        auto saveDir = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "saved_thumbnails";
        std::error_code ec;
        if (!std::filesystem::exists(saveDir, ec)) {
            std::filesystem::create_directories(saveDir, ec);
        }
        auto savePath = saveDir / fmt::format("thumb_{}.png", levelID);
        PaimonNotify::show(Localization::get().getString("level.saving_mod_folder").c_str(), NotificationIcon::Info);
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
        PaimonNotify::show(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error);
        return;
    }

    WeakRef<LocalThumbnailViewPopup> self = this;
    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, levelID, username, accountID, loading](bool isMod, bool isAdmin) {
        auto popup = self.lock();
        if (!popup) return;
        if (!isMod && !isAdmin) {
            if (loading) loading->dismiss();
            PaimonNotify::show(Localization::get().getString("level.delete_moderator_only").c_str(), NotificationIcon::Error);
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
                PaimonNotify::show(Localization::get().getString("level.deleted_server").c_str(), NotificationIcon::Success);
                log::info("[ThumbnailViewPopup] Miniatura {} eliminada del servidor", levelID);

                // invalidate cache and refresh the gallery.
                ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                ThumbnailLoader::get().invalidateLevel(levelID);

                auto popup = self.lock();
                if (popup && popup->isUiAlive()) {
                    ThumbnailAPI::get().getThumbnails(levelID, [self](bool ok, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                        auto popup = self.lock();
                        if (!popup || !popup->isUiAlive()) return;
                        if (!ok || thumbs.empty()) {
                            popup->m_thumbnails.clear();
                            popup->m_currentIndex = 0;
                            popup->showNoThumbnail(popup->m_mainLayer->getContentSize());
                            if (popup->m_leftArrow) popup->m_leftArrow->setVisible(false);
                            if (popup->m_rightArrow) popup->m_rightArrow->setVisible(false);
                            if (popup->m_counterLabel) popup->m_counterLabel->setVisible(false);
                            popup->updateOrderUiState();
                            return;
                        }
                        popup->replaceRemoteThumbnails(thumbs);
                        popup->loadThumbnailAt(popup->m_currentIndex);
                    });
                }
            } else {
                PaimonNotify::show(Localization::get().getString("level.delete_error") + msg, NotificationIcon::Error);
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

        PaimonNotify::show(Localization::get().getString("level.accepting").c_str(), NotificationIcon::Info);

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
                    PaimonNotify::show(Localization::get().getString("level.accepted").c_str(), NotificationIcon::Success);
                    log::info("[ThumbnailViewPopup] Miniatura aceptada para nivel {}", levelID);

                    // invalidate cache so the gallery reloads on return.
                    ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                    ThumbnailLoader::get().invalidateLevel(levelID);
                } else {
                    PaimonNotify::show(Localization::get().getString("level.accept_error") + message, NotificationIcon::Error);
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
        PaimonNotify::show(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error);
        return;
    }

    std::vector<uint8_t> pngData;
    if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
        PaimonNotify::show(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error);
        return;
    }

    size_t base64Size = ((pngData.size() + 2) / 3) * 4;
    log::info("PNG size: {} bytes ({:.2f} KB), Base64 size: ~{} bytes ({:.2f} KB)",
             pngData.size(), pngData.size() / 1024.0, base64Size, base64Size / 1024.0);

    log::warn("[ThumbnailViewPopup] Server upload disabled - thumbnail saved locally only");
    PaimonNotify::show(Localization::get().getString("level.saved_local_server_disabled").c_str(), NotificationIcon::Info);
}

void LocalThumbnailViewPopup::onRejectThumbBtn(CCObject*) {
    log::info("Rechazar thumbnail presionado en ThumbnailViewPopup para levelID={}", m_levelID);

    if (m_verificationCategory < 0) {
        PaimonNotify::show(Localization::get().getString("level.reject_error").c_str(), NotificationIcon::Error);
        return;
    }

    std::string username;
    auto* gm = GameManager::get();
    if (gm) {
        username = gm->m_playerName;
    } else {
        log::warn("[ThumbnailViewPopup] GameManager::get() es null");
    }

    PaimonNotify::show(Localization::get().getString("level.rejecting").c_str(), NotificationIcon::Info);

    ThumbnailAPI::get().rejectQueueItem(
        m_levelID,
        static_cast<PendingCategory>(m_verificationCategory),
        username,
        "Rechazado desde vista de miniatura",
        [levelID = m_levelID, category = m_verificationCategory](bool success, std::string const& message) {
            if (success) {
                PendingQueue::get().reject(levelID, static_cast<PendingCategory>(category));
                PaimonNotify::show(Localization::get().getString("level.rejected").c_str(), NotificationIcon::Warning);
                log::info("[ThumbnailViewPopup] Miniatura rechazada para nivel {}", levelID);

                ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                ThumbnailLoader::get().invalidateLevel(levelID);
            } else {
                PaimonNotify::show(Localization::get().getString("level.reject_error") + message, NotificationIcon::Error);
                log::error("[ThumbnailViewPopup] Error rechazando miniatura: {}", message);
            }
        }
    );
}

void LocalThumbnailViewPopup::onReportBtn(CCObject*) {
    int levelID = m_levelID;

    auto popup = ReportInputPopup::create(levelID, [levelID](std::string reason) {
        std::string user;
        auto* gm = GameManager::get();
        if (gm) {
            user = gm->m_playerName;
        }

        ThumbnailAPI::get().submitReport(levelID, user, reason, [levelID, reason](bool success, std::string const&) {
            if (success) {
                PaimonNotify::show(Localization::get().getString("report.sent_synced") + reason, NotificationIcon::Warning);
                log::info("[ThumbnailViewPopup] Reporte confirmado y enviado al servidor para nivel {}", levelID);
            } else {
                PaimonNotify::show(Localization::get().getString("report.saved_local").c_str(), NotificationIcon::Info);
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
        PaimonNotify::show("Error al borrar miniatura", NotificationIcon::Error);
        return;
    }

    m_localThumbPaths = LocalThumbs::get().getAllThumbPaths(m_levelID);
    int newCount = static_cast<int>(m_localThumbPaths.size());

    if (newCount == 0) {
        m_viewingLocal = false;
        if (m_leftArrow) m_leftArrow->setVisible(false);
        if (m_rightArrow) m_rightArrow->setVisible(false);
        if (m_counterLabel) m_counterLabel->setVisible(false);
        auto content = m_mainLayer->getContentSize();
        showNoThumbnail(content);
        PaimonNotify::show("Miniatura borrada", NotificationIcon::Success);
        return;
    }

    if (m_localCurrentIndex >= newCount) {
        m_localCurrentIndex = newCount - 1;
    }

    auto tex = LocalThumbs::get().loadTextureByIndex(m_levelID, m_localCurrentIndex);
    if (tex) {
        auto content = m_mainLayer->getContentSize();
        float maxWidth = paimon::thumbviewer::imageArea(content).size.width;
        float maxHeight = paimon::thumbviewer::imageArea(content).size.height;
        displayThumbnail(tex, maxWidth, maxHeight, content, false);
    }

    if (m_leftArrow) m_leftArrow->setVisible(newCount > 1);
    if (m_rightArrow) m_rightArrow->setVisible(newCount > 1);
    if (m_counterLabel) {
        m_counterLabel->setVisible(newCount > 1);
        m_counterLabel->setString(fmt::format("{}/{}", m_localCurrentIndex + 1, newCount).c_str());
    }

    PaimonNotify::show("Miniatura borrada", NotificationIcon::Success);
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
    ThumbnailAPI::get().getRating(levelID, username, thumbnailId, [self, levelID, username, accountID](bool /*success*/, float /*avg*/, int count, int /*uservote*/) {
        auto popup = self.lock();
        if (!popup) return;

        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, levelID, username, accountID, count](bool isMod, bool isAdmin) {
            auto popup = self.lock();
            if (!popup) return;

            if (!isMod && !isAdmin) {
                 PaimonNotify::show(Localization::get().getString("level.no_permissions"), NotificationIcon::Error);
                 return;
            }

            if (count > 100 && !isAdmin) {
                PaimonNotify::show(Localization::get().getString("level.admin_only_high_votes"), NotificationIcon::Error);
                return;
            }

            PopupManager::get().quickPopup(
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
                                PaimonNotify::show(Localization::get().getString("level.thumbnail_deleted"), NotificationIcon::Success);

                                ThumbnailTransportClient::get().invalidateGalleryMetadata(levelID);
                                ThumbnailLoader::get().invalidateLevel(levelID);

                                ThumbnailAPI::get().getThumbnails(levelID, [self](bool ok, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                                    auto popup = self.lock();
                                    if (!popup || !popup->isUiAlive()) return;
                                    if (!ok || thumbs.empty()) {
                                        popup->m_thumbnails.clear();
                                        popup->m_currentIndex = 0;
                                        popup->showNoThumbnail(popup->m_mainLayer->getContentSize());
                                        if (popup->m_leftArrow) popup->m_leftArrow->setVisible(false);
                                        if (popup->m_rightArrow) popup->m_rightArrow->setVisible(false);
                                        if (popup->m_counterLabel) popup->m_counterLabel->setVisible(false);
                                        popup->updateOrderUiState();
                                        return;
                                    }
                                    popup->replaceRemoteThumbnails(thumbs);
                                    popup->loadThumbnailAt(popup->m_currentIndex);
                                });
                            } else {
                                PaimonNotify::show(msg.c_str(), NotificationIcon::Error);
                            }
                        });
                    }
                }
            ).showInstant();
        });
    });
}


void LocalThumbnailViewPopup::onRecenter(CCObject*) {
    if (!m_thumbnailSprite) return;

    m_thumbnailSprite->stopAllActions();
    resetZoomGestureState();

    // sprite usually lives in the clip node, so center in its parent space, not the main layer.
    CCPoint target;
    if (m_thumbnailSprite->getParent() == m_clippingNode && m_viewWidth > 0.f && m_viewHeight > 0.f) {
        target = ccp(m_viewWidth * 0.5f, m_viewHeight * 0.5f);
    } else {
        auto content = this->m_mainLayer->getContentSize();
        auto const image = paimon::thumbviewer::imageArea(content);
        target = ccp(image.getMidX(), image.getMidY());
    }

    auto moveTo = CCMoveTo::create(0.3f, target);
    auto scaleTo = CCScaleTo::create(0.3f, m_initialScale);
    auto easeMove = CCEaseSineOut::create(moveTo);
    auto easeScale = CCEaseSineOut::create(scaleTo);

    m_thumbnailSprite->runAction(easeMove);
    m_thumbnailSprite->runAction(easeScale);
    m_thumbnailSprite->setAnchorPoint({0.5f, 0.5f});
}

void LocalThumbnailViewPopup::onFitReset(CCObject* sender) {
    onRecenter(sender);
}

void LocalThumbnailViewPopup::onCopyLevelId(CCObject*) {
    geode::utils::clipboard::write(std::to_string(m_levelID));
    PaimonNotify::show(fmt::format("ID {} copiado", m_levelID), geode::NotificationIcon::Success);
}

void LocalThumbnailViewPopup::prepareFocusImagePose(bool preserveCurrent) {
    if (!m_thumbnailSprite || !m_mainLayer) return;
    auto const size = m_thumbnailSprite->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return;
    auto const content = m_mainLayer->getContentSize();
    auto const normal = paimon::thumbviewer::imageArea(content);
    auto const expanded = paimon::thumbviewer::focusedImageArea(content);
    float const normalFit = std::min(normal.size.width / size.width, normal.size.height / size.height);
    float const expandedFit = std::min(expanded.size.width / size.width, expanded.size.height / size.height);
    float const ratio = expandedFit / normalFit;
    CCPoint const normalCenter{normal.size.width / 2.f, normal.size.height / 2.f};
    m_collapsedImageScale = preserveCurrent ? m_thumbnailSprite->getScale() : normalFit;
    m_collapsedImagePosition = preserveCurrent ? m_thumbnailSprite->getPosition() : normalCenter;
    m_expandedImageScale = m_collapsedImageScale * ratio;
    m_expandedImagePosition = CCPoint{expanded.size.width / 2.f, expanded.size.height / 2.f}
        + (m_collapsedImagePosition - normalCenter) * ratio;
    m_hasFocusImagePose = true;
}

void LocalThumbnailViewPopup::applyFocusLayout() {
    if (!m_mainLayer || !m_clippingNode) return;
    namespace tv = paimon::thumbviewer;
    auto const content = m_mainLayer->getContentSize();
    auto const normal = tv::imageArea(content);
    auto const expanded = tv::focusedImageArea(content);
    float const progress = m_focusTransition.progress();
    auto mix = [progress](float from, float to) { return from + (to - from) * progress; };
    CCRect const area{mix(normal.origin.x, expanded.origin.x), mix(normal.origin.y, expanded.origin.y),
        mix(normal.size.width, expanded.size.width), mix(normal.size.height, expanded.size.height)};
    m_clippingNode->setContentSize(area.size);
    m_clippingNode->setPosition({area.getMidX(), area.getMidY()});
    m_viewWidth = area.size.width;
    m_viewHeight = area.size.height;
    if (m_imageStencil) {
        m_imageStencil->setScaleX(area.size.width / normal.size.width);
        m_imageStencil->setScaleY(area.size.height / normal.size.height);
    }
    if (m_imageBackdrop) m_imageBackdrop->setContentSize(area.size);
    if (m_imageBorder) {
        m_imageBorder->setContentSize({area.size.width + 3.f, area.size.height + 3.f});
        m_imageBorder->setPosition({area.getMidX(), area.getMidY()});
    }
    if (m_thumbnailSprite) {
        if (!m_hasFocusImagePose && (progress > 0.f || m_focusTransition.running())) prepareFocusImagePose(false);
        if (m_hasFocusImagePose) {
            m_thumbnailSprite->setScale(mix(m_collapsedImageScale, m_expandedImageScale));
            m_thumbnailSprite->setPosition({mix(m_collapsedImagePosition.x, m_expandedImagePosition.x),
                mix(m_collapsedImagePosition.y, m_expandedImagePosition.y)});
        }
        auto const size = m_thumbnailSprite->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            m_initialScale = std::min(area.size.width / size.width, area.size.height / size.height);
            m_minScale = m_initialScale;
            m_maxScale = std::max(4.f, m_initialScale * 6.f);
        }
    }
    if (m_playBtn) m_playBtn->setPosition({area.getMidX(), area.getMidY()});
    if (m_playBtnMenu) m_playBtnMenu->setEnabled(!m_focusTransition.running());

    float const alpha = std::clamp(1.f - progress * 1.35f, 0.f, 1.f);
    bool const shown = alpha > 0.f;
    bool const interactive = !m_chromeHidden && !m_focusTransition.running();
    auto const actions = tv::actionArea(content);
    if (m_buttonMenu) {
        m_buttonMenu->setPosition({actions.getMidX() + 12.f * progress, actions.getMidY()});
        m_buttonMenu->setVisible(shown);
        m_buttonMenu->setEnabled(interactive);
        tv::setChromeFade(m_buttonMenu, alpha);
    }
    if (m_sidebarBg) {
        m_sidebarBg->setPosition(actions.origin + CCPoint{12.f * progress, 0.f});
        m_sidebarBg->setVisible(shown);
        tv::setChromeFade(m_sidebarBg, alpha);
    }
    if (m_infoBar) {
        m_infoBar->setPosition({28.f + m_infoBar->getContentWidth() / 2.f,
            content.height - 20.f + 8.f * progress});
        m_infoBar->setVisible(shown);
        tv::setChromeFade(m_infoBar, alpha);
    }
    for (auto const& pose : m_headerButtonPoses) {
        auto button = pose.button.lock();
        if (!button) continue;
        if (button.data() == m_hideChromeBtn) {
            button->setPosition({mix(pose.position.x, content.width - 22.f),
                mix(pose.position.y, content.height - 20.f)});
            button->setVisible(true);
            button->setEnabled(true);
        } else {
            button->setPosition(pose.position + CCPoint{0.f, 8.f * progress});
            button->setVisible(shown);
            button->setEnabled(interactive && (button.data() != m_refreshBtn
                || (!m_refreshCooldownActive && !m_isDownloading)));
            tv::setChromeFade(button.data(), alpha);
        }
    }
    bool const multi = m_thumbnails.size() > 1 || m_suggestions.size() > 1 || m_localThumbPaths.size() > 1;
    for (auto* arrow : {m_leftArrow, m_rightArrow}) {
        if (!arrow) continue;
        arrow->setVisible(shown && multi);
        arrow->setEnabled(interactive);
        tv::setChromeFade(arrow, alpha);
    }
}

void LocalThumbnailViewPopup::setChromeVisible(bool visible) {
    if (m_chromeHidden == !visible) return;
    float const progress = m_focusTransition.progress();
    if (!visible && progress == 0.f) {
        if (m_thumbnailSprite) {
            m_thumbnailSprite->stopAllActions();
            clampSpritePosition();
            if (typeinfo_cast<CCRGBAProtocol*>(m_thumbnailSprite)) {
                m_thumbnailSprite->runAction(CCFadeTo::create(0.18f, 255));
            }
        }
        prepareFocusImagePose(true);
    } else if (visible && progress == 1.f && m_hasFocusImagePose && m_thumbnailSprite) {
        m_thumbnailSprite->stopAllActions();
        m_expandedImagePosition = m_thumbnailSprite->getPosition();
        m_expandedImageScale = m_thumbnailSprite->getScale();
    }
    m_chromeHidden = !visible;
    m_focusTransition.setFocused(!visible);
    m_touches.clear();
    resetZoomGestureState();
    applyFocusLayout();
    this->schedule(schedule_selector(LocalThumbnailViewPopup::tickFocus));
}

void LocalThumbnailViewPopup::tickFocus(float dt) {
    bool const running = m_focusTransition.advance(dt);
    applyFocusLayout();
    if (!running) {
        this->unschedule(schedule_selector(LocalThumbnailViewPopup::tickFocus));
        if (!m_chromeHidden) m_hasFocusImagePose = false;
        resetZoomGestureState();
        updateRefreshButtonState();
    }
}

void LocalThumbnailViewPopup::toggleChrome(CCObject*) {
    setChromeVisible(m_chromeHidden);
}

void LocalThumbnailViewPopup::keyDown(enumKeyCodes key, double timestamp) {
    if (m_focusTransition.running() && (key == KEY_Left || key == KEY_Right || key == KEY_R)) return;
    switch (key) {
        case KEY_Left:
            onPrev(nullptr);
            return;
        case KEY_Right:
            onNext(nullptr);
            return;
        case KEY_R:
            onFitReset(nullptr);
            return;
        default:
            break;
    }
    PaimonPopup::keyDown(key, timestamp);
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

    if (m_clippingNode && m_viewWidth > 0.f && m_viewHeight > 0.f) {
        auto const mousePos = geode::cocos::getMousePos();
        auto const local = m_clippingNode->convertToNodeSpace(mousePos);
        bool cursorInPopup = true;
        if (m_bgSprite) {
            auto const bgLocal = m_bgSprite->convertToNodeSpace(mousePos);
            auto const bgSize = m_bgSprite->getContentSize();
            cursorInPopup = (bgLocal.x >= 0.f && bgLocal.x <= bgSize.width &&
                             bgLocal.y >= 0.f && bgLocal.y <= bgSize.height);
        } else if (m_mainLayer) {
            auto const layerLocal = m_mainLayer->convertToNodeSpace(mousePos);
            auto const layerSize = m_mainLayer->getContentSize();
            cursorInPopup = (layerLocal.x >= 0.f && layerLocal.x <= layerSize.width &&
                             layerLocal.y >= 0.f && layerLocal.y <= layerSize.height);
        }

        if (cursorInPopup) {
            float const clampedX = clamp(local.x, 0.f, m_viewWidth);
            float const clampedY = clamp(local.y, 0.f, m_viewHeight);
            return m_clippingNode->convertToWorldSpace({clampedX, clampedY});
        }

        return m_clippingNode->convertToWorldSpace({m_viewWidth * 0.5f, m_viewHeight * 0.5f});
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

    auto findTouchedItem = [](CCMenu* menu, CCTouch* touch) -> CCMenuItem* {
        if (!menu || !menu->isVisible() || !menu->isEnabled()) return nullptr;
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

    CCMenuItem* hit = nullptr;
    if (!hit) hit = findTouchedItem(m_playBtnMenu, touch);
    if (!hit) hit = findTouchedItem(m_buttonMenu, touch);
    if (!hit) hit = findTouchedItem(m_ratingMenu, touch);
    if (!hit) hit = findTouchedItem(m_settingsMenu, touch);
    if (!hit && m_leftArrow) {
        hit = findTouchedItem(static_cast<CCMenu*>(m_leftArrow->getParent()), touch);
    }

    if (hit) {
        hit->selected();
        m_activatedItem = hit;
        return true;
    }

    if (m_closeBtn && m_closeBtn->ccTouchBegan(touch, event)) {
        m_activatedCloseBtn = true;
        return true;
    }

    if (m_focusTransition.running()) return true;

    // tap on the video area toggles playback.
    if (m_videoPlaying && m_thumbnailSprite) {
        auto* videoSprite = geode::cast::typeinfo_cast<VideoThumbnailSprite*>(m_thumbnailSprite);
        if (videoSprite) {
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

    if (m_clippingNode) {
        auto const point = m_clippingNode->convertTouchToNodeSpace(touch);
        auto const size = m_clippingNode->getContentSize();
        if (point.x < 0.f || point.y < 0.f || point.x > size.width || point.y > size.height) {
            return true;
        }
    }

    if (m_bgSprite) {
        auto local = m_bgSprite->convertTouchToNodeSpace(touch);
        auto size = m_bgSprite->getContentSize();
        if (local.x < 0 || local.y < 0 || local.x > size.width || local.y > size.height) {
            return false;
        }
    }

    if (m_touches.size() == 1) {
        auto firstTouch = *m_touches.begin();
        if (firstTouch == touch) return true;
    }

    m_touches.insert(touch);
    syncZoomGestureState();
    return true;
}

void LocalThumbnailViewPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (m_activatedCloseBtn) {
        if (m_closeBtn) m_closeBtn->ccTouchMoved(touch, event);
        return;
    }
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

    if (m_focusTransition.running() || !m_thumbnailSprite) return;

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
    if (m_activatedCloseBtn) {
        m_activatedCloseBtn = false;
        if (m_closeBtn) m_closeBtn->ccTouchEnded(touch, event);
        return;
    }
    if (m_activatedItem) {
        m_activatedItem->unselected();
        m_activatedItem->activate();
        m_activatedItem = nullptr;
        return;
    }

    m_touches.erase(touch);

    if (m_focusTransition.running() || !m_thumbnailSprite) return;

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

bool LocalThumbnailViewPopup::scrollWheel(float x, float y) {
    if (m_focusTransition.running()) return true;
    if (!m_mainLayer || !m_thumbnailSprite) {
        return true;
    }

    if (!m_thumbnailSprite->getParent()) {
        m_thumbnailSprite = nullptr;
        return true;
    }

    float const steps = paimon::smoothscroll::SmoothScrollController::get()
        .filteredZoomSteps(x, y);
    if (std::abs(steps) < 0.000001f) return true;

    float const baseFactor = steps > 0.f ? 1.12f : 0.89f;
    float const zoomFactor = std::pow(baseFactor, std::abs(steps));

    float currentScale = m_thumbnailSprite->getScale();
    float newScale = currentScale * zoomFactor;

    newScale = clamp(newScale, m_minScale, m_maxScale);

    if (std::abs(newScale - currentScale) < 0.001f) {
        return true;
    }

    m_wasZooming = true;
    applyZoomAtWorldPoint(newScale, getZoomFocusPoint());
    syncZoomGestureState();
    return true;
}

void LocalThumbnailViewPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (m_activatedCloseBtn) {
        m_activatedCloseBtn = false;
        if (m_closeBtn) m_closeBtn->ccTouchCancelled(touch, event);
    }
    if (m_activatedItem) {
        m_activatedItem->unselected();
        m_activatedItem = nullptr;
    }
    m_touches.erase(touch);
    resetZoomGestureState();
}


LocalThumbnailViewPopup* LocalThumbnailViewPopup::create(int32_t levelID, bool canAcceptUpload) {
    auto ret = new LocalThumbnailViewPopup();
    if (ret && ret->init(paimon::thumbviewer::kPopupWidth, paimon::thumbviewer::kPopupHeight)) {
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

// onsettings() lives in levelinfolayer.cpp because it needs the $modify type.
