#include "PopupRegistry.hpp"

#include "../../../utils/Localization.hpp"
#include "../ui/PaimonGuideChatPopup.hpp"

// Popups opened as actions. Each include maps to a real popup; if one moves,
// update its include and the matching open() lambda.
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
#include "../../smooth-scroll/ui/SmoothScrollConfigPopup.hpp"
#include "../../custom-slider/ui/CustomSliderPopup.hpp"
#include "../../beat-shaders/ui/BeatShaderConfigLayer.hpp"
#include "../../scorecell/ui/ScoreCellSettingsPopup.hpp"
#include "../../texture-studio/ui/TextureStudioLayer.hpp"
#include "../../colorful-icons/ui/PaimonIconsConfigPopup.hpp"
#include "../../capture/ui/CaptureMenuPopup.hpp"
#include "../../community/ui/CommunityHubLayer.hpp"
#include "../../../layers/PaiConfigLayer.hpp"
#include "../../../layers/PaimonHubLayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::guide {

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

std::function<void(PaimonGuideChatPopup*)> openCommunity() {
    return [](PaimonGuideChatPopup*) {
        if (auto* scene = CommunityHubLayer::scene()) {
            CCDirector::get()->pushScene(scene);
        }
    };
}

std::function<void(PaimonGuideChatPopup*)> openColorfulIcons() {
    return [](PaimonGuideChatPopup*) {
        paimon::icons::ui::PaimonIconsConfigPopup::open();
    };
}

std::function<void(PaimonGuideChatPopup*)> openCaptureMenu() {
    return [](PaimonGuideChatPopup*) {
        CaptureMenuPopup::toggle();
    };
}

std::function<void(PaimonGuideChatPopup*)> openGeodeSettings() {
    return [](PaimonGuideChatPopup*) {
        geode::openSettingsPopup(geode::Mod::get());
    };
}

} // namespace

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
    {
        // "profile background" should map to ProfileBgPickerPopup, but that needs
        // an accountID, so we only describe it and point to the profile editor.
        PopupEntry e;
        e.id = "profile-background";
        e.category = PopupCategory::Profile;
        e.weight = 130;  // high: most explicit entity
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
        // No action: requires accountID
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
        // No action: requires accountID
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
        // No action: requires accountID
        m_entries.push_back(std::move(e));
    }
    {
        // These popups need accountID + ProfileConfig; can't open them here, only describe.
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
        // No action: requires accountID
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
        // No action: requires accountID
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
        // No action: requires accountID
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
        // No action: requires accountID
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "scene-background";
        e.category = PopupCategory::Background;
        e.weight = 70; // lower than profile-background so that one wins when the query mentions "profile"
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
        // Opens the full editor (PaiConfigLayer, Backgrounds tab), not the smaller
        // BackgroundConfigPopup. PaiConfigLayer defaults to the Backgrounds tab.
        e.open = openPaiConfig();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "menu-music";
        e.category = PopupCategory::Music;
        e.weight = 100;
        e.displayNameByLang["english"] = "Menu Music";
        e.displayNameByLang["spanish"] = "Musica del Menu";
        e.aliasesByLang["english"]     = {
            "menu song", "menuloop", "menu loop", "vinyl",
            "main menu music", "main menu song", "music", "song", "songs"
        };
        e.aliasesByLang["spanish"]     = {
            "musica menu", "cancion menu", "vinilo", "menuloop",
            "musica principal", "musica", "cancion", "canciones"
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
        // No action: requires levelID + thumbnails
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
        // No action: requires an onConfirm callback
        m_entries.push_back(std::move(e));
    }

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

    {
        PopupEntry e;
        e.id = "geode-settings";
        e.category = PopupCategory::None;
        e.weight = 60;
        e.displayNameByLang["english"] = "Mod Settings";
        e.displayNameByLang["spanish"] = "Ajustes del Mod";
        e.aliasesByLang["english"]     = {
            "settings", "preferences", "options", "language", "translate"
        };
        e.aliasesByLang["spanish"]     = {
            "ajustes", "preferencias", "opciones", "idioma", "lenguaje"
        };
        e.descriptionByLang["english"] =
            "<cy>Mod Settings!</c> Language, and other Geode settings for Paimbnails.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes del Mod!</c> Idioma y demas settings de Geode para Paimbnails.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }

    // ---- Extras / visual features (openable popups) ----
    {
        PopupEntry e;
        e.id = "smooth-scroll";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "Smooth Scroll";
        e.displayNameByLang["spanish"] = "Scroll Suave";
        e.aliasesByLang["english"] = {"smooth scroll", "smooth scrolling", "scroll suave", "list scrolling"};
        e.aliasesByLang["spanish"] = {"scroll suave", "desplazamiento suave", "scroll fluido"};
        e.descriptionByLang["english"] =
            "<cy>Smooth Scroll!</c> Smooth mouse-wheel scrolling for menus and lists "
            "(sensitivity, smoothness, editor zoom). Also in <cy>Paimon Hub > Extras > Scroll</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Scroll Suave!</c> Desplazamiento suave con la rueda en menus y listas "
            "(sensibilidad, suavidad, zoom del editor). Tambien en <cy>Paimon Hub > Extras > Scroll</c>.";
        e.open = openSimple<paimon::smoothscroll::SmoothScrollConfigPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "custom-slider";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "Custom Slider";
        e.displayNameByLang["spanish"] = "Slider Personalizado";
        e.aliasesByLang["english"] = {"slider", "slider thumb", "custom slider", "slider skin"};
        e.aliasesByLang["spanish"] = {"slider", "deslizador", "slider personalizado"};
        e.descriptionByLang["english"] =
            "<cy>Custom Slider!</c> Replace slider thumbs with your player icon or an image "
            "(scale, animation, targets). Also in <cy>Paimon Hub > Extras > Slider</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Slider Personalizado!</c> Reemplaza el thumb del slider con tu icono o una imagen "
            "(escala, animacion, objetivos). Tambien en <cy>Paimon Hub > Extras > Slider</c>.";
        e.open = openSimple<paimon::slider::CustomSliderPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "beat-shaders";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "Beat Shaders";
        e.displayNameByLang["spanish"] = "Beat Shaders";
        e.aliasesByLang["english"] = {"beat shaders", "audio shaders", "music shaders", "reactive shaders"};
        e.aliasesByLang["spanish"] = {"beat shaders", "shaders de audio", "shaders de musica", "shaders reactivos"};
        e.descriptionByLang["english"] =
            "<cy>Beat Shaders!</c> Shaders that pulse to the music. Configure them in "
            "<cy>Paimon Hub > Extras > Beat Shaders</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Beat Shaders!</c> Shaders que laten con la musica. Configuralos en "
            "<cy>Paimon Hub > Extras > Beat Shaders</c>.";
        e.open = openSimple<paimon::beat_shaders::BeatShaderConfigLayer>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "score-cell";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "Score Cell Style";
        e.displayNameByLang["spanish"] = "Estilo de Celda de Puntaje";
        e.aliasesByLang["english"] = {"score cell", "scorecell", "score cell style", "cell style"};
        e.aliasesByLang["spanish"] = {"celda de puntaje", "estilo de celda", "score cell"};
        e.descriptionByLang["english"] =
            "<cy>Score Cell Style!</c> Gradient, hover and entrance effects for leaderboard score cells.";
        e.descriptionByLang["spanish"] =
            "<cy>Estilo de Celda de Puntaje!</c> Gradiente, hover y efectos de entrada para las celdas de puntaje.";
        e.open = openSimple<paimon::scorecell::ScoreCellSettingsPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "texture-studio";
        e.category = PopupCategory::None;
        e.weight = 85;
        e.displayNameByLang["english"] = "Texture Studio";
        e.displayNameByLang["spanish"] = "Texture Studio";
        e.aliasesByLang["english"] = {"texture studio", "texture pack", "sprite editor", "retexture", "textures"};
        e.aliasesByLang["spanish"] = {"texture studio", "paquete de texturas", "editor de sprites", "texturas"};
        e.descriptionByLang["english"] =
            "<cy>Texture Studio!</c> Create and edit texture packs and recolor sprites, then apply them.";
        e.descriptionByLang["spanish"] =
            "<cy>Texture Studio!</c> Crea y edita paquetes de texturas y recolorea sprites, luego aplicalos.";
        e.open = [](PaimonGuideChatPopup*) {
            paimon::texture_studio::TextureStudioLayer::open();
        };
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "colorful-icons";
        e.category = PopupCategory::None;
        e.weight = 85;
        e.displayNameByLang["english"] = "Paimon Icons (Recolor)";
        e.displayNameByLang["spanish"] = "Iconos Paimon (Recolor)";
        e.aliasesByLang["english"] = {"colorful icons", "recolor icons", "paimon icons", "icon colors", "rainbow icons"};
        e.aliasesByLang["spanish"] = {"iconos coloridos", "recolorear iconos", "iconos paimon", "colores de iconos"};
        e.descriptionByLang["english"] =
            "<cy>Paimon Icons!</c> Recolor every icon with color modes (your colors, rainbow, "
            "gradient...). Also via the round cube button in the icon garage.";
        e.descriptionByLang["spanish"] =
            "<cy>Iconos Paimon!</c> Recolorea todos los iconos con modos de color (tus colores, "
            "arcoiris, degradado...). Tambien desde el boton redondo con un cubo en la garage de iconos.";
        e.open = openColorfulIcons();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "capture";
        e.category = PopupCategory::Capture;
        e.weight = 90;
        e.displayNameByLang["english"] = "Capture Menu";
        e.displayNameByLang["spanish"] = "Menu de Captura";
        e.aliasesByLang["english"] = {"capture", "screenshot", "snap", "take screenshot", "thumbnail capture", "capture menu"};
        e.aliasesByLang["spanish"] = {"captura", "capturar", "capturadora", "screenshot", "tomar captura", "menu de captura"};
        e.descriptionByLang["english"] =
            "<cy>Capture Menu!</c> Take a clean thumbnail of your level. Opens with right-click in "
            "menus/pause (or your capture keybind); also from the pause menu button.";
        e.descriptionByLang["spanish"] =
            "<cy>Menu de Captura!</c> Toma una miniatura limpia de tu nivel. Se abre con click derecho "
            "en menus/pausa (o tu tecla de captura); tambien desde el boton del menu de pausa.";
        e.open = openCaptureMenu();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "leaderboards";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "Community Leaderboards";
        e.displayNameByLang["spanish"] = "Clasificaciones de la Comunidad";
        e.aliasesByLang["english"] = {"leaderboard", "leaderboards", "ranking", "top creators", "top players"};
        e.aliasesByLang["spanish"] = {"clasificacion", "clasificaciones", "ranking", "tabla", "mejores jugadores", "mejores creadores"};
        e.descriptionByLang["english"] =
            "<cy>Community Leaderboards!</c> Top creators and players, and moderators of the community.";
        e.descriptionByLang["spanish"] =
            "<cy>Clasificaciones de la Comunidad!</c> Mejores creadores y jugadores, y los moderadores.";
        e.open = openCommunity();
        m_entries.push_back(std::move(e));
    }

    // ---- Update flow ----
    {
        PopupEntry e;
        e.id = "mod-updates";
        e.category = PopupCategory::Update;
        e.weight = 80;
        e.displayNameByLang["english"] = "Update Paimbnails";
        e.displayNameByLang["spanish"] = "Actualizar Paimbnails";
        e.aliasesByLang["english"] = {"update", "updates", "version", "upgrade", "check for updates", "new version"};
        e.aliasesByLang["spanish"] = {"actualizar", "actualizacion", "version", "nueva version", "buscar actualizaciones"};
        e.descriptionByLang["english"] =
            "<cy>Update Paimbnails!</c> Check for and install the latest version from "
            "<cy>Paimon Hub > Extras > Actualizar</c>. Auto-update can be toggled in Mod Settings.";
        e.descriptionByLang["spanish"] =
            "<cy>Actualizar Paimbnails!</c> Busca e instala la ultima version desde "
            "<cy>Paimon Hub > Extras > Actualizar</c>. La auto-actualizacion se activa en Ajustes del Mod.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }

    // ---- Toggle / how-to features (described; opens the right settings surface) ----
    {
        PopupEntry e;
        e.id = "profile-redesign";
        e.category = PopupCategory::Profile;
        e.weight = 80;
        e.displayNameByLang["english"] = "Profile Redesign";
        e.displayNameByLang["spanish"] = "Rediseno de Perfil";
        e.aliasesByLang["english"] = {"profile redesign", "redesign profile", "profile layout", "new profile design"};
        e.aliasesByLang["spanish"] = {"rediseno de perfil", "redisenar perfil", "perfil moderno", "nuevo perfil"};
        e.descriptionByLang["english"] =
            "<cy>Profile Redesign!</c> A modern layered-card profile page. Toggle it in "
            "<cy>Mod Settings</c> (Redesign Profile).";
        e.descriptionByLang["spanish"] =
            "<cy>Rediseno de Perfil!</c> Una pagina de perfil moderna con tarjetas. Actívalo en "
            "<cy>Ajustes del Mod</c> (Redesign Profile).";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "global-icons";
        e.category = PopupCategory::None;
        e.weight = 75;
        e.displayNameByLang["english"] = "Global Icons";
        e.displayNameByLang["spanish"] = "Iconos Globales";
        e.aliasesByLang["english"] = {"global icons", "shared icons", "global icon"};
        e.aliasesByLang["spanish"] = {"iconos globales", "iconos compartidos"};
        e.descriptionByLang["english"] =
            "<cy>Global Icons!</c> Share your custom icons and see others' on their profiles. "
            "Enable it in <cy>Mod Settings</c> (needs the More Icons mod).";
        e.descriptionByLang["spanish"] =
            "<cy>Iconos Globales!</c> Comparte tus iconos y ve los de otros en sus perfiles. "
            "Actívalo en <cy>Ajustes del Mod</c> (requiere el mod More Icons).";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "emotes";
        e.category = PopupCategory::Emote;
        e.weight = 80;
        e.displayNameByLang["english"] = "Emotes";
        e.displayNameByLang["spanish"] = "Emotes";
        e.aliasesByLang["english"] = {"emotes", "emote", "emoji", "stickers", "emoticons"};
        e.aliasesByLang["spanish"] = {"emotes", "emote", "emoji", "stickers", "emoticonos"};
        e.descriptionByLang["english"] =
            "<cy>Emotes!</c> Add emotes and stickers in comments and chat. Tap the emote button "
            "next to any comment text box.";
        e.descriptionByLang["spanish"] =
            "<cy>Emotes!</c> Agrega emotes y stickers en comentarios y chat. Toca el boton de emote "
            "al lado de cualquier caja de texto de comentario.";
        // No action: the picker is bound to a specific text input.
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "fonts";
        e.category = PopupCategory::None;
        e.weight = 75;
        e.displayNameByLang["english"] = "Custom Fonts";
        e.displayNameByLang["spanish"] = "Fuentes Personalizadas";
        e.aliasesByLang["english"] = {"fonts", "custom font", "typography", "font picker"};
        e.aliasesByLang["spanish"] = {"fuentes", "fuente", "tipografia", "letra"};
        e.descriptionByLang["english"] =
            "<cy>Custom Fonts!</c> Insert styled text with custom fonts. Use the font button next to "
            "supported text inputs.";
        e.descriptionByLang["spanish"] =
            "<cy>Fuentes Personalizadas!</c> Inserta texto con fuentes personalizadas. Usa el boton de "
            "fuente al lado de las cajas de texto compatibles.";
        // No action: bound to a specific text input.
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "paidraw";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "PaiDraw";
        e.displayNameByLang["spanish"] = "PaiDraw";
        e.aliasesByLang["english"] = {"paidraw", "pai draw", "draw", "drawing", "canvas"};
        e.aliasesByLang["spanish"] = {"paidraw", "dibujar", "dibujo", "lienzo", "pizarra"};
        e.descriptionByLang["english"] =
            "<cy>PaiDraw!</c> A drawing canvas. Open it from <cy>Paimon Hub > General > PaiDraw</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>PaiDraw!</c> Un lienzo para dibujar. Abrelo desde <cy>Paimon Hub > General > PaiDraw</c>.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "layout-editor";
        e.category = PopupCategory::Layout;
        e.weight = 80;
        e.displayNameByLang["english"] = "Main Menu Layout";
        e.displayNameByLang["spanish"] = "Layout del Menu Principal";
        e.aliasesByLang["english"] = {"layout editor", "menu layout", "main menu layout", "customize menu", "move buttons"};
        e.aliasesByLang["spanish"] = {"editor de layout", "layout del menu", "mover botones", "personalizar menu", "editar menu"};
        e.descriptionByLang["english"] =
            "<cy>Main Menu Layout!</c> Drag and restyle the main-menu buttons. Open it with the "
            "Layout Editor keybind (set it in Mod Settings).";
        e.descriptionByLang["spanish"] =
            "<cy>Layout del Menu Principal!</c> Arrastra y reestiliza los botones del menu. Abrelo con "
            "la tecla del Editor de Layout (configurala en Ajustes del Mod).";
        // No action: lives on the main menu via keybind.
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "search-history";
        e.category = PopupCategory::None;
        e.weight = 75;
        e.displayNameByLang["english"] = "Search History";
        e.displayNameByLang["spanish"] = "Historial de Busqueda";
        e.aliasesByLang["english"] = {"search history", "recent searches", "incognito"};
        e.aliasesByLang["spanish"] = {"historial de busqueda", "busquedas recientes", "incognito"};
        e.descriptionByLang["english"] =
            "<cy>Search History!</c> Re-run recent level searches from the history button in the "
            "search bar. Turn on Incognito Mode in Mod Settings to stop saving searches.";
        e.descriptionByLang["spanish"] =
            "<cy>Historial de Busqueda!</c> Repite busquedas recientes desde el boton de historial en "
            "la barra de busqueda. Activa el Modo Incognito en Ajustes del Mod para no guardarlas.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "auto-preview";
        e.category = PopupCategory::Thumbnail;
        e.weight = 80;
        e.displayNameByLang["english"] = "Auto Previews";
        e.displayNameByLang["spanish"] = "Vistas Previas Automaticas";
        e.aliasesByLang["english"] = {"auto preview", "auto previews", "auto thumbnail", "generate thumbnail"};
        e.aliasesByLang["spanish"] = {"vista previa automatica", "miniatura automatica", "generar miniatura"};
        e.descriptionByLang["english"] =
            "<cy>Auto Previews!</c> Auto-generate a thumbnail for levels that have none. Configure "
            "quality and limits in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Vistas Previas Automaticas!</c> Genera una miniatura para niveles que no tienen. "
            "Ajusta calidad y limites en <cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "dynamic-song";
        e.category = PopupCategory::None;
        e.weight = 70;
        e.displayNameByLang["english"] = "Dynamic Song";
        e.displayNameByLang["spanish"] = "Cancion Dinamica";
        e.aliasesByLang["english"] = {"dynamic song", "level song preview", "info screen song"};
        e.aliasesByLang["spanish"] = {"cancion dinamica", "preview de cancion", "cancion del nivel"};
        e.descriptionByLang["english"] =
            "<cy>Dynamic Song!</c> Plays the level's song while you view its info screen. Toggle it in "
            "<cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Cancion Dinamica!</c> Reproduce la cancion del nivel mientras ves su info. Actívala en "
            "<cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "editor-music";
        e.category = PopupCategory::Music;
        e.weight = 78;
        e.displayNameByLang["english"] = "Editor Music";
        e.displayNameByLang["spanish"] = "Musica del Editor";
        e.aliasesByLang["english"] = {"editor music", "music in editor", "editor player"};
        e.aliasesByLang["spanish"] = {"musica del editor", "musica en el editor", "reproductor del editor"};
        e.descriptionByLang["english"] =
            "<cy>Editor Music!</c> Listen to your music library inside the level editor. Enable it in "
            "Mod Settings, then toggle the mini player with the Editor Music keybind (Ctrl+M).";
        e.descriptionByLang["spanish"] =
            "<cy>Musica del Editor!</c> Escucha tu biblioteca dentro del editor. Actívala en Ajustes del "
            "Mod y muestra el mini reproductor con la tecla de Musica del Editor (Ctrl+M).";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "level-info-background";
        e.category = PopupCategory::Background;
        e.weight = 72;
        e.displayNameByLang["english"] = "Level Info Background";
        e.displayNameByLang["spanish"] = "Fondo de Info del Nivel";
        e.aliasesByLang["english"] = {"level info background", "info screen background", "level background style"};
        e.aliasesByLang["spanish"] = {"fondo de info del nivel", "fondo de la pantalla de info", "estilo de fondo del nivel"};
        e.descriptionByLang["english"] =
            "<cy>Level Info Background!</c> Visual style of the level info screen background (blur, "
            "grayscale, shaders...). Pick it in <cy>Paimon Hub > Nivel</c> or Mod Settings.";
        e.descriptionByLang["spanish"] =
            "<cy>Fondo de Info del Nivel!</c> Estilo del fondo de la pantalla de info (blur, escala de "
            "grises, shaders...). Eligelo en <cy>Paimon Hub > Nivel</c> o Ajustes del Mod.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }
}

GuideIntent PopupRegistry::toIntent(PopupEntry const& entry) {
    GuideIntent intent;
    intent.id = entry.id;
    intent.kind = IntentKind::Functional;
    intent.priority = 50;
    intent.weight = entry.weight;
    intent.animation = entry.animation;

    // Intent keywords are [displayName + aliases] per language; displayName is the
    // primary keyword since it's what the user sees on screen.
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

    // Response = entry description; falls back to english.
    if (entry.descriptionByLang.count("english")) {
        intent.responseByLang["english"] = entry.descriptionByLang.at("english");
    }
    if (entry.descriptionByLang.count("spanish")) {
        intent.responseByLang["spanish"] = entry.descriptionByLang.at("spanish");
    }

    intent.action = entry.open;
    return intent;
}

std::string PopupRegistry::displayNameFor(std::string const& id,
                                          std::string const& langId) const {
    for (auto const& e : m_entries) {
        if (e.id != id) continue;
        auto it = e.displayNameByLang.find(langId);
        if (it != e.displayNameByLang.end()) return it->second;
        it = e.displayNameByLang.find("english");
        if (it != e.displayNameByLang.end()) return it->second;
        break;
    }
    // Prettify the id as a last resort: "quick-hub" -> "Quick hub".
    std::string pretty = id;
    std::replace(pretty.begin(), pretty.end(), '-', ' ');
    if (!pretty.empty()) pretty[0] = static_cast<char>(std::toupper(
        static_cast<unsigned char>(pretty[0])));
    return pretty;
}

} // namespace paimon::guide
