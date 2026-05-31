#include "MenuMusicAddPopup.hpp"

#include "../services/MenuMusicLibrary.hpp"
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
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

MenuMusicAddPopup* MenuMusicAddPopup::create() {
    auto ret = new MenuMusicAddPopup();
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

// ── URL section ───────────────────────────────────────────────

void MenuMusicAddPopup::buildUrlSection() {
    auto size = m_mainLayer->getContentSize();

    auto header = CCLabelBMFont::create("Download via yt-dlp", "goldFont.fnt");
    if (header) {
        header->setScale(0.45f);
        header->setPosition({size.width / 2.f, size.height * 0.88f});
        header->setID("url-header"_spr);
        m_mainLayer->addChild(header, 3);
    }

    m_ytDlpLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_ytDlpLabel) {
        m_ytDlpLabel->setScale(0.38f);
        m_ytDlpLabel->setPosition({size.width / 2.f, size.height * 0.82f});
        m_ytDlpLabel->setColor({180, 220, 180});
        m_ytDlpLabel->setID("ytdlp-status"_spr);
        m_mainLayer->addChild(m_ytDlpLabel, 3);
    }

    m_urlInput = TextInput::create(size.width * 0.55f, "Paste URL or click the clipboard button");
    if (m_urlInput) {
        // Forzamos el whitelist completo (Any) primero...
        m_urlInput->setCommonFilter(geode::CommonFilter::Any);
        // ...y ademas sobreescribimos el m_allowedChars del CCTextInputNode
        // interno, porque en algunas builds de Geode `setCommonFilter(Any)`
        // no incluye ':', '/', '?', '&', '=' y la URL llega mutilada como
        // "httpswwwyoutubecomwatchv..." a yt-dlp. Este fallback lo ha usado
        // tambien ShareCommentLayer para admitir caracteres de emotes.
        if (auto* inner = m_urlInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }
        m_urlInput->setMaxCharCount(2048);
        m_urlInput->setPosition({size.width * 0.34f, size.height * 0.73f});
        m_urlInput->setID("url-input"_spr);
        m_mainLayer->addChild(m_urlInput, 3);
    }

    // Boton "Paste from clipboard" — via geode::utils::clipboard::read bypass
    // completamente el filtro de chars del TextInput. Aunque el usuario pegue
    // con Ctrl+V y Geode le coma los ':' y '/', este boton lee la URL directa
    // del portapapeles y la inyecta con setString(), que no aplica filtro.
    {
        auto pasteSpr = CCSprite::createWithSpriteFrameName("GJ_clipboardIcon_001.png");
        if (!pasteSpr) {
            // Fallback: boton textual si el sprite frame no existe.
            auto bs = ButtonSprite::create("Paste", 50, true, "bigFont.fnt",
                "GJ_button_04.png", 22.f, 0.5f);
            if (bs) {
                auto btn = CCMenuItemSpriteExtra::create(bs, this,
                    menu_selector(MenuMusicAddPopup::onPasteUrl));
                auto menu = CCMenu::create();
                menu->setPosition({size.width * 0.73f, size.height * 0.73f});
                menu->addChild(btn);
                menu->setID("paste-menu"_spr);
                m_mainLayer->addChild(menu, 3);
            }
        } else {
            pasteSpr->setScale(0.7f);
            auto btn = CCMenuItemSpriteExtra::create(pasteSpr, this,
                menu_selector(MenuMusicAddPopup::onPasteUrl));
            if (btn) {
                auto menu = CCMenu::create();
                menu->setPosition({size.width * 0.73f, size.height * 0.73f});
                menu->addChild(btn);
                menu->setID("paste-menu"_spr);
                m_mainLayer->addChild(menu, 3);
            }
        }
    }

    auto dlSpr = ButtonSprite::create("Download", 90, true, "bigFont.fnt", "GJ_button_05.png", 24.f, 0.55f);
    if (dlSpr) {
        auto btn = CCMenuItemSpriteExtra::create(dlSpr, this,
            menu_selector(MenuMusicAddPopup::onStartDownload));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.88f, size.height * 0.73f});
        menu->addChild(btn);
        menu->setID("dl-menu"_spr);
        m_mainLayer->addChild(menu, 3);
    }

    // Boton info con el path sugerido.
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

// ── Local section ─────────────────────────────────────────────

void MenuMusicAddPopup::buildLocalSection() {
    auto size = m_mainLayer->getContentSize();

    auto sep = CCLayerColor::create(ccc4(100, 100, 120, 120));
    if (sep) {
        sep->setContentSize({size.width - 30.f, 1.f});
        sep->setAnchorPoint({0.5f, 0.5f});
        sep->setPosition({size.width / 2.f, size.height * 0.62f});
        sep->ignoreAnchorPointForPosition(false);
        m_mainLayer->addChild(sep, 2);
    }

    auto header = CCLabelBMFont::create("Import local files", "goldFont.fnt");
    if (header) {
        header->setScale(0.45f);
        header->setPosition({size.width / 2.f, size.height * 0.57f});
        header->setID("local-header"_spr);
        m_mainLayer->addChild(header, 3);
    }

    auto audioSpr = ButtonSprite::create("Audio", 80, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.55f);
    if (audioSpr) {
        auto b = CCMenuItemSpriteExtra::create(audioSpr, this,
            menu_selector(MenuMusicAddPopup::onPickAudio));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.23f, size.height * 0.48f});
        menu->addChild(b);
        m_mainLayer->addChild(menu, 3);
    }
    m_audioPathLabel = CCLabelBMFont::create("No audio selected", "chatFont.fnt");
    if (m_audioPathLabel) {
        m_audioPathLabel->setScale(0.35f);
        m_audioPathLabel->setAnchorPoint({0.f, 0.5f});
        m_audioPathLabel->setPosition({size.width * 0.4f, size.height * 0.48f});
        m_audioPathLabel->setColor({200, 200, 200});
        m_mainLayer->addChild(m_audioPathLabel, 3);
    }

    auto coverSpr = ButtonSprite::create("Cover", 80, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.55f);
    if (coverSpr) {
        auto b = CCMenuItemSpriteExtra::create(coverSpr, this,
            menu_selector(MenuMusicAddPopup::onPickCover));
        auto menu = CCMenu::create();
        menu->setPosition({size.width * 0.23f, size.height * 0.38f});
        menu->addChild(b);
        m_mainLayer->addChild(menu, 3);
    }
    m_coverPathLabel = CCLabelBMFont::create("No cover (optional)", "chatFont.fnt");
    if (m_coverPathLabel) {
        m_coverPathLabel->setScale(0.35f);
        m_coverPathLabel->setAnchorPoint({0.f, 0.5f});
        m_coverPathLabel->setPosition({size.width * 0.4f, size.height * 0.38f});
        m_coverPathLabel->setColor({200, 200, 200});
        m_mainLayer->addChild(m_coverPathLabel, 3);
    }

    m_nameInput = TextInput::create(size.width * 0.75f, "Display name (optional)");
    if (m_nameInput) {
        m_nameInput->setCommonFilter(geode::CommonFilter::Any);
        if (auto* inner = m_nameInput->getInputNode()) {
            inner->m_allowedChars = geode::getCommonFilterAllowedChars(geode::CommonFilter::Any);
        }
        m_nameInput->setMaxCharCount(120);
        m_nameInput->setPosition({size.width / 2.f, size.height * 0.28f});
        m_nameInput->setID("name-input"_spr);
        m_mainLayer->addChild(m_nameInput, 3);
    }

    auto importSpr = ButtonSprite::create("Import", 110, true, "bigFont.fnt", "GJ_button_05.png", 26.f, 0.6f);
    if (importSpr) {
        auto b = CCMenuItemSpriteExtra::create(importSpr, this,
            menu_selector(MenuMusicAddPopup::onImportLocal));
        auto menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height * 0.15f});
        menu->addChild(b);
        m_mainLayer->addChild(menu, 3);
    }
}

// ── Refresh status ────────────────────────────────────────────

void MenuMusicAddPopup::refreshStatus() {
    if (!m_ytDlpLabel) return;
    auto& boot = YtDlpBootstrap::get();
    if (boot.exists()) {
        m_ytDlpLabel->setString("yt-dlp ready - paste URL & hit Download");
        m_ytDlpLabel->setColor({180, 230, 180});
    } else {
        m_ytDlpLabel->setString("yt-dlp not installed - will auto-install on first download (~17MB)");
        m_ytDlpLabel->setColor({255, 220, 150});
    }
}

// ── File pickers ──────────────────────────────────────────────

void MenuMusicAddPopup::onPickAudio(CCObject*) {
    auto weakThis = this;
    pt::pickAudio([weakThis, this](Result<std::optional<std::filesystem::path>> res) {
        if (!m_alive.load()) return;
        if (!res) return;
        auto v = res.unwrap();
        if (!v.has_value()) return;
        m_pendingAudioPath = geode::utils::string::pathToString(v.value());
        if (m_audioPathLabel) {
            m_audioPathLabel->setString(geode::utils::string::pathToString(v.value().filename()).c_str());
        }
    });
}

void MenuMusicAddPopup::onPickCover(CCObject*) {
    pt::pickImage([this](Result<std::optional<std::filesystem::path>> res) {
        if (!m_alive.load()) return;
        if (!res) return;
        auto v = res.unwrap();
        if (!v.has_value()) return;
        m_pendingCoverPath = geode::utils::string::pathToString(v.value());
        if (m_coverPathLabel) {
            m_coverPathLabel->setString(geode::utils::string::pathToString(v.value().filename()).c_str());
        }
    });
}

// ── Import local ──────────────────────────────────────────────

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

    std::string name = m_nameInput ? m_nameInput->getString() : "";
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

    // Reset inputs
    m_pendingAudioPath.clear();
    m_pendingCoverPath.clear();
    if (m_audioPathLabel) m_audioPathLabel->setString("No audio selected");
    if (m_coverPathLabel) m_coverPathLabel->setString("No cover (optional)");
    if (m_nameInput) m_nameInput->setString("");

    Notification::create("Track imported!", NotificationIcon::Success)->show();
}

// ── Download via yt-dlp ───────────────────────────────────────

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

    // Si falta yt-dlp, preguntamos al usuario ANTES de iniciar cualquier
    // descarga. Es el comportamiento que el usuario pidio explicitamente:
    // nada de descargas silenciosas. Si acepta, abrimos el popup dedicado
    // con barra de progreso y al terminar reintentamos esta misma funcion;
    // ya no caera aqui.
    auto& bootstrap = YtDlpBootstrap::get();
    if (!bootstrap.exists()) {
        if (m_statusLabel) {
            m_statusLabel->setString("yt-dlp is not installed.");
            m_statusLabel->setColor({255, 200, 140});
        }

        geode::createQuickPopup(
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

                // Pasamos `this` y usamos m_alive como atomic guard para
                // evitar tocar la UI si el popup padre se cerro mientras
                // el install popup estaba abierto.
                // Usamos WeakRef para evitar dangling pointer si el popup se destruye
                // durante la instalacion.
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
                        // Reintentamos la descarga; ya tenemos yt-dlp.
                        self->onStartDownload(nullptr);
                    }
                );
                if (installPopup) installPopup->show();
            }
        );
        return;
    }

    // Lo mismo con ffmpeg — sin el no podemos garantizar audio
    // reproducible por FMOD (AAC/Opus no funcionan en GD).
    auto& ffmpeg = FfmpegBootstrap::get();
    if (!ffmpeg.exists()) {
        if (m_statusLabel) {
            m_statusLabel->setString("ffmpeg is not installed.");
            m_statusLabel->setColor({255, 200, 140});
        }

        geode::createQuickPopup(
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
        );
        return;
    }

    auto& dl = YtDlpDownloader::get();
    if (!dl.isAvailable()) {
        // No deberia pasar tras ensureInstalled, pero por seguridad.
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

    auto id = MenuMusicLibrary::get().generateId("dl");

    auto weakThis = geode::WeakRef<cocos2d::CCNode>(this);
    dl.download(url, id,
        [weakThis](YtDlpProgress p) {
            auto ref = weakThis.lock();
            auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
            if (!self || !self->m_alive.load()) return;
            if (self->m_statusLabel) {
                if (p.stage == "downloading") {
                    self->m_statusLabel->setString(
                        fmt::format("Downloading... {:.0f}%", p.percent * 100.f).c_str());
                } else if (!p.message.empty()) {
                    self->m_statusLabel->setString(p.message.substr(0, 64).c_str());
                }
            }
        },
        [weakThis, url](YtDlpResult result) {
            auto ref = weakThis.lock();
            auto* self = typeinfo_cast<MenuMusicAddPopup*>(ref.data());
            if (!self || !self->m_alive.load()) {
                // popup cerrado: igual registramos el track para no perder el download
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

            // Manejo de errores especiales: el downloader nos dice que
            // falta un binario. Reintentamos entrando por onStartDownload,
            // que ahora si preguntara al usuario.
            if (!result.success && result.error == "__NEED_YTDLP__") {
                self->onStartDownload(nullptr);
                return;
            }
            if (!result.success && result.error == "__NEED_FFMPEG__") {
                self->onStartDownload(nullptr);
                return;
            }

            if (!result.success) {
                if (self->m_statusLabel) {
                    self->m_statusLabel->setString(
                        fmt::format("Error: {}", result.error.substr(0, 100)).c_str());
                    self->m_statusLabel->setColor({255, 130, 130});
                }
                Notification::create(result.error.substr(0, 120),
                    NotificationIcon::Error, 5.f)->show();
                return;
            }

            // Persistir el track en la libreria.
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

            if (self->m_statusLabel) {
                self->m_statusLabel->setString("Download complete!");
                self->m_statusLabel->setColor({140, 230, 140});
            }
            if (self->m_urlInput) self->m_urlInput->setString("");
            Notification::create("Track downloaded!", NotificationIcon::Success)->show();
        }
    );
}

// ── yt-dlp help popup ─────────────────────────────────────────

void MenuMusicAddPopup::onPasteUrl(CCObject*) {
    if (!m_urlInput) return;
    // Leer clipboard directamente. Esto evita cualquier filtro del input.
    auto clip = geode::utils::clipboard::read();
    // Trim whitespace / newlines al inicio y final.
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
    // Inyectar via setString — el TextInput acepta el string completo sin
    // aplicar el filtro de caracteres (que solo actua en keystroke/paste UI).
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

    FLAlertLayer::create("yt-dlp setup", msg.c_str(), "OK")->show();
}

} // namespace paimon::menumusic
