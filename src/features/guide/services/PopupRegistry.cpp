#include "PopupRegistry.hpp"

#include "../../../utils/Localization.hpp"
#include "../ui/PaimonGuideChatPopup.hpp"

// Popups que abrimos como acciones. Cada include corresponde a un popup
// real del mod. Si en el futuro se mueve algun popup, basta con tocar
// estos includes y la lambda open() correspondiente.
#include "../../cursor/ui/CursorConfigPopup.hpp"
#include "../../discord-presence/ui/DiscordConfigPopup.hpp"
#include "../../pet/ui/PetConfigPopup.hpp"
#include "../../transitions/ui/TransitionConfigPopup.hpp"
#include "../../profiles/ui/ProfilePicEditorPopup.hpp"
#include "../../menu-music/ui/MenuMusicPopup.hpp"
#include "../../menu-music/ui/MenuMusicLibraryPopup.hpp"
#include "../../menu-music/ui/MenuMusicPlaylistsPopup.hpp"
#include "../../quick-hub/ui/RadialConfigPopup.hpp"
#include "../../thumbnails/ui/ThumbnailSettingsPopup.hpp"
#include "../../thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../../progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../../visuals/ui/ExtraEffectsPopup.hpp"
#include "../../volume-scroll/ui/ScrollKeybindsPopup.hpp"
#include "../../../layers/PaiConfigLayer.hpp"
#include "../../../layers/PaimonHubLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

namespace paimon::guide {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

template <typename PopupT>
std::function<void(PaimonGuideChatPopup*)> openSimple() {
    return [](PaimonGuideChatPopup*) {
        if (auto* popup = PopupT::create()) popup->show();
    };
}

std::function<void(PaimonGuideChatPopup*)> openPaiConfig() {
    return [](PaimonGuideChatPopup*) {
        if (auto* scene = PaiConfigLayer::scene()) {
            CCDirector::get()->pushScene(scene);
        }
    };
}

std::function<void(PaimonGuideChatPopup*)> openHub() {
    return [](PaimonGuideChatPopup*) {
        if (auto* scene = PaimonHubLayer::scene()) {
            CCDirector::get()->pushScene(scene);
        }
    };
}

std::function<void(PaimonGuideChatPopup*)> openGeodeSettings() {
    return [](PaimonGuideChatPopup*) {
        geode::openSettingsPopup(geode::Mod::get());
    };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

PopupRegistry& PopupRegistry::get() {
    static PopupRegistry instance;
    return instance;
}

PopupRegistry::PopupRegistry() {
    registerAll();
}

void PopupRegistry::rebuild() {
    m_entries.clear();
    registerAll();
}

void PopupRegistry::registerAll() {
    // ── Profiles ─────────────────────────────────────────────────────────
    {
        // Este es EL caso del usuario: "profile background" debe abrir el
        // ProfileBgPickerPopup, cuyo titulo real es "Profile Background"
        // (en) / "Fondo de Perfil" (es). Como requiere accountID, no
        // podemos abrirlo directo desde aqui — solo describimos y mandamos
        // al editor de perfil.
        PopupEntry e;
        e.id = "profile-background";
        e.category = PopupCategory::Profile;
        e.weight = 130;  // alta porque es la entidad mas explicita
        e.displayNameByLang["english"] = "Profile Background";
        e.displayNameByLang["spanish"] = "Fondo de Perfil";
        e.aliasesByLang["english"]     = {"profile bg", "profile wallpaper", "pfp background"};
        e.aliasesByLang["spanish"]     = {"fondo perfil", "wallpaper perfil", "fondo del perfil"};
        e.descriptionByLang["english"] =
            "<cy>Profile Background!</c> Pick the background that appears on your profile. "
            "Open your <cy>profile photo editor</c> first; the option is in there.";
        e.descriptionByLang["spanish"] =
            "<cy>Fondo de Perfil!</c> Elige el fondo que aparece en tu perfil. "
            "Abre primero el <cy>editor de foto de perfil</c>; la opcion esta ahi.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "profile-photo-editor";
        e.category = PopupCategory::Profile;
        e.weight = 120;
        e.displayNameByLang["english"] = "Profile Photo Editor";
        e.displayNameByLang["spanish"] = "Editor de Foto de Perfil";
        e.aliasesByLang["english"]     = {"pfp", "avatar", "profile picture", "profile pic"};
        e.aliasesByLang["spanish"]     = {"foto de perfil", "avatar", "imagen de perfil"};
        e.descriptionByLang["english"] =
            "<cy>Profile Photo Editor!</c> Pick your profile picture, shape, badge, and more.";
        e.descriptionByLang["spanish"] =
            "<cy>Editor de Foto de Perfil!</c> Elige tu foto, forma, badge y mas.";
        e.open = openSimple<ProfilePicEditorPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "profile-settings";
        e.category = PopupCategory::Profile;
        e.weight = 115;
        e.displayNameByLang["english"] = "Profile Settings";
        e.displayNameByLang["spanish"] = "Ajustes de Perfil";
        e.aliasesByLang["english"]     = {"profile config", "configure profile"};
        e.aliasesByLang["spanish"]     = {"configuracion de perfil", "ajustes perfil"};
        e.descriptionByLang["english"] =
            "<cy>Profile Settings!</c> Privacy, music, badges and other profile-wide options. "
            "Open your <cy>profile photo editor</c> and tap the gear icon.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes de Perfil!</c> Privacidad, musica, badges y demas opciones del perfil. "
            "Abre el <cy>editor de foto de perfil</c> y toca el engranaje.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "profile-music";
        e.category = PopupCategory::Profile;
        e.weight = 115;
        e.displayNameByLang["english"] = "Profile Music";
        e.displayNameByLang["spanish"] = "Musica de Perfil";
        e.aliasesByLang["english"]     = {"profile song"};
        e.aliasesByLang["spanish"]     = {"cancion de perfil", "musica del perfil"};
        e.descriptionByLang["english"] =
            "<cy>Profile Music!</c> The song that plays when someone visits your profile. "
            "Open <cy>Profile Settings</c> first.";
        e.descriptionByLang["spanish"] =
            "<cy>Musica de Perfil!</c> La cancion que suena cuando alguien visita tu perfil. "
            "Abre primero los <cy>Ajustes de Perfil</c>.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }
    {
        // Estos popups requieren accountID + ProfileConfig, no se pueden
        // abrir desde aqui. Solo se describen.
        PopupEntry e;
        e.id = "comment-background";
        e.category = PopupCategory::Profile;
        e.weight = 100;
        e.displayNameByLang["english"] = "Comment Background";
        e.displayNameByLang["spanish"] = "Fondo de Comentarios";
        e.aliasesByLang["english"]     = {"comments bg", "comments wallpaper"};
        e.aliasesByLang["spanish"]     = {"fondo comentarios"};
        e.descriptionByLang["english"] =
            "<cy>Comment Background!</c> Customize the background of your profile comments. "
            "Open it from your <cy>profile photo editor</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Fondo de Comentarios!</c> Personaliza el fondo de los comentarios. "
            "Abrelo desde el <cy>editor de foto de perfil</c>.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "custom-badge";
        e.category = PopupCategory::Profile;
        e.weight = 95;
        e.displayNameByLang["english"] = "Custom Badge";
        e.displayNameByLang["spanish"] = "Badge Personalizado";
        e.aliasesByLang["english"]     = {"profile badge", "user badge"};
        e.aliasesByLang["spanish"]     = {"badge perfil", "insignia"};
        e.descriptionByLang["english"] =
            "<cy>Custom Badge!</c> Pick a badge icon that shows next to your name. "
            "Open it from your <cy>profile photo editor</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Badge Personalizado!</c> Elige un icono que aparece al lado de tu nombre. "
            "Abrelo desde el <cy>editor de foto de perfil</c>.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "profile-reviews";
        e.category = PopupCategory::Profile;
        e.weight = 95;
        e.displayNameByLang["english"] = "Profile Reviews";
        e.displayNameByLang["spanish"] = "Reseñas de Perfil";
        e.aliasesByLang["english"]     = {"reviews", "ratings", "feedback"};
        e.aliasesByLang["spanish"]     = {"resenas", "valoraciones"};
        e.descriptionByLang["english"] =
            "<cy>Profile Reviews!</c> See and write reviews on profiles. "
            "Open a profile and tap the reviews icon.";
        e.descriptionByLang["spanish"] =
            "<cy>Reseñas de Perfil!</c> Mira y escribe reseñas en perfiles. "
            "Abre un perfil y toca el icono de reseñas.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "profile-views";
        e.category = PopupCategory::Profile;
        e.weight = 95;
        e.displayNameByLang["english"] = "Profile Views";
        e.displayNameByLang["spanish"] = "Visitas de Perfil";
        e.aliasesByLang["english"]     = {"visitors", "who viewed"};
        e.aliasesByLang["spanish"]     = {"visitas", "quien me visito"};
        e.descriptionByLang["english"] =
            "<cy>Profile Views!</c> See who visited your profile. "
            "Open your own profile and tap the views icon.";
        e.descriptionByLang["spanish"] =
            "<cy>Visitas de Perfil!</c> Mira quien visito tu perfil. "
            "Abre tu propio perfil y toca el icono de visitas.";
        // Sin accion: requiere accountID
        m_entries.push_back(std::move(e));
    }

    // ── Backgrounds (general, NO el de profile) ─────────────────────────
    {
        PopupEntry e;
        e.id = "scene-background";
        e.category = PopupCategory::Background;
        e.weight = 70; // peso menor que profile-background para que ese gane
                       // si la query menciona "profile"
        e.displayNameByLang["english"] = "Scene Background";
        e.displayNameByLang["spanish"] = "Fondo de Escena";
        e.aliasesByLang["english"]     = {
            "background", "backgrounds", "wallpaper", "scene wallpaper",
            "menu background", "search background", "level select background",
            "background config", "bg"
        };
        e.aliasesByLang["spanish"]     = {
            "fondo", "fondos", "wallpaper", "escenario", "fondo menu",
            "fondo busqueda", "fondo seleccion", "configurar fondo"
        };
        e.descriptionByLang["english"] =
            "<cy>Scene Background!</c> Configure the per-screen background "
            "(menu, search, gauntlet, level select). Images, gradients, video, shaders.";
        e.descriptionByLang["spanish"] =
            "<cy>Fondo de Escena!</c> Configura el fondo por pantalla "
            "(menu, busqueda, gauntlet, level select). Imagenes, gradientes, video, shaders.";
        // Apunta al editor FULL (PaiConfigLayer en tab Backgrounds), no al popup
        // BackgroundConfigPopup mas chico. PaiConfigLayer arranca en m_currentMainTab=0
        // por defecto (switchMainTab(0) en init), que es el tab Backgrounds.
        e.open = openPaiConfig();
        m_entries.push_back(std::move(e));
    }

    // ── Music ───────────────────────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "menu-music";
        e.category = PopupCategory::Music;
        e.weight = 100;
        e.displayNameByLang["english"] = "Menu Music";
        e.displayNameByLang["spanish"] = "Musica del Menu";
        e.aliasesByLang["english"]     = {
            "menu song", "menuloop", "menu loop", "vinyl",
            "main menu music", "main menu song"
        };
        e.aliasesByLang["spanish"]     = {
            "musica menu", "cancion menu", "vinilo", "menuloop",
            "musica principal"
        };
        e.descriptionByLang["english"] =
            "<cy>Menu Music!</c> Library, playlists and downloads for the music in the main menu.";
        e.descriptionByLang["spanish"] =
            "<cy>Musica del Menu!</c> Biblioteca, playlists y descargas para la musica del menu.";
        e.open = openSimple<paimon::menumusic::MenuMusicPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "music-library";
        e.category = PopupCategory::Music;
        e.weight = 90;
        e.displayNameByLang["english"] = "Music Library";
        e.displayNameByLang["spanish"] = "Biblioteca de Musica";
        e.aliasesByLang["english"]     = {"library", "song library", "my songs"};
        e.aliasesByLang["spanish"]     = {"biblioteca", "mis canciones"};
        e.descriptionByLang["english"] =
            "<cy>Music Library!</c> All your downloaded songs in one place.";
        e.descriptionByLang["spanish"] =
            "<cy>Biblioteca de Musica!</c> Todas tus canciones descargadas en un solo lugar.";
        e.open = openSimple<paimon::menumusic::MenuMusicLibraryPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "music-playlists";
        e.category = PopupCategory::Music;
        e.weight = 90;
        e.displayNameByLang["english"] = "Music Playlists";
        e.displayNameByLang["spanish"] = "Playlists de Musica";
        e.aliasesByLang["english"]     = {"playlists", "playlist", "song list"};
        e.aliasesByLang["spanish"]     = {"playlists", "lista canciones", "playlist"};
        e.descriptionByLang["english"] =
            "<cy>Music Playlists!</c> Create and manage your menu music playlists.";
        e.descriptionByLang["spanish"] =
            "<cy>Playlists de Musica!</c> Crea y gestiona tus playlists de musica.";
        e.open = openSimple<paimon::menumusic::MenuMusicPlaylistsPopup>();
        m_entries.push_back(std::move(e));
    }

    // ── Visual / customization ──────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "custom-cursor";
        e.category = PopupCategory::Cursor;
        e.weight = 95;
        e.displayNameByLang["english"] = "Custom Cursor";
        e.displayNameByLang["spanish"] = "Cursor Personalizado";
        e.aliasesByLang["english"]     = {"cursor", "mouse pointer", "pointer", "mouse"};
        e.aliasesByLang["spanish"]     = {"cursor", "raton", "puntero", "mouse"};
        e.descriptionByLang["english"] =
            "<cy>Custom Cursor!</c> Replace the OS cursor with your own image.";
        e.descriptionByLang["spanish"] =
            "<cy>Cursor Personalizado!</c> Reemplaza el cursor del sistema con tu propia imagen.";
        e.open = openSimple<CursorConfigPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "discord-rich-presence";
        e.category = PopupCategory::Discord;
        e.weight = 95;
        e.displayNameByLang["english"] = "Discord Rich Presence";
        e.displayNameByLang["spanish"] = "Discord Rich Presence";
        e.aliasesByLang["english"]     = {"discord", "rpc", "rich presence", "presence", "status"};
        e.aliasesByLang["spanish"]     = {"discord", "rpc", "presencia", "estado"};
        e.descriptionByLang["english"] =
            "<cy>Discord Rich Presence!</c> Show what you're playing in Discord.";
        e.descriptionByLang["spanish"] =
            "<cy>Discord Rich Presence!</c> Muestra a que estas jugando en Discord.";
        e.open = openSimple<paimon::discord::DiscordConfigPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "pet";
        e.category = PopupCategory::Pet;
        e.weight = 90;
        e.displayNameByLang["english"] = "Pet / Mascot";
        e.displayNameByLang["spanish"] = "Mascota";
        e.aliasesByLang["english"]     = {"pet", "mascot", "companion", "fish"};
        e.aliasesByLang["spanish"]     = {"mascota", "pet", "compañero", "pez"};
        e.descriptionByLang["english"] =
            "<cy>Pet / Mascot!</c> Pick a Paimon-style companion that follows you.";
        e.descriptionByLang["spanish"] =
            "<cy>Mascota!</c> Elige un companero estilo Paimon que te sigue.";
        e.open = openSimple<PetConfigPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "transition-settings";
        e.category = PopupCategory::Transition;
        e.weight = 75;
        e.displayNameByLang["english"] = "Transition Settings";
        e.displayNameByLang["spanish"] = "Ajustes de Transiciones";
        e.aliasesByLang["english"]     = {"transition", "transitions", "popup animation", "scene transition"};
        e.aliasesByLang["spanish"]     = {"transicion", "transiciones", "animacion popup"};
        e.descriptionByLang["english"] =
            "<cy>Transition Settings!</c> How popups and screens animate.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes de Transiciones!</c> Como se animan popups y pantallas.";
        e.open = openSimple<TransitionConfigPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "extra-effects";
        e.category = PopupCategory::None;
        e.weight = 70;
        e.displayNameByLang["english"] = "Extra Effects";
        e.displayNameByLang["spanish"] = "Efectos Extra";
        e.aliasesByLang["english"]     = {"effects", "shaders", "visual effects", "fx"};
        e.aliasesByLang["spanish"]     = {"efectos", "shaders", "fx"};
        e.descriptionByLang["english"] =
            "<cy>Extra Effects!</c> Optional visual effects layered on top of GD.";
        e.descriptionByLang["spanish"] =
            "<cy>Efectos Extra!</c> Efectos visuales opcionales sobre GD.";
        e.open = openSimple<ExtraEffectsPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "progress-bar";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "Custom Progress Bar";
        e.displayNameByLang["spanish"] = "Barra de Progreso Personalizada";
        e.aliasesByLang["english"]     = {"progress bar", "progressbar", "loading bar"};
        e.aliasesByLang["spanish"]     = {"barra de progreso", "barra progreso", "barra de carga"};
        e.descriptionByLang["english"] =
            "<cy>Custom Progress Bar!</c> Style the in-game progress bar.";
        e.descriptionByLang["spanish"] =
            "<cy>Barra de Progreso Personalizada!</c> Personaliza la barra de progreso del juego.";
        e.open = openSimple<ProgressBarConfigPopup>();
        m_entries.push_back(std::move(e));
    }

    // ── Quick Hub ───────────────────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "quick-hub";
        e.category = PopupCategory::QuickHub;
        e.weight = 95;
        e.displayNameByLang["english"] = "Quick Hub";
        e.displayNameByLang["spanish"] = "Quick Hub";
        e.aliasesByLang["english"]     = {"qh", "radial menu", "wheel menu", "shortcut wheel"};
        e.aliasesByLang["spanish"]     = {"qh", "menu radial", "rueda atajos"};
        e.descriptionByLang["english"] =
            "<cy>Quick Hub!</c> A radial wheel of shortcuts.";
        e.descriptionByLang["spanish"] =
            "<cy>Quick Hub!</c> Una rueda radial de atajos.";
        e.open = openSimple<paimon::quickhub::RadialConfigPopup>();
        m_entries.push_back(std::move(e));
    }

    // ── Thumbnails ──────────────────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "thumbnail-settings";
        e.category = PopupCategory::Thumbnail;
        e.weight = 90;
        e.displayNameByLang["english"] = "Thumbnail Settings";
        e.displayNameByLang["spanish"] = "Ajustes de Miniaturas";
        e.aliasesByLang["english"]     = {"thumbnail", "thumbnails", "thumbs", "preview"};
        e.aliasesByLang["spanish"]     = {"miniatura", "miniaturas", "preview"};
        e.descriptionByLang["english"] =
            "<cy>Thumbnail Settings!</c> Configure how thumbnails behave in the level lists.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes de Miniaturas!</c> Configura como se comportan las miniaturas en las listas.";
        e.open = openSimple<ThumbnailSettingsPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "thumbnail-order";
        e.category = PopupCategory::Thumbnail;
        e.weight = 85;
        e.displayNameByLang["english"] = "Thumbnail Order";
        e.displayNameByLang["spanish"] = "Orden de Miniaturas";
        e.aliasesByLang["english"]     = {"thumbnail order", "thumb order", "sort thumbnails"};
        e.aliasesByLang["spanish"]     = {"orden miniaturas", "ordenar miniaturas"};
        e.descriptionByLang["english"] =
            "<cy>Thumbnail Order!</c> Reorder the thumbnails of a level. "
            "Open a level and tap the thumbnail order icon.";
        e.descriptionByLang["spanish"] =
            "<cy>Orden de Miniaturas!</c> Reordena las miniaturas de un nivel. "
            "Abre un nivel y toca el icono de orden.";
        // Sin accion: requiere levelID + thumbnails
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "level-cell-settings";
        e.category = PopupCategory::Thumbnail;
        e.weight = 85;
        e.displayNameByLang["english"] = "LevelCell Settings";
        e.displayNameByLang["spanish"] = "Ajustes de Lista de Niveles";
        e.aliasesByLang["english"]     = {"level cell", "levelcell", "list settings", "level list"};
        e.aliasesByLang["spanish"]     = {"lista niveles", "lista de niveles"};
        e.descriptionByLang["english"] =
            "<cy>LevelCell Settings!</c> How level cells render in browsers.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes de Lista de Niveles!</c> Como se renderizan las celdas en los browsers.";
        e.open = openSimple<LevelCellSettingsPopup>();
        m_entries.push_back(std::move(e));
    }

    // ── Volume / Keybinds ───────────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "scroll-keybinds";
        e.category = PopupCategory::Volume;
        e.weight = 85;
        e.displayNameByLang["english"] = "Scroll Keybinds";
        e.displayNameByLang["spanish"] = "Atajos de Teclado";
        e.aliasesByLang["english"]     = {"volume", "scroll volume", "music volume", "sfx volume", "keybinds"};
        e.aliasesByLang["spanish"]     = {"volumen", "scroll volumen", "subir volumen", "bajar volumen", "atajos teclado"};
        e.descriptionByLang["english"] =
            "<cy>Scroll Keybinds!</c> Bind keys for volume scroll and other shortcuts.";
        e.descriptionByLang["spanish"] =
            "<cy>Atajos de Teclado!</c> Configura las teclas para scroll de volumen y otros atajos.";
        e.open = openSimple<paimon::volscroll::ScrollKeybindsPopup>();
        m_entries.push_back(std::move(e));
    }

    // ── For You preferences ─────────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "foryou-preferences";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "For You Preferences";
        e.displayNameByLang["spanish"] = "Preferencias Para Ti";
        e.aliasesByLang["english"]     = {"for you", "foryou", "feed", "recommendations", "recommended"};
        e.aliasesByLang["spanish"]     = {"para ti", "feed", "recomendaciones", "recomendados"};
        e.descriptionByLang["english"] =
            "<cy>For You Preferences!</c> Tune the feed of recommended content. "
            "Find it in the For You section of the Hub.";
        e.descriptionByLang["spanish"] =
            "<cy>Preferencias Para Ti!</c> Ajusta el feed de contenido recomendado. "
            "Encuentralo en la seccion Para Ti del Hub.";
        // Sin accion: requiere callback onConfirm
        m_entries.push_back(std::move(e));
    }

    // ── Layers (PaiConfig, Hub) ─────────────────────────────────────────
    {
        PopupEntry e;
        e.id = "paiconfig";
        e.category = PopupCategory::Cache;
        e.weight = 90;
        e.displayNameByLang["english"] = "PaiConfig";
        e.displayNameByLang["spanish"] = "PaiConfig";
        e.aliasesByLang["english"]     = {
            "paiconfig", "settings", "config", "extras", "cache",
            "clear cache", "delete cache"
        };
        e.aliasesByLang["spanish"]     = {
            "paiconfig", "ajustes", "config", "extras", "cache",
            "limpiar cache", "borrar cache"
        };
        e.descriptionByLang["english"] =
            "<cy>PaiConfig!</c> The big settings layer with Extras (cache, language, ...).";
        e.descriptionByLang["spanish"] =
            "<cy>PaiConfig!</c> El layer grande de ajustes con Extras (cache, idioma, ...).";
        e.open = openPaiConfig();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "hub";
        e.category = PopupCategory::Forum;
        e.weight = 85;
        e.displayNameByLang["english"] = "Paimon Hub";
        e.displayNameByLang["spanish"] = "Paimon Hub";
        e.aliasesByLang["english"]     = {
            "hub", "forum", "community", "news", "posts",
            "paimon hub"
        };
        e.aliasesByLang["spanish"]     = {
            "hub", "foro", "comunidad", "noticias", "publicaciones"
        };
        e.descriptionByLang["english"] =
            "<cy>Paimon Hub!</c> Forum, news, and community features.";
        e.descriptionByLang["spanish"] =
            "<cy>Paimon Hub!</c> Foro, noticias y features de comunidad.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }

    // ── Geode settings (capture, language, volume keybinds, updates) ────
    {
        PopupEntry e;
        e.id = "geode-settings";
        e.category = PopupCategory::None;
        e.weight = 60;
        e.displayNameByLang["english"] = "Mod Settings";
        e.displayNameByLang["spanish"] = "Ajustes del Mod";
        e.aliasesByLang["english"]     = {
            "settings", "preferences", "options", "language", "translate",
            "capture", "screenshot", "snap", "thumbnail capture",
            "update", "version"
        };
        e.aliasesByLang["spanish"]     = {
            "ajustes", "preferencias", "opciones", "idioma", "lenguaje",
            "capturar", "captura", "screenshot",
            "actualizar", "actualizacion", "version"
        };
        e.descriptionByLang["english"] =
            "<cy>Mod Settings!</c> Capture keybinds, language, updates, and other Geode settings.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes del Mod!</c> Atajos de captura, idioma, actualizaciones y demas settings de Geode.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// toIntent
// ─────────────────────────────────────────────────────────────────────────────

GuideIntent PopupRegistry::toIntent(PopupEntry const& entry) {
    GuideIntent intent;
    intent.id = entry.id;
    intent.kind = IntentKind::Functional;
    intent.priority = 50;
    intent.weight = entry.weight;
    intent.animation = entry.animation;

    // Las "keywords" del intent son: [displayName + aliases] por idioma.
    // El displayName actua como la keyword PRIMARIA (la mas autoritativa
    // porque es lo que el usuario ve en pantalla).
    auto buildList = [&](std::string const& lang) {
        std::vector<std::string> kws;
        auto dnIt = entry.displayNameByLang.find(lang);
        if (dnIt != entry.displayNameByLang.end()) {
            kws.push_back(dnIt->second);
        }
        auto alIt = entry.aliasesByLang.find(lang);
        if (alIt != entry.aliasesByLang.end()) {
            for (auto const& alias : alIt->second) kws.push_back(alias);
        }
        return kws;
    };

    intent.keywordsByLang["english"] = buildList("english");
    intent.keywordsByLang["spanish"] = buildList("spanish");

    // Respuesta = description del entry. Localization fallback a english.
    if (entry.descriptionByLang.count("english")) {
        intent.responseByLang["english"] = entry.descriptionByLang.at("english");
    }
    if (entry.descriptionByLang.count("spanish")) {
        intent.responseByLang["spanish"] = entry.descriptionByLang.at("spanish");
    }

    intent.action = entry.open;
    return intent;
}

} // namespace paimon::guide
