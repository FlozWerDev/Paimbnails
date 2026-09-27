#include "WelcomeFlow.hpp"

#include "../../layers/PaiConfigLayer.hpp"
#include "../../features/cursor/ui/CursorConfigPopup.hpp"
#include "../../features/cursor/services/CursorManager.hpp"
#include "../../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../../features/updates/services/UpdateChecker.hpp"
#include "../../features/crash-reports/services/CrashReporter.hpp"
#include "../../features/forum/services/ForumApi.hpp"
#include "../../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../../core/BanGate.hpp"
#include "../../features/thumb-alerts/services/NewThumbWatcher.hpp"
#include "../../features/thumb-alerts/services/ThumbFeedSocket.hpp"
#include "../../utils/Localization.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <functional>
#include <vector>

using namespace geode::prelude;

namespace paimon::onboarding {
namespace {

constexpr int kTermsVersion = 1;
constexpr float kWidth = 440.f;
constexpr float kHeight = 300.f;

struct Option {
    char const* key;
    char const* es;
    char const* en;
    char const* detailEs;
    char const* detailEn;
};

struct Step {
    char const* titleEs;
    char const* titleEn;
    char const* hintEs;
    char const* hintEn;
    std::vector<Option> options;
};

std::vector<Step> const& steps() {
    static std::vector<Step> const value = {
        {"Idioma", "Language", "Elige el idioma del mod. Puedes cambiarlo despues.",
            "Choose the mod language. You can change it later.", {}},
        {"Fondos", "Backgrounds", "Personaliza las pantallas en el Editor de Fondos.",
            "Customize screens in the Background Editor.", {
                {"menu-background-dark", "Oscurecer fondo del menu", "Dim menu background", "Ayuda a leer los botones.", "Makes buttons easier to read."},
                {"bg-adaptive-colors", "Colores adaptativos", "Adaptive colors", "Ajusta el color del menu al fondo.", "Matches menu colors to the background."},
            }},
        {"Apariencia", "Appearance", "Iconos, perfiles, animaciones y ventanas.",
            "Icons, profiles, animations and popups.", {
                {"colorful-icons-enabled", "Iconos recoloreados", "Recolored icons", "Efectos de color en iconos.", "Color effects on icons."},
                {"icon-gradients-enabled", "Degradados en iconos", "Icon gradients", "Degradados para tus iconos.", "Gradients for your icons."},
                {"profile-redesign-enabled", "Nuevo diseno de perfil", "Profile redesign", "Presentacion renovada del perfil.", "Updated profile layout."},
                {"smooth-ui-enabled", "Animaciones suaves", "Smooth UI", "Movimiento en menus y botones.", "Motion in menus and buttons."},
                {"popup-blur-enabled", "Desenfoque de ventanas", "Popup blur", "Difumina el fondo de las ventanas.", "Blurs the area behind popups."},
                {"custom-hover-enabled", "Efectos al pasar el cursor", "Hover effects", "Efectos en controles interactivos.", "Effects on interactive controls."},
                {"dynamic-popup-enabled", "Ventanas dinamicas", "Dynamic popups", "Anima la entrada de ventanas.", "Animates popup entry."},
                {"smooth-text-enabled", "Texto animado", "Animated text", "Anima los caracteres al escribir.", "Animates characters as you type."},
                {"global-icons-enabled", "Iconos compartidos", "Shared icons", "Con More Icons, muestra iconos de otros.", "With More Icons, shows other players' icons."},
            }},
        {"Cursor", "Cursor", "Activa un cursor propio y abre su editor para elegir imagen y efectos.",
            "Enable a custom cursor and open its editor for images and effects.", {
                {"custom-cursor-enable", "Cursor personalizado", "Custom cursor", "Reemplaza el cursor del sistema.", "Replaces the system cursor."},
                {"custom-slider-enabled", "Barra de desplazamiento propia", "Custom scroll thumb", "Personaliza los controles deslizantes.", "Customizes slider controls."},
                {"smooth-scroll", "Desplazamiento suave", "Smooth scrolling", "Suaviza el movimiento de listas.", "Smooths list movement."},
            }},
        {"Miniaturas y niveles", "Thumbnails and levels", "Vista de niveles, capturas y vistas previas.",
            "Level view, captures and previews.", {
                {"auto-preview-enable", "Vistas previas automaticas", "Automatic previews", "Genera miniaturas al jugar niveles.", "Creates thumbnails while playing levels."},
                {"enable-thumbnail-taking", "Boton de captura", "Capture button", "Permite crear tus miniaturas.", "Lets you create thumbnails."},
                {"compact-list-mode", "Listas compactas", "Compact lists", "Muestra mas niveles por pantalla.", "Shows more levels per screen."},
                {"levelcell-hover-effects", "Animacion de celdas", "Cell animation", "Anima las celdas al pasar el cursor.", "Animates cells on hover."},
            }},
        {"Audio", "Audio", "Musica del menu, perfiles y niveles.",
            "Menu, profile and level music.", {
                {"menuMusicEnable", "Reproductor del menu", "Menu music player", "Biblioteca y listas de musica.", "Music library and playlists."},
                {"profile-music-enabled", "Musica de perfil", "Profile music", "Reproduce musica en perfiles.", "Plays music on profiles."},
                {"dynamic-song", "Cancion dinamica del nivel", "Dynamic level song", "Escucha la cancion desde la vista del nivel.", "Hear a song from the level screen."},
            }},
        {"Juego", "Gameplay", "Ajustes visibles durante los niveles y menus.",
            "Settings visible in levels and menus.", {
                {"golden-best-enabled", "Mejor progreso dorado", "Golden best", "Resalta tu mejor progreso.", "Highlights your best progress."},
                {"info-mod-death-heatmap", "Mapa de muertes", "Death heatmap", "Muestra donde mueres mas veces.", "Shows where you die most often."},
                {"menu-physics-enable", "Fisica del menu", "Menu physics", "Anade movimiento a elementos del menu.", "Adds motion to menu elements."},
                {"input-scroll-enable", "Rueda en campos numericos", "Number field scrolling", "Ajusta valores con la rueda.", "Adjusts values with the mouse wheel."},
            }},
        {"Editor y creacion", "Editor and creation", "Herramientas de edicion y creacion de recursos.",
            "Editing and asset creation tools.", {
                {"editor-music-enable", "Musica en el editor", "Editor music", "Control musical en el editor.", "Music controls in the editor."},
                {"editor-color-picker-enable", "Selector de color", "Color picker", "Colores mas faciles de elegir.", "Easier color selection."},
                {"gif-import-enable", "Importar GIF", "GIF import", "Convierte GIF en objetos.", "Converts GIFs to objects."},
                {"icon-maker-enabled", "Creador de iconos", "Icon maker", "Crea iconos propios.", "Create your own icons."},
                {"texture-studio-enabled", "Texture Studio", "Texture Studio", "Crea paquetes de texturas.", "Create texture packs."},
                {"collab-enabled", "Editor colaborativo", "Collaborative editor", "Salas de edicion en linea.", "Online editing rooms."},
            }},
        {"Utilidades", "Utilities", "Busqueda, informacion y mantenimiento.",
            "Search, information and maintenance.", {
                {"info-suite-enable", "Informacion de niveles", "Level info suite", "Historial, progreso y filtros.", "History, progress and filters."},
                {"song-search-enable", "Buscar canciones", "Song search", "Busca canciones por nombre.", "Find songs by name."},
                {"gd-robtop-cache-enabled", "Cache de RobTop", "RobTop cache", "Reduce consultas repetidas.", "Reduces repeated requests."},
                {"enable-disk-cache", "Cache en disco", "Disk cache", "Guarda recursos para cargarlos mas rapido.", "Stores resources for faster loading."},
                {"incognito-mode", "Modo incognito de busqueda", "Search incognito mode", "No guarda tus busquedas recientes.", "Does not save recent searches."},
            }},
        {"Comunidad y datos", "Community and data", "Funciones conectadas; revisa los terminos antes de aceptar.",
            "Connected features; review the terms before accepting.", {
                {"discord-rpc-enabled", "Estado de Discord", "Discord activity status", "Comparte tu actividad con Discord.", "Shares activity with Discord."},
                {"thumbalert-enabled", "Avisos de miniaturas", "Thumbnail alerts", "Consulta novedades del servidor.", "Checks the server for new thumbnails."},
                {"mentions-enabled", "Avisos de menciones", "Mention alerts", "Busca menciones de tu usuario.", "Checks for mentions of your username."},
                {"msgnotif-enabled", "Avisos de mensajes", "Message alerts", "Consulta mensajes del juego.", "Checks game messages."},
                {"twitch-requests-enabled", "Peticiones en directo", "Live requests", "Lee peticiones de plataformas de streaming.", "Reads requests from streaming platforms."},
                {"auto-update", "Actualizaciones automaticas", "Automatic updates", "Descarga actualizaciones al cerrar el juego.", "Downloads updates when exiting the game."},
                {"crash-reports-enable", "Enviar reportes de errores", "Send crash reports", "Envia registros tras un fallo.", "Sends logs after a crash."},
            }},
    };
    return value;
}

struct TermsPage {
    char const* titleEs;
    char const* titleEn;
    char const* bodyEs;
    char const* bodyEn;
};

std::array<TermsPage, 5> const& termsPages() {
    static std::array<TermsPage, 5> const pages = {{
        {"1. Datos locales", "1. Local data",
            "Paimbnails guarda en tu dispositivo ajustes, recursos descargados, miniaturas, imagenes, musica, iconos, historial de busqueda, progreso, datos de perfil y otros archivos generados por las funciones que uses. Algunas funciones recuerdan nombres de usuario, identificadores de niveles o cuentas y contenido creado por ti. Puedes borrar estos datos desde las opciones del mod o la carpeta de datos de Geode. El modo incognito evita guardar nuevas busquedas; no borra el historial anterior.",
            "Paimbnails stores settings, downloaded resources, thumbnails, images, music, icons, search history, progress, profile data and other files created by features you use on your device. Some features remember usernames, level or account IDs and content you create. You can remove these data through mod options or the Geode data folder. Incognito mode stops saving new searches; it does not erase earlier history."},
        {"2. Red y servidor", "2. Network and server",
            "El mod consulta api.flozwer.org y paimbnailsbot.onrender.com para miniaturas, perfiles, foro, avisos y otras funciones comunitarias. Tras aceptar, la presencia del foro puede enviar tu identidad publica de Geometry Dash y una marca de tiempo. Si subes miniaturas, GIF, videos, imagenes de perfil, dibujos, mensajes o contenido colaborativo, se envian los archivos y datos asociados al servicio correspondiente y pueden hacerse visibles a otros usuarios. Las consultas de red exponen al servidor la direccion IP y datos tecnicos propios de la conexion. Los reportes de fallos solo se envian si activas su opcion; incluyen el registro de Geode y el de la sesion, con rutas comunes de cuenta sustituidas, aunque el contenido puede conservar otros datos escritos en los registros.",
            "The mod contacts api.flozwer.org and paimbnailsbot.onrender.com for thumbnails, profiles, forum, alerts and other community features. After acceptance, forum presence may send your public Geometry Dash identity and a timestamp. If you upload thumbnails, GIFs, videos, profile images, drawings, messages or collaborative content, the files and related data go to the relevant service and may become visible to others. Network requests expose your IP address and connection details to the server. Crash reports are sent only if you enable that option; they include Geode and session logs with common account folder names replaced, though other data written to the logs may remain."},
        {"3. Servicios externos", "3. External services",
                "Segun las funciones que uses, el mod puede conectar con RobTop/Geometry Dash, Geode, GitHub, Codeberg, Discord, Newgrounds, YouTube, SoundCloud, Twitch, Kick, TikTok y su relay, GDHistory, LevelTags, LevelThumbs, Global Icons, el servidor Versus, rw-designer.com, custom-cursor.com y repositorios de mods. Las imagenes de Discord pueden subirse a 0x0.st o catbox.moe si lo pides. Algunas funciones descargan yt-dlp o FFmpeg bajo tu accion. Discord Rich Presence publica tu actividad si esta habilitado. El editor colaborativo usa un servidor HTTP separado; la voz y los mensajes de una sala se comparten con sus participantes. Cada servicio aplica sus propias condiciones y practicas de datos. Desactiva la funcion correspondiente si no quieres usarlo.",
            "Depending on the features you use, the mod may contact RobTop/Geometry Dash, Geode, GitHub, Codeberg, Discord, Newgrounds, YouTube, SoundCloud, Twitch, Kick, TikTok and its relay, GDHistory, LevelTags, LevelThumbs, Global Icons, the Versus server, rw-designer.com, custom-cursor.com and mod repositories. Discord images may be uploaded to 0x0.st or catbox.moe if you request it. Some features download yt-dlp or FFmpeg at your request. Discord Rich Presence publishes your activity when enabled. The collaborative editor uses a separate HTTP server; room voice and messages are shared with participants. Each service has its own terms and data practices. Disable the corresponding feature if you do not want to use it."},
        {"4. Mods y contenido ajeno", "4. Other mods and content",
            "Paimbnails requiere geode.node-ids y declara compatibilidad opcional con Better Touch Prio, ImagePlus, Texture Loader, More Icons, Level Tags y Globed. Acredita ideas de Mod Previews (Alphalaneous), Message Notifications (BlueToadMaker), Search History (hiimjasmine00), Separate Dual Icons (Weebify), Custom Cursor (Ecuet) y Texture Studio/PackGen (Asterveila y ravexcode). Search History adapta codigo MIT; los avisos completos estan en THIRD-PARTY-NOTICES.md. Las obras, canciones, cursores, mods y recursos de terceros siguen perteneciendo a sus autores. Al subir contenido, confirma que tienes permiso y respeta las reglas del servicio de destino. Paimbnails no representa a esos autores ni servicios.",
            "Paimbnails requires geode.node-ids and declares optional compatibility with Better Touch Prio, ImagePlus, Texture Loader, More Icons, Level Tags and Globed. It credits ideas from Mod Previews (Alphalaneous), Message Notifications (BlueToadMaker), Search History (hiimjasmine00), Separate Dual Icons (Weebify), Custom Cursor (Ecuet) and Texture Studio/PackGen (Asterveila and ravexcode). Search History adapts MIT code; full notices are in THIRD-PARTY-NOTICES.md. Third-party art, songs, cursors, mods and resources remain their authors' property. When uploading content, make sure you have permission and follow the destination service's rules. Paimbnails does not represent those authors or services."},
        {"5. Aceptacion", "5. Acceptance",
            "Al aceptar, confirmas que has leido estas condiciones y decides usar Paimbnails y las funciones que actives. El mod modifica Geometry Dash y puede tener errores, conflictos con otros mods o interrupciones de servicios. Revisa las opciones antes de compartir contenido o activar integraciones. Puedes rechazar cerrando esta ventana; no se guardara la aceptacion y volvera a aparecer en el siguiente inicio. El tutorial se puede saltar, pero la aceptacion es independiente. La aceptacion se guarda localmente en welcome.json junto con la version de estas condiciones. Si el texto cambia de forma importante, se solicitara aceptacion de nuevo.",
            "By accepting, you confirm that you have read these terms and choose to use Paimbnails and the features you enable. The mod changes Geometry Dash and may have bugs, conflicts with other mods or service outages. Check options before sharing content or enabling integrations. You can decline by closing this window; acceptance will not be saved and it will appear again on the next launch. The tutorial is skippable, but acceptance is separate. Acceptance is stored locally in welcome.json with the version of these terms. Material changes to this text will ask for acceptance again."},
    }};
    return pages;
}

bool spanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

std::string localized(char const* es, char const* en) {
    return spanish() ? es : en;
}

std::filesystem::path welcomePath() {
    return Mod::get()->getSaveDir() / "welcome.json";
}

bool readOption(std::string const& key) {
    if (key == "menu-background-dark") return LayerBackgroundManager::get().getConfig("menu").darkMode;
    if (key == "bg-adaptive-colors") return Mod::get()->getSavedValue<bool>(key, false);
    return Mod::get()->getSettingValue<bool>(key);
}

void writeOption(std::string const& key, bool value) {
    if (key == "menu-background-dark") {
        auto cfg = LayerBackgroundManager::get().getConfig("menu");
        cfg.darkMode = value;
        LayerBackgroundManager::get().saveConfig("menu", cfg);
    } else if (key == "bg-adaptive-colors") {
        Mod::get()->setSavedValue(key, value);
    } else if (key == "custom-cursor-enable") {
        auto& cursor = CursorManager::get();
        cursor.config().enabled = value;
        cursor.saveConfig();
        Mod::get()->setSettingValue<bool>(key, value);
        cursor.applyConfigLive();
    } else {
        Mod::get()->setSettingValue<bool>(key, value);
    }
}

CCMenuItemSpriteExtra* button(std::string const& label, char const* texture,
    std::function<void()> action, float scale = 0.48f) {
    auto* sprite = ButtonSprite::create(label.c_str(), "goldFont.fnt", texture, scale);
    return CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) { action(); });
}

void addText(CCNode* parent, std::string const& value, float x, float y,
    float width, float scale, bool gold = false) {
    auto* text = SimpleTextArea::create(value, gold ? "goldFont.fnt" : "chatFont.fnt", scale, width);
    if (!text) return;
    text->setAnchorPoint({0.f, 1.f});
    text->setPosition({x, y});
    parent->addChild(text);
}

}

bool isAccepted() {
    auto result = geode::utils::file::readString(welcomePath());
    if (!result) return false;
    auto parsed = matjson::parse(result.unwrap());
    if (!parsed) return false;
    auto const& data = parsed.unwrap();
    return data.isObject() && data.contains("accepted_terms_version") &&
        data["accepted_terms_version"].asInt().unwrapOr(0) == kTermsVersion;
}

WelcomePopup* WelcomePopup::create() {
    auto* popup = new WelcomePopup();
    if (popup && popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

bool WelcomePopup::init() {
    if (!Popup::init(kWidth, kHeight)) return false;
    m_content = CCNode::create();
    m_content->setContentSize({kWidth, kHeight});
    m_content->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_content);
    std::error_code ec;
    if (std::filesystem::exists(welcomePath(), ec)) {
        m_skippedTutorial = true;
        m_step = static_cast<int>(steps().size());
        showTerms();
    } else {
        showStep();
    }
    return true;
}

void WelcomePopup::selectLanguage(std::string const& language) {
    Localization::get().setLanguage(Localization::languageFromId(language));
    showStep();
}

void WelcomePopup::toggleOption(std::string const& key) {
    auto it = m_pending.find(key);
    if (it != m_pending.end()) it->second = !it->second;
    else m_pending.emplace(key, !readOption(key));
    showStep();
}

void WelcomePopup::applyStep() {
    for (auto const& [key, value] : m_pending) {
        if (readOption(key) != value) writeOption(key, value);
    }
}

void WelcomePopup::nextStep() {
    ++m_step;
    m_pending.clear();
    if (m_step >= static_cast<int>(steps().size())) showTerms();
    else showStep();
}

void WelcomePopup::showStep() {
    m_content->removeAllChildren();
    auto const& step = steps().at(m_step);
    setTitle(localized(step.titleEs, step.titleEn).c_str());
    addText(m_content, fmt::format("{} / {}", m_step + 1, steps().size()), 22.f, 262.f, 90.f, 0.48f, true);
    addText(m_content, localized(step.hintEs, step.hintEn), 22.f, 239.f, 385.f, 0.53f);

    if (m_step == 0) {
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_content->addChild(menu);
        for (int i = 0; i < 2; ++i) {
            std::string language = i == 0 ? "spanish" : "english";
            std::string label = i == 0 ? "Espanol" : "English";
            bool selected = Localization::get().getCurrentLanguageId() == language;
            auto* item = button(label + (selected ? "  OK" : ""),
                selected ? "GJ_button_01.png" : "GJ_button_04.png",
                [this, language] { selectLanguage(language); }, 0.65f);
            item->setPosition({145.f + i * 150.f, 145.f});
            menu->addChild(item);
        }
    } else {
        auto* scroll = ScrollLayer::create({398.f, 164.f});
        scroll->setPosition({21.f, 62.f});
        m_content->addChild(scroll);
        std::vector<Option const*> visibleOptions;
        for (auto const& option : step.options) {
            if (std::string(option.key) == "menu-background-dark" ||
                std::string(option.key) == "bg-adaptive-colors" ||
                Mod::get()->hasSetting(option.key)) {
                visibleOptions.push_back(&option);
            }
        }
        float total = std::max(164.f, static_cast<float>(visibleOptions.size()) * 43.f + 8.f);
        scroll->m_contentLayer->setContentSize({398.f, total});
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        scroll->m_contentLayer->addChild(menu);
        for (size_t i = 0; i < visibleOptions.size(); ++i) {
            auto const& option = *visibleOptions[i];
            auto it = m_pending.try_emplace(option.key, readOption(option.key)).first;
            float y = total - 14.f - static_cast<float>(i) * 43.f;
            addText(scroll->m_contentLayer, localized(option.es, option.en), 8.f, y, 290.f, 0.52f, true);
            addText(scroll->m_contentLayer, localized(option.detailEs, option.detailEn), 8.f, y - 17.f, 300.f, 0.40f);
            auto* item = button(it->second ? localized("Si", "On") : localized("No", "Off"),
                it->second ? "GJ_button_01.png" : "GJ_button_06.png",
                [this, key = std::string(option.key)] { toggleOption(key); }, 0.47f);
            item->setPosition({350.f, y - 10.f});
            menu->addChild(item);
        }
        scroll->moveToTop();
        if (m_step == 1 || m_step == 3) {
            auto* detail = button(m_step == 1 ? localized("Editor de Fondos", "Background Editor")
                                                : localized("Editor de Cursor", "Cursor Editor"),
                "GJ_button_04.png", [this] {
                    applyStep();
                    m_pending.clear();
                    if (m_step == 1) PaiConfigLayer::openOverlay();
                    else if (auto* popup = CursorConfigPopup::create()) popup->show();
                }, 0.42f);
            detail->setPosition({345.f, 250.f});
            auto* detailMenu = CCMenu::create();
            detailMenu->setPosition({0.f, 0.f});
            detailMenu->addChild(detail);
            m_content->addChild(detailMenu);
        }
    }

    auto* footer = CCMenu::create();
    footer->setPosition({0.f, 0.f});
    m_content->addChild(footer);
    auto* skipAll = button(localized("Saltar tutorial", "Skip tutorial"), "GJ_button_06.png",
        [this] { m_skippedTutorial = true; m_step = static_cast<int>(steps().size()); showTerms(); }, 0.46f);
    skipAll->setPosition({72.f, 28.f});
    footer->addChild(skipAll);
    if (m_step > 0) {
        auto* skip = button(localized("Saltar seccion", "Skip section"), "GJ_button_04.png",
            [this] { nextStep(); }, 0.46f);
        skip->setPosition({218.f, 28.f});
        footer->addChild(skip);
    }
    auto* next = button(localized("Continuar", "Continue"), "GJ_button_01.png",
        [this] { applyStep(); nextStep(); }, 0.52f);
    next->setPosition({360.f, 28.f});
    footer->addChild(next);
}

void WelcomePopup::showTerms() {
    m_content->removeAllChildren();
    auto const& page = termsPages().at(m_termsPage);
    setTitle(localized("Terminos y condiciones", "Terms and conditions").c_str());
    addText(m_content, fmt::format("{} / {} - {}", m_termsPage + 1, termsPages().size(),
        localized(page.titleEs, page.titleEn)), 22.f, 260.f, 395.f, 0.53f, true);

    auto* scroll = ScrollLayer::create({396.f, 188.f});
    scroll->setPosition({22.f, 54.f});
    m_content->addChild(scroll);
    auto* body = SimpleTextArea::create(localized(page.bodyEs, page.bodyEn), "chatFont.fnt", 0.52f, 375.f);
    if (body) {
        float contentHeight = std::max(188.f, body->getContentSize().height + 16.f);
        scroll->m_contentLayer->setContentSize({396.f, contentHeight});
        body->setAnchorPoint({0.f, 1.f});
        body->setPosition({9.f, contentHeight - 6.f});
        scroll->m_contentLayer->addChild(body);
        scroll->moveToTop();
    }

    auto* footer = CCMenu::create();
    footer->setPosition({0.f, 0.f});
    m_content->addChild(footer);
    auto* decline = button(localized("Rechazar", "Decline"), "GJ_button_06.png",
        [this] { onClose(nullptr); }, 0.46f);
    decline->setPosition({70.f, 28.f});
    footer->addChild(decline);
    if (m_termsPage > 0) {
        auto* previous = button(localized("Anterior", "Previous"), "GJ_button_04.png",
            [this] { --m_termsPage; showTerms(); }, 0.46f);
        previous->setPosition({215.f, 28.f});
        footer->addChild(previous);
    }
    auto* next = button(m_termsPage + 1 == static_cast<int>(termsPages().size())
            ? localized("Acepto", "I accept") : localized("Siguiente", "Next"),
        "GJ_button_01.png", [this] {
            if (m_termsPage + 1 == static_cast<int>(termsPages().size())) finish();
            else { ++m_termsPage; showTerms(); }
        }, 0.52f);
    next->setPosition({360.f, 28.f});
    footer->addChild(next);
}

void WelcomePopup::finish() {
    auto data = matjson::makeObject({
        {"version", 1},
        {"accepted_terms_version", kTermsVersion},
        {"accepted_at", static_cast<int64_t>(std::time(nullptr))},
        {"language", Localization::get().getCurrentLanguageId()},
        {"tutorial_skipped", m_skippedTutorial},
    });
    auto result = geode::utils::file::writeStringSafe(welcomePath(), data.dump());
    if (!result) {
        Notification::create(localized("No se pudo guardar welcome.json", "Could not save welcome.json"),
            NotificationIcon::Error)->show();
        return;
    }
    onClose(nullptr);
    if (paimon::ban::runStartupBanGate()) return;
    paimon::discord::DiscordPresenceManager::get().resumeAfterConsent();
    paimon::thumbalerts::NewThumbWatcher::get().pollNow();
    paimon::thumbalerts::ThumbFeedSocket::get().resumeAfterConsent();
    paimon::forum::ForumApi::get().sendHeartbeat([](paimon::forum::Result<bool>) {});
    paimon::updates::UpdateChecker::get().checkAsync();
    paimon::crash::reportPendingCrashes();
}

}
