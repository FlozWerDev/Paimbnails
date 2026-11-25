#include "../../utils/FilesystemPath.hpp"
#include "WelcomeFlow.hpp"

#include "../../ui/PaimonUI.hpp"
#include "../../layers/PaiConfigLayer.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../../features/cursor/ui/CursorConfigPopup.hpp"
#include "../../features/cursor/services/CursorManager.hpp"
#endif
#include "../../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../../features/updates/services/UpdateChecker.hpp"
#include "../../features/crash-reports/services/CrashReporter.hpp"
#include "../../features/forum/services/ForumApi.hpp"
#include "../../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../../core/BanGate.hpp"
#include "../../features/thumb-alerts/services/NewThumbWatcher.hpp"
#include "../../features/thumb-alerts/services/ThumbFeedSocket.hpp"
#include "../../utils/Localization.hpp"
#include "../../utils/UnicodeLabel.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <functional>
#include <vector>

using namespace geode::prelude;

namespace paimon::onboarding {
namespace {

constexpr int kTermsVersion = 1;
constexpr float kWidth = 460.f;
constexpr float kHeight = 320.f;

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
#if defined(GEODE_IS_DESKTOP)
        {"Cursor", "Cursor", "Activa un cursor propio y abre su editor para elegir imagen y efectos.",
            "Enable a custom cursor and open its editor for images and effects.", {
                {"custom-cursor-enable", "Cursor personalizado", "Custom cursor", "Replaces the system cursor.", "Replaces the system cursor."},
                {"custom-slider-enabled", "Barra de desplazamiento propia", "Custom scroll thumb", "Personaliza los controles deslizantes.", "Customizes slider controls."},
                {"smooth-scroll", "Desplazamiento suave", "Smooth scrolling", "Suaviza el movimiento de listas.", "Smooths list movement."},
            }},
#else
        {"Controles", "Controls", "Personaliza las barras deslizantes de los menus y gestos tactiles.",
            "Customize menu sliders and touch controls.", {
                {"custom-slider-enabled", "Barra de desplazamiento propia", "Custom scroll thumb", "Personaliza los controles deslizantes.", "Customizes slider controls."},
            }},
#endif
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
                {"editor-animate-enable", "Paimon Animate", "Paimon Animate", "Anima frame a frame en el editor.", "Animate frame by frame in the editor."},
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
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "welcome.json";
}

// -1 unknown, 0 not accepted, 1 accepted. several startup paths ask before the
// menu exists; each answer used to re-read and re-parse welcome.json.
std::atomic<int>& acceptedCache() {
    static auto* value = new std::atomic<int>{-1};
    return *value;
}

void setAcceptedCache(bool accepted) {
    acceptedCache().store(accepted ? 1 : 0, std::memory_order_release);
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
#if defined(GEODE_IS_DESKTOP)
    } else if (key == "custom-cursor-enable") {
        auto& cursor = CursorManager::get();
        cursor.config().enabled = value;
        cursor.saveConfig();
        Mod::get()->setSettingValue<bool>(key, value);
        cursor.applyConfigLive();
#endif
    } else {
        Mod::get()->setSettingValue<bool>(key, value);
    }
}

// gold progress bar (track + fill) spanning the content width.
void addProgressBar(CCNode* parent, int current, int total, float y) {
    float const x = 22.f;
    float const w = kWidth - 44.f;
    float const frac = total > 0
        ? std::clamp(static_cast<float>(current) / static_cast<float>(total), 0.f, 1.f) : 0.f;
    auto* track = CCLayerColor::create({0, 0, 0, 110}, w, 6.f);
    track->ignoreAnchorPointForPosition(false);
    track->setAnchorPoint({0.f, 0.5f});
    track->setPosition({x, y});
    parent->addChild(track);
    if (frac > 0.f) {
        auto* fill = CCLayerColor::create({255, 210, 90, 255}, w * frac, 6.f);
        fill->ignoreAnchorPointForPosition(false);
        fill->setAnchorPoint({0.f, 0.5f});
        fill->setPosition({x, y});
        parent->addChild(fill);
    }
}

// header counter label ("Paso 3 / 11") at the left of the progress bar.
void addCounterLabel(CCNode* parent, std::string const& text, float y) {
    auto* label = paimon::ui::makeTitle(text.c_str(), 220.f, 0.45f);
    if (!label) return;
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({22.f, y});
    parent->addChild(label);
}

void addFooterDivider(CCNode* parent) {
    auto* divider = paimon::ui::makeDivider(kWidth - 44.f, paimon::ui::palette::gold, 80);
    if (!divider) return;
    divider->setPosition({kWidth / 2.f, 48.f});
    parent->addChild(divider);
}

CCMenuItemSpriteExtra* addFooterButton(CCMenu* menu, std::string const& label,
    paimon::ui::Btn skin, float x, std::function<void()> action) {
    auto* item = paimon::ui::makeButton(label.c_str(), std::move(action), skin, 126.f, 0.55f);
    if (!item) return nullptr;
    item->setPosition({x, 28.f});
    menu->addChild(item);
    return item;
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
    int cached = acceptedCache().load(std::memory_order_acquire);
    if (cached >= 0) return cached == 1;

    bool accepted = false;
    if (auto result = geode::utils::file::readString(paimon::paths::toSdkPath(welcomePath()))) {
        if (auto parsed = matjson::parse(result.unwrap())) {
            auto const& data = parsed.unwrap();
            accepted = data.isObject() && data.contains("accepted_terms_version") &&
                data["accepted_terms_version"].asInt().unwrapOr(0) == kTermsVersion;
        }
    }

    // only publish if nobody else did: finish() writing the file concurrently
    // must win over a stale "not accepted" read from an older worker.
    int expected = -1;
    acceptedCache().compare_exchange_strong(expected, accepted ? 1 : 0,
        std::memory_order_acq_rel, std::memory_order_acquire);
    return acceptedCache().load(std::memory_order_acquire) == 1;
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
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    m_content = CCNode::create();
    m_content->setContentSize({kWidth, kHeight});
    m_content->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_content);
    addCorners();
    addInfoButton(localized("Bienvenido a Paimbnails", "Welcome to Paimbnails"),
        localized("Este asistente configura las funciones principales del mod paso a paso.\n"
                  "<cy>Saltar</c> deja cada opcion como esta; puedes cambiar todo despues en los ajustes.\n"
                  "Al final se muestran los <cy>terminos</c>: aceptarlos es obligatorio para usar el mod.",
                  "This assistant sets up the mod's main features step by step.\n"
                  "<cy>Skipping</c> leaves each option as it is; you can change everything later in settings.\n"
                  "The <cy>terms</c> are shown at the end: accepting them is required to use the mod."));
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

    addCounterLabel(m_content,
        fmt::format("{} {} / {}", localized("Paso", "Step"), m_step + 1, steps().size()),
        kHeight - 48.f);
    addProgressBar(m_content, m_step + 1, static_cast<int>(steps().size()), kHeight - 62.f);

    auto* hint = paimon::ui::makeText(localized(step.hintEs, step.hintEn).c_str(),
        kWidth - 44.f, 0.5f, paimon::ui::palette::muted);
    if (hint) {
        hint->setAnchorPoint({0.f, 1.f});
        hint->setPosition({22.f, kHeight - 72.f});
        m_content->addChild(hint);
    }

    // editor shortcut (backgrounds / cursor steps) in the header row.
#if defined(GEODE_IS_DESKTOP)
    bool const showStepEditor = (m_step == 1 || m_step == 3);
#else
    bool const showStepEditor = (m_step == 1);
#endif
    if (showStepEditor) {
        auto* detailMenu = paimon::ui::makeZeroMenu();
        m_content->addChild(detailMenu);
        auto* detail = paimon::ui::makeButton(
            (m_step == 1 ? localized("Editor de Fondos", "Background Editor")
                         : localized("Editor de Cursor", "Cursor Editor")).c_str(),
            [this] {
                applyStep();
                m_pending.clear();
                if (m_step == 1) PaiConfigLayer::openOverlay();
#if defined(GEODE_IS_DESKTOP)
                else if (auto* popup = CursorConfigPopup::create()) popup->show();
#endif
            }, paimon::ui::Btn::Cyan, 126.f, 0.5f);
        if (detail) {
            detail->setPosition({kWidth - 85.f, kHeight - 48.f});
            detailMenu->addChild(detail);
        }
    }

    constexpr float panelW = kWidth - 36.f;
    constexpr float panelH = 152.f;
    auto* panel = paimon::ui::makePanel({panelW, panelH},
        step.options.empty() ? nullptr : localized("Opciones", "Options").c_str());
    panel->setPosition({18.f, 54.f});
    m_content->addChild(panel);
    paimon::ui::animateIn(panel);

    if (m_step == 0) {
        auto* menu = paimon::ui::makeZeroMenu();
        panel->addChild(menu);
        for (int i = 0; i < 2; ++i) {
            std::string language = i == 0 ? "spanish" : "english";
            std::string label = Localization::languageDisplayName(Localization::languageFromId(language));
            bool selected = Localization::get().getCurrentLanguageId() == language;
            auto* item = paimon::ui::makeButton("",
                [this, language] { selectLanguage(language); },
                selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray, 150.f, 0.6f);
            if (!item) continue;
            auto* face = item->getNormalImage();
            auto* caption = paimon::ui::createUnicodeLabel(label + (selected ? "  OK" : ""), "goldFont.fnt");
            if (caption) {
                caption->setLimitLabelWidth(face->getContentWidth() - 14.f, .8f, .4f);
                caption->setPosition(face->getContentSize() / 2.f);
                face->addChild(caption, 2);
            }
            item->setPosition({panelW / 2.f + (i == 0 ? -84.f : 84.f), panelH / 2.f});
            menu->addChild(item);
        }
    } else {
        float const scrollW = panelW - 16.f;
        float const scrollH = panelH - paimon::ui::kPanelHeader - 14.f;
        auto* scroll = ScrollLayer::create({scrollW, scrollH});
        if (scroll) scroll->setAnchorPoint({0.f, 0.f});
        scroll->setPosition({8.f, 7.f});
        panel->addChild(scroll);
        std::vector<Option const*> visibleOptions;
        for (auto const& option : step.options) {
            if (std::string(option.key) == "menu-background-dark" ||
                std::string(option.key) == "bg-adaptive-colors" ||
                Mod::get()->hasSetting(option.key)) {
                visibleOptions.push_back(&option);
            }
        }
        float total = std::max(scrollH, static_cast<float>(visibleOptions.size()) * 44.f + 8.f);
        scroll->m_contentLayer->setContentSize({scrollW, total});
        auto* menu = paimon::ui::makeZeroMenu();
        scroll->m_contentLayer->addChild(menu);
        for (size_t i = 0; i < visibleOptions.size(); ++i) {
            auto const& option = *visibleOptions[i];
            auto it = m_pending.try_emplace(option.key, readOption(option.key)).first;
            float y = total - 14.f - static_cast<float>(i) * 44.f;
            if (i > 0) {
                auto* divider = paimon::ui::makeDivider(scrollW - 12.f, paimon::ui::palette::gold, 55);
                if (divider) {
                    divider->setPosition({scrollW / 2.f, y + 22.f});
                    scroll->m_contentLayer->addChild(divider);
                }
            }
            addText(scroll->m_contentLayer, localized(option.es, option.en), 8.f, y, 290.f, 0.52f, true);
            addText(scroll->m_contentLayer, localized(option.detailEs, option.detailEn), 8.f, y - 17.f, 300.f, 0.40f);
            // ignore the toggler's reported state; toggleOption flips m_pending and rebuilds.
            auto* item = paimon::ui::makeToggle(it->second,
                [this, key = std::string(option.key)](bool) { toggleOption(key); }, 0.7f);
            item->setPosition({scrollW - 28.f, y - 10.f});
            menu->addChild(item);
        }
        scroll->moveToTop();
    }

    addFooterDivider(m_content);
    auto* footer = paimon::ui::makeZeroMenu();
    m_content->addChild(footer);
    addFooterButton(footer, localized("Saltar tutorial", "Skip tutorial"), paimon::ui::Btn::Gray, 84.f,
        [this] { m_skippedTutorial = true; m_step = static_cast<int>(steps().size()); showTerms(); });
    if (m_step > 0) {
        addFooterButton(footer, localized("Saltar seccion", "Skip section"), paimon::ui::Btn::Gray, kWidth / 2.f,
            [this] { nextStep(); });
    }
    addFooterButton(footer, localized("Continuar", "Continue"), paimon::ui::Btn::Green, kWidth - 84.f,
        [this] { applyStep(); nextStep(); });
}

void WelcomePopup::showTerms() {
    m_content->removeAllChildren();
    auto const& page = termsPages().at(m_termsPage);
    setTitle(localized("Terminos y condiciones", "Terms and conditions").c_str());

    addCounterLabel(m_content,
        fmt::format("{} {} / {}", localized("Pagina", "Page"), m_termsPage + 1, termsPages().size()),
        kHeight - 48.f);
    addProgressBar(m_content, m_termsPage + 1, static_cast<int>(termsPages().size()), kHeight - 62.f);

    constexpr float panelW = kWidth - 36.f;
    constexpr float panelH = 178.f;
    auto* panel = paimon::ui::makePanel({panelW, panelH},
        localized(page.titleEs, page.titleEn).c_str());
    panel->setPosition({18.f, 54.f});
    m_content->addChild(panel);
    paimon::ui::animateIn(panel);

    float const scrollW = panelW - 16.f;
    float const scrollH = panelH - paimon::ui::kPanelHeader - 14.f;
    auto* scroll = ScrollLayer::create({scrollW, scrollH});
    if (scroll) scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({8.f, 7.f});
    panel->addChild(scroll);
    auto* body = SimpleTextArea::create(localized(page.bodyEs, page.bodyEn), "chatFont.fnt", 0.52f, scrollW - 18.f);
    if (body) {
        float contentHeight = std::max(scrollH, body->getContentSize().height + 16.f);
        scroll->m_contentLayer->setContentSize({scrollW, contentHeight});
        body->setAnchorPoint({0.f, 1.f});
        body->setPosition({9.f, contentHeight - 6.f});
        scroll->m_contentLayer->addChild(body);
        scroll->moveToTop();
    }

    addFooterDivider(m_content);
    auto* footer = paimon::ui::makeZeroMenu();
    m_content->addChild(footer);
    addFooterButton(footer, localized("Rechazar", "Decline"), paimon::ui::Btn::Red, 84.f,
        [this] { onClose(nullptr); });
    if (m_termsPage > 0) {
        addFooterButton(footer, localized("Anterior", "Previous"), paimon::ui::Btn::Gray, kWidth / 2.f,
            [this] { --m_termsPage; showTerms(); });
    }
    bool const lastPage = m_termsPage + 1 == static_cast<int>(termsPages().size());
    addFooterButton(footer, lastPage ? localized("Acepto", "I accept") : localized("Siguiente", "Next"),
        paimon::ui::Btn::Green, kWidth - 84.f, [this, lastPage] {
            if (lastPage) finish();
            else { ++m_termsPage; showTerms(); }
        });
}

void WelcomePopup::finish() {
    auto data = matjson::makeObject({
        {"version", 1},
        {"accepted_terms_version", kTermsVersion},
        {"accepted_at", static_cast<int64_t>(std::time(nullptr))},
        {"language", Localization::get().getCurrentLanguageId()},
        {"tutorial_skipped", m_skippedTutorial},
    });
    auto result = geode::utils::file::writeStringSafe(paimon::paths::toSdkPath(welcomePath()), data.dump());
    if (!result) {
        Notification::create(localized("No se pudo guardar welcome.json", "Could not save welcome.json"),
            NotificationIcon::Error)->show();
        return;
    }
    setAcceptedCache(true);
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
