#include "../../../utils/FilesystemPath.hpp"
#include "MenuMusicAddPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicCopy.hpp"
#include "../services/NewgroundsCatalog.hpp"
#include "../services/YtDlpDownloader.hpp"
#include "../services/YtDlpBootstrap.hpp"
#include "../services/FfmpegBootstrap.hpp"
#include "../services/SpotifyService.hpp"
#include "ToolInstallPopupBase.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../ui/PaimonUI.hpp"

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
    if (!PaimonPopup::init(width, height)) return false;
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
        m_statusLabel->setColor(paimon::ui::palette::gold);
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

    const CCSize panelSize{size.width - 28.f, 114.f};
    const float panelY = size.height - 40.f - panelSize.height;
    m_urlPanel = paimon::ui::makePanel(panelSize, "Music Sources");
    m_urlPanel->setPosition({14.f, panelY});
    m_urlPanel->setID("url-panel"_spr);
    m_mainLayer->addChild(m_urlPanel, 2);

    const float innerTop = panelSize.height - paimon::ui::kPanelHeader;

    // source Selector Tabs Menu: [YouTube] [SoundCloud] [Spotify]
    auto tabsMenu = CCMenu::create();
    tabsMenu->setPosition({panelSize.width / 2.f, innerTop - 12.f});
    tabsMenu->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    tabsMenu->setID("source-tabs-menu"_spr);
    m_urlPanel->addChild(tabsMenu, 3);

    auto ytBtn = paimon::ui::makeButton("YouTube", [this] {
        switchSourceTab(MusicSourceTab::YouTube);
    }, paimon::ui::Btn::Red, 75.f, 0.44f);
    tabsMenu->addChild(ytBtn);

    auto scBtn = paimon::ui::makeButton("SoundCloud", [this] {
        switchSourceTab(MusicSourceTab::SoundCloud);
    }, paimon::ui::Btn::Pink, 85.f, 0.44f);
    tabsMenu->addChild(scBtn);

    auto spBtn = paimon::ui::makeButton("Spotify", [this] {
        switchSourceTab(MusicSourceTab::Spotify);
    }, paimon::ui::Btn::Green, 75.f, 0.44f);
    tabsMenu->addChild(spBtn);

    tabsMenu->updateLayout();

    m_sourceDescLabel = paimon::ui::makeText(
        "YouTube (Uso Personal) · Descarga local MP3",
        panelSize.width - 24.f, 0.38f,
        paimon::ui::palette::gold, kCCTextAlignmentCenter
    );
    if (m_sourceDescLabel) {
        m_sourceDescLabel->setPosition({panelSize.width / 2.f, innerTop - 30.f});
        m_sourceDescLabel->setID("source-desc-label"_spr);
        m_urlPanel->addChild(m_sourceDescLabel, 3);
    }

    // container 1: URL Download (YouTube & SoundCloud)
    m_urlSubContainer = CCNode::create();
    m_urlSubContainer->setContentSize({panelSize.width, 50.f});
    m_urlSubContainer->setPosition({0.f, 0.f});
    m_urlSubContainer->setID("url-subcontainer"_spr);
    m_urlPanel->addChild(m_urlSubContainer, 3);

    m_ytDlpLabel = paimon::ui::makeText("", panelSize.width - 24.f, 0.36f,
        paimon::ui::palette::muted, kCCTextAlignmentCenter);
    if (m_ytDlpLabel) {
        m_ytDlpLabel->setPosition({panelSize.width / 2.f, 42.f});
        m_ytDlpLabel->setID("ytdlp-status"_spr);
        m_urlSubContainer->addChild(m_ytDlpLabel, 3);
    }

    m_urlInput = TextInput::create(panelSize.width * 0.62f, "Pega enlace de YouTube (youtube.com / youtu.be)");
    if (m_urlInput) {
        m_urlInput->setCommonFilter(geode::CommonFilter::Any);
        m_urlInput->setMaxCharCount(2048);
        m_urlInput->setPosition({panelSize.width * 0.38f, 18.f});
        m_urlInput->setID("url-input"_spr);
        if (!m_initialUrl.empty()) m_urlInput->setString(m_initialUrl);
        m_urlSubContainer->addChild(m_urlInput, 3);
    }

    auto actions = CCMenu::create();
    actions->setPosition({0.f, 0.f});
    actions->setID("url-actions"_spr);
    m_urlSubContainer->addChild(actions, 3);

    if (auto* btn = paimon::ui::makeFrameButton("GJ_pasteBtn2_001.png", 0.9f,
            [this] { this->onPasteUrl(nullptr); })) {
        btn->setPosition({panelSize.width * 0.76f, 18.f});
        actions->addChild(btn);
    }
    if (auto* btn = paimon::ui::makeFrameButton("GJ_downloadBtn_001.png", 0.9f,
            [this] { this->onStartDownload(nullptr); })) {
        btn->setPosition({panelSize.width * 0.9f, 18.f});
        actions->addChild(btn);
    }

    // container 2: Spotify Connect UI (Spotify Tab)
    m_spotifySubContainer = CCNode::create();
    m_spotifySubContainer->setContentSize({panelSize.width, 50.f});
    m_spotifySubContainer->setPosition({0.f, 0.f});
    m_spotifySubContainer->setID("spotify-subcontainer"_spr);
    m_spotifySubContainer->setVisible(false);
    m_urlPanel->addChild(m_spotifySubContainer, 3);

    refreshSpotifyUI();

    auto helpMenu = CCMenu::create();
    helpMenu->setPosition({0.f, 0.f});
    helpMenu->setID("help-menu"_spr);
    m_mainLayer->addChild(helpMenu, 3);
    if (auto* btn = paimon::ui::makeFrameButton("GJ_infoIcon_001.png", 0.6f,
            [this] { this->onOpenYtDlpHelp(nullptr); })) {
        btn->setPosition({14.f + panelSize.width - 14.f, panelY + panelSize.height - 11.f});
        helpMenu->addChild(btn);
    }
}

void MenuMusicAddPopup::switchSourceTab(MusicSourceTab tab) {
    m_currentSourceTab = tab;
    if (m_urlSubContainer) {
        m_urlSubContainer->setVisible(tab != MusicSourceTab::Spotify);
    }
    if (m_spotifySubContainer) {
        m_spotifySubContainer->setVisible(tab == MusicSourceTab::Spotify);
        if (tab == MusicSourceTab::Spotify) {
            refreshSpotifyUI();
        }
    }

    if (m_sourceDescLabel) {
        if (tab == MusicSourceTab::YouTube) {
            m_sourceDescLabel->setString("YouTube (Uso Personal) · Descarga local MP3");
            m_sourceDescLabel->setColor(paimon::ui::palette::gold);
            if (m_urlInput) m_urlInput->setPlaceholder("Pega enlace de YouTube (youtube.com / youtu.be)");
        } else if (tab == MusicSourceTab::SoundCloud) {
            m_sourceDescLabel->setString("SoundCloud (Legal) · Pistas publicas y Creative Commons");
            m_sourceDescLabel->setColor(paimon::ui::palette::warning);
            if (m_urlInput) m_urlInput->setPlaceholder("Pega enlace de SoundCloud (soundcloud.com)");
        } else {
            m_sourceDescLabel->setString("Spotify Connect · Vinculacion oficial sin descargas");
            m_sourceDescLabel->setColor(paimon::ui::palette::success);
        }
    }
}

void MenuMusicAddPopup::refreshSpotifyUI() {
    if (!m_spotifySubContainer) return;
    m_spotifySubContainer->removeAllChildren();

    auto const panelSize = m_spotifySubContainer->getContentSize();
    bool linked = SpotifyService::get().isLinked();

    if (!linked) {
        auto* promptLbl = CCLabelBMFont::create("Ingresa tu usuario de Spotify para vincular:", "chatFont.fnt");
        promptLbl->setScale(0.38f);
        promptLbl->setPosition({panelSize.width * 0.38f, panelSize.height - 12.f});
        promptLbl->setColor(paimon::ui::palette::muted);
        m_spotifySubContainer->addChild(promptLbl);

        m_spotifyUserInput = TextInput::create(panelSize.width * 0.44f, "Usuario de Spotify", "chatFont.fnt");
        m_spotifyUserInput->setMaxCharCount(64);
        m_spotifyUserInput->setPosition({panelSize.width * 0.28f, panelSize.height - 30.f});
        m_spotifySubContainer->addChild(m_spotifyUserInput);

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_spotifySubContainer->addChild(menu);

        auto linkBtn = paimon::ui::makeButton("Vincular", [this] {
            onLinkSpotify();
        }, paimon::ui::Btn::Green, 70.f, 0.44f);
        linkBtn->setPosition({panelSize.width * 0.60f, panelSize.height - 30.f});
        menu->addChild(linkBtn);

        auto webBtn = paimon::ui::makeButton("Abrir Web", [] {
            SpotifyService::get().openSpotifyAppOrWeb();
        }, paimon::ui::Btn::Cyan, 70.f, 0.44f);
        webBtn->setPosition({panelSize.width * 0.82f, panelSize.height - 30.f});
        menu->addChild(webBtn);
    } else {
        std::string userStr = "✓ Conectado como: " + SpotifyService::get().getUserName();
        auto* userLbl = CCLabelBMFont::create(userStr.c_str(), "goldFont.fnt");
        userLbl->setScale(0.40f);
        userLbl->setColor(paimon::ui::palette::success);
        userLbl->setPosition({panelSize.width / 2.f, panelSize.height - 10.f});
        m_spotifySubContainer->addChild(userLbl);

        m_spotifyTrackInput = TextInput::create(panelSize.width * 0.58f, "Pega enlace de cancion (open.spotify.com/track/...)", "chatFont.fnt");
        m_spotifyTrackInput->setMaxCharCount(2048);
        m_spotifyTrackInput->setPosition({panelSize.width * 0.36f, panelSize.height - 30.f});
        m_spotifySubContainer->addChild(m_spotifyTrackInput);

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_spotifySubContainer->addChild(menu);

        auto addTrackBtn = paimon::ui::makeButton("Agregar al Disco", [this] {
            onAddSpotifyTrack();
        }, paimon::ui::Btn::Green, 95.f, 0.42f);
        addTrackBtn->setPosition({panelSize.width * 0.74f, panelSize.height - 30.f});
        menu->addChild(addTrackBtn);

        auto unlinkBtn = paimon::ui::makeButton("Salir", [this] {
            onUnlinkSpotify();
        }, paimon::ui::Btn::Red, 50.f, 0.42f);
        unlinkBtn->setPosition({panelSize.width * 0.92f, panelSize.height - 30.f});
        menu->addChild(unlinkBtn);
    }
}

void MenuMusicAddPopup::onAddSpotifyTrack() {
    if (!m_spotifyTrackInput) return;
    auto url = m_spotifyTrackInput->getString();
    if (url.empty()) {
        Notification::create("Pega un enlace de cancion de Spotify.", NotificationIcon::Warning)->show();
        return;
    }
    if (url.find("spotify.com") == std::string::npos && url.find("spotify:") == std::string::npos) {
        Notification::create("El enlace debe ser de Spotify (ej. open.spotify.com/track/...)", NotificationIcon::Warning)->show();
        return;
    }

    if (m_statusLabel) {
        m_statusLabel->setString("Descargando banner de album de Spotify...");
        m_statusLabel->setColor(paimon::ui::palette::gold);
    }

    auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
    SpotifyService::get().fetchSpotifyTrack(url, [weakThis](bool ok, std::string title, std::string artist, std::string) {
        auto ref = weakThis.lock();
        auto* self = ref ? typeinfo_cast<MenuMusicAddPopup*>(ref.data()) : nullptr;
        if (!self || !self->m_alive.load()) return;

        if (ok) {
            if (self->m_statusLabel) {
                self->m_statusLabel->setString(fmt::format("Agregada con banner: {} ({})", title, artist).c_str());
                self->m_statusLabel->setColor(paimon::ui::palette::success);
            }
            if (self->m_spotifyTrackInput) self->m_spotifyTrackInput->setString("");
            Notification::create("Cancion de Spotify anadida con banner al disco!", NotificationIcon::Success, 3.5f)->show();
        } else {
            if (self->m_statusLabel) {
                self->m_statusLabel->setString("Error al obtener datos de Spotify.");
                self->m_statusLabel->setColor(paimon::ui::palette::danger);
            }
            Notification::create("No se pudo obtener informacion de la cancion.", NotificationIcon::Error)->show();
        }
    });
}

void MenuMusicAddPopup::onLinkSpotify() {
    std::string user = m_spotifyUserInput ? m_spotifyUserInput->getString() : "";
    if (user.empty()) {
        user = "Usuario Spotify";
    }
    SpotifyService::get().linkAccount(user);
    Notification::create("Cuenta de Spotify vinculada!", NotificationIcon::Success)->show();
    refreshSpotifyUI();
}

void MenuMusicAddPopup::onUnlinkSpotify() {
    SpotifyService::get().unlinkAccount();
    Notification::create("Cuenta de Spotify desvinculada.", NotificationIcon::Info)->show();
    refreshSpotifyUI();
}

void MenuMusicAddPopup::onSpotifyMediaCmd(std::string const& cmd) {
    SpotifyService::get().sendMediaCommand(cmd);
    Notification::create("Comando enviado a Spotify", NotificationIcon::Info, 1.2f)->show();
}

void MenuMusicAddPopup::buildLocalSection() {
    auto size = m_mainLayer->getContentSize();

    const CCSize panelSize{size.width - 28.f, 128.f};
    const float panelY = 40.f;
    auto panel = paimon::ui::makePanel(panelSize, "Import a file from your PC");
    panel->setPosition({14.f, panelY});
    panel->setID("local-panel"_spr);
    m_mainLayer->addChild(panel, 2);

    const float innerTop = panelSize.height - paimon::ui::kPanelHeader;
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setID("local-actions"_spr);
    panel->addChild(menu, 3);

    const float row1 = innerTop - 18.f;
    const float row2 = innerTop - 46.f;

    if (auto* b = paimon::ui::makeButton("Audio",
            [this] { this->onPickAudio(nullptr); },
            paimon::ui::Btn::Cyan, 86.f, 0.55f, "bigFont.fnt")) {
        b->setPosition({58.f, row1});
        menu->addChild(b);
    }
    m_audioPathLabel = paimon::ui::makeLabel("No audio selected",
        panelSize.width - 120.f, 0.4f, paimon::ui::palette::muted);
    if (m_audioPathLabel) {
        m_audioPathLabel->setAnchorPoint({0.f, 0.5f});
        m_audioPathLabel->setPosition({112.f, row1});
        panel->addChild(m_audioPathLabel, 3);
    }

    if (auto* b = paimon::ui::makeButton("Cover",
            [this] { this->onPickCover(nullptr); },
            paimon::ui::Btn::Cyan, 86.f, 0.55f, "bigFont.fnt")) {
        b->setPosition({58.f, row2});
        menu->addChild(b);
    }
    m_coverPathLabel = paimon::ui::makeLabel("No cover (optional)",
        panelSize.width - 120.f, 0.4f, paimon::ui::palette::muted);
    if (m_coverPathLabel) {
        m_coverPathLabel->setAnchorPoint({0.f, 0.5f});
        m_coverPathLabel->setPosition({112.f, row2});
        panel->addChild(m_coverPathLabel, 3);
    }

    m_nameInput = TextInput::create(panelSize.width * 0.8f, "Display name (optional)");
    if (m_nameInput) {
        m_nameInput->setCommonFilter(geode::CommonFilter::Any);
        m_nameInput->setMaxCharCount(120);
        m_nameInput->setPosition({panelSize.width / 2.f, innerTop - 72.f});
        m_nameInput->setID("name-input"_spr);
        panel->addChild(m_nameInput, 3);
    }

    if (auto* b = paimon::ui::makeButton("Import",
            [this] { this->onImportLocal(nullptr); },
            paimon::ui::Btn::Green, 120.f, 0.6f)) {
        b->setPosition({panelSize.width / 2.f, 16.f});
        menu->addChild(b);
    }
}

void MenuMusicAddPopup::buildProgressBar() {
    auto size = m_mainLayer->getContentSize();

    const float barW = size.width - 60.f;
    const float barH = 14.f;
    const float barY = size.height - 40.f - 104.f + 16.f;
    const float cx   = size.width / 2.f;

    auto bg = paimon::ui::makeInset({barW, barH}, 230);
    if (bg) {
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
        pct->setColor(paimon::ui::palette::text);
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
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_ytDlpLabel->setString("En movil: usa Newgrounds, Spotify o 'Importar archivo local'");
    m_ytDlpLabel->setColor(paimon::ui::palette::muted);
#else
    auto& boot = YtDlpBootstrap::get();
    if (boot.exists()) {
        m_ytDlpLabel->setString("Ready - paste a link and press the download button");
        m_ytDlpLabel->setColor(paimon::ui::palette::success);
    } else {
        m_ytDlpLabel->setString("The first download installs a small helper (~17 MB, one time)");
        m_ytDlpLabel->setColor(paimon::ui::palette::warning);
    }
#endif
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
        self->m_pendingAudioPath = paimon::paths::toUtf8(v.value());
        if (self->m_audioPathLabel) {
            self->m_audioPathLabel->setString(paimon::paths::toUtf8(v.value().filename()).c_str());
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
        self->m_pendingCoverPath = paimon::paths::toUtf8(v.value());
        if (self->m_coverPathLabel) {
            self->m_coverPathLabel->setString(paimon::paths::toUtf8(v.value().filename()).c_str());
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
    auto ext = paimon::paths::toUtf8(audioP.extension());
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
            auto destCover = lib.getCoversDir() / (id + paimon::paths::toUtf8(coverP.extension()));
            std::filesystem::copy_file(coverP, destCover,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) destCoverStr = paimon::paths::toUtf8(destCover);
        }
    }

    std::string name = m_nameInput ? std::string(m_nameInput->getString()) : std::string();
    if (name.empty()) name = paimon::paths::toUtf8(audioP.stem());

    MusicTrack track;
    track.id = id;
    track.audioPath = paimon::paths::toUtf8(destAudio);
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

    // intercept Spotify links to direct users to account linking
    if (url.find("spotify.com") != std::string::npos) {
        Notification::create(
            "Spotify requiere vincular tu cuenta (pestana Spotify). No admite descarga directa.",
            NotificationIcon::Warning, 4.0f
        )->show();
        switchSourceTab(MusicSourceTab::Spotify);
        return;
    }

    // enforce source rules: YouTube tab only accepts YouTube/Newgrounds; SoundCloud tab only accepts SoundCloud
    if (m_currentSourceTab == MusicSourceTab::YouTube) {
        if (url.find("soundcloud.com") != std::string::npos) {
            switchSourceTab(MusicSourceTab::SoundCloud);
        } else if (url.find("youtube.com") == std::string::npos &&
                   url.find("youtu.be") == std::string::npos &&
                   parseNewgroundsSongId(url) == 0) {
            Notification::create(
                "Solo se admiten enlaces de YouTube (o Newgrounds) en esta seccion.",
                NotificationIcon::Warning, 3.5f
            )->show();
            return;
        }
    } else if (m_currentSourceTab == MusicSourceTab::SoundCloud) {
        if (url.find("youtube.com") != std::string::npos || url.find("youtu.be") != std::string::npos) {
            switchSourceTab(MusicSourceTab::YouTube);
        } else if (url.find("soundcloud.com") == std::string::npos && parseNewgroundsSongId(url) == 0) {
            Notification::create(
                "Solo se admiten enlaces de SoundCloud en esta seccion.",
                NotificationIcon::Warning, 3.5f
            )->show();
            return;
        }
    }

// newgrounds links/ids use gd's downloader, then register the resulting mp3.
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
// the service already registered the track.
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

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    if (m_statusLabel) {
        m_statusLabel->setString("Descarga directa de URL solo disponible en PC; usa Newgrounds o archivo local.");
        m_statusLabel->setColor({255, 190, 110});
    }
    Notification::create(
        "yt-dlp no esta soportado en movil. Usa un ID/enlace de Newgrounds o 'Importar archivo local'.",
        NotificationIcon::Warning, 4.0f)->show();
    return;
#endif

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
            [weakThis = geode::WeakRef<cocos2d::CCNode>(this)](FLAlertLayer*, bool accepted) {
                auto ref = weakThis.lock();
                auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
                if (!self || !self->m_alive.load()) return;
                if (!accepted) {
                    if (self->m_statusLabel) {
                        self->m_statusLabel->setString("Install cancelled.");
                        self->m_statusLabel->setColor({200, 200, 200});
                    }
                    self->refreshStatus();
                    return;
                }

// m_alive and weakref guard ui access during install.
                ToolInstallPopupConfig ytConfig;
                ytConfig.popupW = 400.f;
                ytConfig.popupH = 220.f;
                ytConfig.title = "Installing yt-dlp";
                ytConfig.infoTitle = "yt-dlp";
                ytConfig.infoBody =
                    "yt-dlp is the <cy>audio downloader</c> Paimbnails uses to fetch songs from "
                    "online sources. It downloads <cg>once</c> (~17 MB) and is kept bundled with the mod.";
                ytConfig.headline = "Downloading the audio downloader (one-time, ~17 MB)";
                ytConfig.barW = 320.f;
                ytConfig.fillColor = {120, 220, 255, 255};
                ytConfig.successMessage = "yt-dlp installed - starting download...";
                ytConfig.pathLimit = 60;
                auto installPopup = ToolInstallPopupBase::createForTool(
                    ytConfig, YtDlpBootstrap::get(),
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

// ffmpeg converts aac/opus to mp3 for fmod.
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
            [weakThis = geode::WeakRef<cocos2d::CCNode>(this)](FLAlertLayer*, bool accepted) {
                auto ref = weakThis.lock();
                auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
                if (!self || !self->m_alive.load()) return;
                if (!accepted) {
                    if (self->m_statusLabel) {
                        self->m_statusLabel->setString("Install cancelled.");
                        self->m_statusLabel->setColor({200, 200, 200});
                    }
                    self->refreshStatus();
                    return;
                }

                ToolInstallPopupConfig ffConfig;
                ffConfig.popupW = 420.f;
                ffConfig.popupH = 230.f;
                ffConfig.title = "Installing ffmpeg";
                ffConfig.infoTitle = "ffmpeg";
                ffConfig.infoBody =
                    "ffmpeg is the <cy>audio converter</c> Paimbnails uses to decode and re-encode "
                    "songs. It downloads <cg>once</c> (~80 MB) and is kept bundled with the mod.";
                ffConfig.headline = "Downloading audio converter (one-time, ~80 MB)";
                ffConfig.barW = 340.f;
                ffConfig.fillColor = {180, 140, 255, 255};
                ffConfig.successMessage = "ffmpeg installed - resuming download...";
                ffConfig.pathLimit = 62;
                auto installPopup = ToolInstallPopupBase::createForTool(
                    ffConfig, FfmpegBootstrap::get(),
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
                // the track still counts even if the popup closed first.
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
                ? paimon::paths::toUtf8(std::filesystem::path(result.audioPath).stem())
                : result.displayName;
            t.artist = result.artist;
            t.sourceUrl = url;
            t.source = TrackSource::Downloaded;
            t.addedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            MenuMusicLibrary::get().addTrack(t);

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
    // bypasses the input filter.
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
    auto bundleStr = paimon::paths::toUtf8(bundle);
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
