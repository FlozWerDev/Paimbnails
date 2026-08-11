#include "MenuMusicAddPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicCopy.hpp"
#include "../services/NewgroundsCatalog.hpp"
#include "../services/YtDlpDownloader.hpp"
#include "../services/YtDlpBootstrap.hpp"
#include "../services/FfmpegBootstrap.hpp"
#include "YtDlpInstallPopup.hpp"
#include "FfmpegInstallPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

MenuMusicAddPopup* MenuMusicAddPopup::create(std::string initialUrl) {
    auto ret = new MenuMusicAddPopup();
    ret->m_initialUrl = std::move(initialUrl);
    if (ret && ret->init(420.f, 320.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuMusicAddPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Add Music");

    MenuMusicLibrary::get().load();

    buildUrlSection();
    buildLocalSection();
    buildProgressBar();

    auto size = m_mainLayer->getContentSize();
    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.4f);
        m_statusLabel->setPosition({size.width / 2.f, size.height * 0.05f});
        m_statusLabel->setColor({255, 220, 120});
        m_statusLabel->setID("status-label"_spr);
        m_mainLayer->addChild(m_statusLabel, 4);
    }

    refreshStatus();
    return true;
}

void MenuMusicAddPopup::onExit() {
    m_alive = false;
    Popup::onExit();
}

void MenuMusicAddPopup::buildUrlSection() {
    auto size = m_mainLayer->getContentSize();

    auto header = CCLabelBMFont::create("Download from a link", "goldFont.fnt");
    if (header) {
        header->setScale(0.5f);
        header->setPosition({size.width / 2.f, size.height * 0.88f});
        header->setID("url-header"_spr);
        m_mainLayer->addChild(header, 3);
    }

    m_ytDlpLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_ytDlpLabel) {
        m_ytDlpLabel->setScale(0.4f);
        m_ytDlpLabel->setPosition({size.width / 2.f, size.height * 0.82f});
        m_ytDlpLabel->setColor({180, 220, 180});
        m_ytDlpLabel->setID("ytdlp-status"_spr);
        m_mainLayer->addChild(m_ytDlpLabel, 3);
    }

    m_urlInput = TextInput::create(size.width * 0.6f, "Paste a YouTube/SoundCloud link");
    if (m_urlInput) {
        m_urlInput->setCommonFilter(geode::CommonFilter::Any);
// Preserve URL punctuation that some Geode builds omit from setCommonFilter.
        if (auto* inner = m_urlInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }
        m_urlInput->setMaxCharCount(2048);
        m_urlInput->setPosition({size.width * 0.38f, size.height * 0.73f});
        m_urlInput->setID("url-input"_spr);
        if (!m_initialUrl.empty()) m_urlInput->setString(m_initialUrl);
        m_mainLayer->addChild(m_urlInput, 3);
    }

    {
        auto pasteSpr = CCSprite::createWithSpriteFrameName("GJ_pasteBtn2_001.png");
        if (pasteSpr) {
            auto btn = CCMenuItemSpriteExtra::create(pasteSpr, this,
                menu_selector(MenuMusicAddPopup::onPasteUrl));
            auto menu = CCMenu::create();
            menu->setPosition({size.width * 0.75f, size.height * 0.73f});
            menu->addChild(btn);
            menu->setID("paste-menu"_spr);
            m_mainLayer->addChild(menu, 3);
        }
    }

    {
        auto dlSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
        if (dlSpr) {
            auto btn = CCMenuItemSpriteExtra::create(dlSpr, this,
                menu_selector(MenuMusicAddPopup::onStartDownload));
            auto menu = CCMenu::create();
            menu->setPosition({size.width * 0.9f, size.height * 0.73f});
            menu->addChild(btn);
            menu->setID("dl-menu"_spr);
            m_mainLayer->addChild(menu, 3);
        }
    }

    auto helpSpr = CCSprite::createWithSpriteFrameName("GJ_infoBtn_001.png");
    if (helpSpr) {
        helpSpr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(helpSpr, this,
            menu_selector(MenuMusicAddPopup::onOpenYtDlpHelp));
        auto menu = CCMenu::create();
        menu->setPosition({size.width - 16.f, size.height * 0.88f});
        menu->addChild(btn);
        menu->setID("help-menu"_spr);
        m_mainLayer->addChild(menu, 3);
    }
}

void MenuMusicAddPopup::buildLocalSection() {
    auto size = m_mainLayer->getContentSize();

    auto sep = CCLayerColor::create(ccc4(120, 140, 160, 150));
    if (sep) {
        sep->setContentSize({size.width - 40.f, 2.f});
        sep->setAnchorPoint({0.5f, 0.5f});
        sep->setPosition({size.width / 2.f, size.height * 0.62f});
        sep->ignoreAnchorPointForPosition(false);
        m_mainLayer->addChild(sep, 2);
    }

    auto header = CCLabelBMFont::create("Or import a file from your PC", "goldFont.fnt");
    if (header) {
        header->setScale(0.5f);
        header->setPosition({size.width / 2.f, size.height * 0.56f});
        header->setID("local-header"_spr);
        m_mainLayer->addChild(header, 3);
    }

    auto audioSpr = ButtonSprite::create("Audio", 90, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.6f);
    if (audioSpr) {
        auto b = CCMenuItemSpriteExtra::create(audioSpr, this,
            menu_selector(MenuMusicAddPopup::onPickAudio));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.25f, size.height * 0.47f});
        menu->addChild(b);
        m_mainLayer->addChild(menu, 3);
    }
    m_audioPathLabel = CCLabelBMFont::create("No audio selected", "chatFont.fnt");
    if (m_audioPathLabel) {
        m_audioPathLabel->setScale(0.38f);
        m_audioPathLabel->setAnchorPoint({0.f, 0.5f});
        m_audioPathLabel->setPosition({size.width * 0.42f, size.height * 0.47f});
        m_audioPathLabel->setColor({220, 220, 220});
        m_mainLayer->addChild(m_audioPathLabel, 3);
    }

    auto coverSpr = ButtonSprite::create("Cover", 90, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.6f);
    if (coverSpr) {
        auto b = CCMenuItemSpriteExtra::create(coverSpr, this,
            menu_selector(MenuMusicAddPopup::onPickCover));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.25f, size.height * 0.37f});
        menu->addChild(b);
        m_mainLayer->addChild(menu, 3);
    }
    m_coverPathLabel = CCLabelBMFont::create("No cover (optional)", "chatFont.fnt");
    if (m_coverPathLabel) {
        m_coverPathLabel->setScale(0.38f);
        m_coverPathLabel->setAnchorPoint({0.f, 0.5f});
        m_coverPathLabel->setPosition({size.width * 0.42f, size.height * 0.37f});
        m_coverPathLabel->setColor({220, 220, 220});
        m_mainLayer->addChild(m_coverPathLabel, 3);
    }

    m_nameInput = TextInput::create(size.width * 0.78f, "Display name (optional)");
    if (m_nameInput) {
        m_nameInput->setCommonFilter(geode::CommonFilter::Any);
        if (auto* inner = m_nameInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }
        m_nameInput->setMaxCharCount(120);
        m_nameInput->setPosition({size.width / 2.f, size.height * 0.27f});
        m_nameInput->setID("name-input"_spr);
        m_mainLayer->addChild(m_nameInput, 3);
    }

    auto importSpr = ButtonSprite::create("Import", 120, true, "bigFont.fnt", "GJ_button_05.png", 28.f, 0.65f);
    if (importSpr) {
        auto b = CCMenuItemSpriteExtra::create(importSpr, this,
            menu_selector(MenuMusicAddPopup::onImportLocal));
        auto menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height * 0.14f});
        menu->addChild(b);
        m_mainLayer->addChild(menu, 3);
    }
}

void MenuMusicAddPopup::buildProgressBar() {
    auto size = m_mainLayer->getContentSize();

// Place the progress row between the URL input and local-file separator; hide
// it until a download starts.
    const float barW = size.width - 60.f;
    const float barH = 12.f;
    const float barY = size.height * 0.66f;
    const float cx   = size.width / 2.f;

    auto bg = CCLayerColor::create(ccc4(20, 20, 30, 230));
    if (bg) {
        bg->setContentSize({barW, barH});
        bg->ignoreAnchorPointForPosition(false);
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition({cx, barY});
        bg->setID("download-bar-bg"_spr);
        bg->setVisible(false);
        m_mainLayer->addChild(bg, 4);
        m_progressBarBg = bg;

        auto fill = CCLayerColor::create(ccc4(120, 220, 255, 255));
        if (fill) {
            fill->setContentSize({0.f, barH});
            fill->setAnchorPoint({0.f, 0.f});
            fill->ignoreAnchorPointForPosition(false);
            fill->setPosition({0.f, 0.f});
            fill->setID("download-bar-fill"_spr);
            bg->addChild(fill, 1);
            m_progressBarFill = fill;
        }
    }

    auto pct = CCLabelBMFont::create("0%", "bigFont.fnt");
    if (pct) {
        pct->setScale(0.35f);
        pct->setAnchorPoint({0.5f, 0.5f});
        pct->setPosition({cx, barY});
        pct->setColor({255, 255, 255});
        pct->setID("download-bar-percent"_spr);
        pct->setVisible(false);
        m_mainLayer->addChild(pct, 5);
        m_progressPercentLabel = pct;
    }
}

void MenuMusicAddPopup::setProgressBarVisible(bool visible) {
    if (m_progressBarBg) m_progressBarBg->setVisible(visible);
    if (m_progressPercentLabel) m_progressPercentLabel->setVisible(visible);
    if (visible) {
// Reset progress for each download.
        updateProgressBar(0.f);
    }
}

void MenuMusicAddPopup::updateProgressBar(float ratio01) {
    if (ratio01 < 0.f) ratio01 = 0.f;
    if (ratio01 > 1.f) ratio01 = 1.f;

    if (m_progressBarBg && m_progressBarFill) {
        const float barW = m_progressBarBg->getContentSize().width;
        const float barH = m_progressBarFill->getContentSize().height;
        m_progressBarFill->setContentSize({barW * ratio01, barH});
    }
    if (m_progressPercentLabel) {
        m_progressPercentLabel->setString(
            fmt::format("{:.0f}%", ratio01 * 100.f).c_str());
    }
}

void MenuMusicAddPopup::refreshStatus() {
    if (!m_ytDlpLabel) return;
    auto& boot = YtDlpBootstrap::get();
    if (boot.exists()) {
        m_ytDlpLabel->setString("Ready - paste a link and press the download button");
        m_ytDlpLabel->setColor({180, 230, 180});
    } else {
        m_ytDlpLabel->setString("The first download installs a small helper (~17 MB, one time)");
        m_ytDlpLabel->setColor({255, 220, 150});
    }
}

void MenuMusicAddPopup::onPickAudio(CCObject*) {
    auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
    pt::pickAudio([weakThis](Result<std::optional<std::filesystem::path>> res) {
        auto ref = weakThis.lock();
        if (!ref) return;
        auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
        if (!self || !self->m_alive.load()) return;
        if (!res) return;
        auto v = res.unwrap();
        if (!v.has_value()) return;
        self->m_pendingAudioPath = geode::utils::string::pathToString(v.value());
        if (self->m_audioPathLabel) {
            self->m_audioPathLabel->setString(geode::utils::string::pathToString(v.value().filename()).c_str());
        }
    });
}

void MenuMusicAddPopup::onPickCover(CCObject*) {
    auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
    pt::pickImage([weakThis](Result<std::optional<std::filesystem::path>> res) {
        auto ref = weakThis.lock();
        if (!ref) return;
        auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
        if (!self || !self->m_alive.load()) return;
        if (!res) return;
        auto v = res.unwrap();
        if (!v.has_value()) return;
        self->m_pendingCoverPath = geode::utils::string::pathToString(v.value());
        if (self->m_coverPathLabel) {
            self->m_coverPathLabel->setString(geode::utils::string::pathToString(v.value().filename()).c_str());
        }
    });
}

void MenuMusicAddPopup::onImportLocal(CCObject*) {
    finalizeLocalImport();
}

void MenuMusicAddPopup::finalizeLocalImport() {
    if (m_pendingAudioPath.empty()) {
        Notification::create("Select an audio file first.", NotificationIcon::Warning)->show();
        return;
    }
    auto audioP = std::filesystem::path(m_pendingAudioPath);
    if (!MenuMusicLibrary::isAudioExtension(audioP)) {
        Notification::create("Selected file is not a supported audio format.",
            NotificationIcon::Error)->show();
        return;
    }

    auto& lib = MenuMusicLibrary::get();
    auto id = lib.generateId("local");
    auto ext = geode::utils::string::pathToString(audioP.extension());
    auto destAudio = lib.getTracksDir() / (id + ext);
    std::error_code ec;
    std::filesystem::create_directories(destAudio.parent_path(), ec);
    std::filesystem::copy_file(audioP, destAudio,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        Notification::create("Failed to copy audio file.", NotificationIcon::Error)->show();
        return;
    }

    std::string destCoverStr;
    if (!m_pendingCoverPath.empty()) {
        auto coverP = std::filesystem::path(m_pendingCoverPath);
        if (MenuMusicLibrary::isImageExtension(coverP)) {
            auto destCover = lib.getCoversDir() / (id + geode::utils::string::pathToString(coverP.extension()));
            std::filesystem::copy_file(coverP, destCover,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) destCoverStr = geode::utils::string::pathToString(destCover);
        }
    }

    std::string name = m_nameInput ? std::string(m_nameInput->getString()) : std::string();
    if (name.empty()) name = geode::utils::string::pathToString(audioP.stem());

    MusicTrack track;
    track.id = id;
    track.audioPath = geode::utils::string::pathToString(destAudio);
    track.coverPath = destCoverStr;
    track.displayName = name;
    track.source = TrackSource::Local;
    track.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    lib.addTrack(track);

    m_pendingAudioPath.clear();
    m_pendingCoverPath.clear();
    if (m_audioPathLabel) m_audioPathLabel->setString("No audio selected");
    if (m_coverPathLabel) m_coverPathLabel->setString("No cover (optional)");
    if (m_nameInput) m_nameInput->setString("");

    Notification::create("Track imported!", NotificationIcon::Success)->show();
}

void MenuMusicAddPopup::onStartDownload(CCObject*) {
    if (!m_urlInput) return;
    auto url = m_urlInput->getString();
    if (url.empty()) {
        Notification::create("Enter a URL.", NotificationIcon::Warning)->show();
        return;
    }
    if (m_busy) {
        Notification::create("Download already in progress...", NotificationIcon::Info)->show();
        return;
    }

// Newgrounds links/IDs use GD's downloader, then register the resulting MP3.
    if (auto songId = parseNewgroundsSongId(url); songId > 0) {
        m_busy = true;
        if (m_statusLabel) {
            m_statusLabel->setString(
                fmt::format("Looking up song #{} on GD's servers...", songId).c_str());
            m_statusLabel->setColor({255, 220, 120});
        }

        auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
        fetchNewgroundsSongInfo(songId,
            [weakThis, songId](NewgroundsSongResult info) {
                auto ref = weakThis.lock();
                auto* self = ref
                    ? typeinfo_cast<MenuMusicAddPopup*>(ref.data())
                    : nullptr;
                if (!self || !self->m_alive.load()) return;

                if (!info.success) {
                    self->m_busy = false;
                    if (self->m_statusLabel) {
                        self->m_statusLabel->setString(info.error.c_str());
                        self->m_statusLabel->setColor({255, 130, 130});
                    }
                    Notification::create(info.error, NotificationIcon::Error, 4.f)->show();
                    return;
                }

                if (self->m_statusLabel) {
                    self->m_statusLabel->setString(fmt::format(
                        "Downloading {} by {}...",
                        info.track.title, info.track.artist).c_str());
                    self->m_statusLabel->setColor({255, 220, 120});
                }

                downloadNewgroundsSong(songId,
                    [weakThis, songId](NewgroundsDownloadResult result) {
                        auto ref = weakThis.lock();
                        auto* self = ref
                            ? typeinfo_cast<MenuMusicAddPopup*>(ref.data())
                            : nullptr;
                        if (!self || !self->m_alive.load()) {
// The service already registered the track.
                            return;
                        }

                        self->m_busy = false;
                        if (!result.success) {
                            if (self->m_statusLabel) {
                                self->m_statusLabel->setString(result.error.c_str());
                                self->m_statusLabel->setColor({255, 130, 130});
                            }
                            Notification::create(result.error, NotificationIcon::Error, 4.f)->show();
                            return;
                        }

                        if (self->m_statusLabel) {
                            self->m_statusLabel->setString(fmt::format(
                                "Saved as {}.mp3 - added to your library!", songId).c_str());
                            self->m_statusLabel->setColor({140, 230, 140});
                        }
                        if (self->m_urlInput) self->m_urlInput->setString("");
                        Notification::create("Track downloaded!", NotificationIcon::Success)->show();
                    });
            });
        return;
    }

    auto& bootstrap = YtDlpBootstrap::get();
    if (!bootstrap.exists()) {
        if (m_statusLabel) {
            m_statusLabel->setString("yt-dlp is not installed.");
            m_statusLabel->setColor({255, 200, 140});
        }

        PopupManager::get().quickPopup(
            "yt-dlp Required",
            "To download music from a URL, the <cy>yt-dlp</c> binary is needed.\n"
            "<cl>~17 MB, one-time download.</c>\n\n"
            "Do you want to install it now?",
            "Cancel", "Install",
            [this](FLAlertLayer*, bool accepted) {
                if (!m_alive.load()) return;
                if (!accepted) {
                    if (m_statusLabel) {
                        m_statusLabel->setString("Install cancelled.");
                        m_statusLabel->setColor({200, 200, 200});
                    }
                    refreshStatus();
                    return;
                }

// m_alive and WeakRef guard UI access during install.
                auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
                auto installPopup = YtDlpInstallPopup::create(
                    [weakThis](bool ok) {
                        auto ref = weakThis.lock();
                        auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
                        if (!self || !self->m_alive.load()) return;
                        if (!ok) {
                            if (self->m_statusLabel) {
                                self->m_statusLabel->setString("yt-dlp install failed or cancelled.");
                                self->m_statusLabel->setColor({255, 130, 130});
                            }
                            self->refreshStatus();
                            return;
                        }
                        self->refreshStatus();
                        self->onStartDownload(nullptr);
                    }
                );
                if (installPopup) installPopup->show();
            }
        ).showInstant();
        return;
    }

// ffmpeg converts AAC/Opus to MP3 for FMOD.
    auto& ffmpeg = FfmpegBootstrap::get();
    if (!ffmpeg.exists()) {
        if (m_statusLabel) {
            m_statusLabel->setString("ffmpeg is not installed.");
            m_statusLabel->setColor({255, 200, 140});
        }

        PopupManager::get().quickPopup(
            "ffmpeg Required",
            "Geometry Dash's audio engine can only play <cy>MP3</c> from YouTube-style sources.\n"
            "We use <cy>ffmpeg</c> to convert downloaded audio to MP3.\n"
            "<cl>~80 MB, one-time download.</c>\n\n"
            "Do you want to install it now?",
            "Cancel", "Install",
            [this](FLAlertLayer*, bool accepted) {
                if (!m_alive.load()) return;
                if (!accepted) {
                    if (m_statusLabel) {
                        m_statusLabel->setString("Install cancelled.");
                        m_statusLabel->setColor({200, 200, 200});
                    }
                    refreshStatus();
                    return;
                }

                auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
                auto installPopup = FfmpegInstallPopup::create(
                    [weakThis](bool ok) {
                        auto ref = weakThis.lock();
                        auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
                        if (!self || !self->m_alive.load()) return;
                        if (!ok) {
                            if (self->m_statusLabel) {
                                self->m_statusLabel->setString("ffmpeg install failed or cancelled.");
                                self->m_statusLabel->setColor({255, 130, 130});
                            }
                            self->refreshStatus();
                            return;
                        }
                        self->refreshStatus();
                        self->onStartDownload(nullptr);
                    }
                );
                if (installPopup) installPopup->show();
            }
        ).showInstant();
        return;
    }

    auto& dl = YtDlpDownloader::get();
    if (!dl.isAvailable()) {
        Notification::create(
            "yt-dlp binary missing. Try clicking Download again.",
            NotificationIcon::Error, 4.f)->show();
        return;
    }

    m_busy = true;
    if (m_statusLabel) {
        m_statusLabel->setString("Starting download...");
        m_statusLabel->setColor({255, 220, 120});
    }
// Show progress once work starts.
    setProgressBarVisible(true);

    auto id = MenuMusicLibrary::get().generateId("dl");

    auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
    dl.download(url, id,
        [weakThis](YtDlpProgress p) {
            auto ref = weakThis.lock();
            auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
            if (!self || !self->m_alive.load()) return;
            if (p.stage == "downloading") {
                self->updateProgressBar(p.percent);
                if (self->m_statusLabel) {
                    self->m_statusLabel->setString(
                        fmt::format("Downloading... {:.0f}%", p.percent * 100.f).c_str());
                }
            } else if (!p.message.empty()) {
                if (self->m_statusLabel) {
                    self->m_statusLabel->setString(p.message.substr(0, 64).c_str());
                }
            }
        },
        [weakThis, url](YtDlpResult result) {
            auto ref = weakThis.lock();
            auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
            if (!self || !self->m_alive.load()) {
// Register the track even if the popup closed.
                if (result.success) {
                    MusicTrack t;
                    t.id = result.trackId;
                    t.audioPath = result.audioPath;
                    t.coverPath = result.coverPath;
                    t.displayName = result.displayName;
                    t.artist = result.artist;
                    t.sourceUrl = url;
                    t.source = TrackSource::Downloaded;
                    t.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    MenuMusicLibrary::get().addTrack(t);
                }
                return;
            }

            self->m_busy = false;

            if (!result.success && result.error == "__NEED_YTDLP__") {
                self->setProgressBarVisible(false);
                self->onStartDownload(nullptr);
                return;
            }
            if (!result.success && result.error == "__NEED_FFMPEG__") {
                self->setProgressBarVisible(false);
                self->onStartDownload(nullptr);
                return;
            }

            if (!result.success) {
                self->setProgressBarVisible(false);
                if (self->m_statusLabel) {
                    self->m_statusLabel->setString(
                        fmt::format("Error: {}", result.error.substr(0, 100)).c_str());
                    self->m_statusLabel->setColor({255, 130, 130});
                }
                Notification::create(result.error.substr(0, 120),
                    NotificationIcon::Error, 5.f)->show();
                return;
            }

            MusicTrack t;
            t.id = result.trackId;
            t.audioPath = result.audioPath;
            t.coverPath = result.coverPath;
            t.displayName = result.displayName.empty()
                ? geode::utils::string::pathToString(std::filesystem::path(result.audioPath).stem())
                : result.displayName;
            t.artist = result.artist;
            t.sourceUrl = url;
            t.source = TrackSource::Downloaded;
            t.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            MenuMusicLibrary::get().addTrack(t);

// Show 100% briefly before hiding the bar.
            self->updateProgressBar(1.f);
            self->setProgressBarVisible(false);

            if (self->m_statusLabel) {
                self->m_statusLabel->setString("Download complete!");
                self->m_statusLabel->setColor({140, 230, 140});
            }
            if (self->m_urlInput) self->m_urlInput->setString("");
            Notification::create("Track downloaded!", NotificationIcon::Success)->show();
        }
    );
}

void MenuMusicAddPopup::onPasteUrl(CCObject*) {
    if (!m_urlInput) return;
// Read the clipboard directly to bypass the input filter.
    auto clip = geode::utils::clipboard::read();
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!clip.empty() && isSpace(static_cast<unsigned char>(clip.front()))) clip.erase(clip.begin());
    while (!clip.empty() && isSpace(static_cast<unsigned char>(clip.back()))) clip.pop_back();

    if (clip.empty()) {
        Notification::create("Clipboard is empty.", NotificationIcon::Warning)->show();
        return;
    }
    if (clip.size() > 2048) clip = clip.substr(0, 2048);
    m_urlInput->setString(clip);
    Notification::create("URL pasted from clipboard.", NotificationIcon::Success)->show();
}

void MenuMusicAddPopup::onOpenYtDlpHelp(CCObject*) {
    auto bundle = YtDlpBootstrap::get().bundledPath();
    auto bundleStr = geode::utils::string::pathToString(bundle);
    bool installed = YtDlpBootstrap::get().exists();

    std::string msg = installed
        ? fmt::format(
            "<cg>yt-dlp is installed</c> at:\n"
            "<cl>{}</c>\n\n"
            "The binary lives inside the mod's save data folder, so if you "
            "uninstall Paimbnails with <cy>'delete data'</c>, it is removed "
            "automatically.\n\n"
            "Paste any YouTube, SoundCloud, TikTok, Bandcamp, etc. URL and "
            "hit Download. Audio is kept in its native format "
            "(<cy>no ffmpeg required</c>).",
            bundleStr)
        : fmt::format(
            "<cy>yt-dlp will auto-install on first download</c>\n"
            "(~17MB, one-time) into the mod's save data folder:\n"
            "<cl>{}</c>\n\n"
            "Because it lives in the mod's data dir, uninstalling Paimbnails "
            "with <cy>'delete data'</c> will also remove the binary.\n\n"
            "You can also manually drop the official binary there if you "
            "cannot reach github.com.\n\n"
            "Source: <cb>https://github.com/yt-dlp/yt-dlp</c>",
            bundleStr);

    PopupManager::get().alert("yt-dlp setup", msg).showInstant();
}

}
