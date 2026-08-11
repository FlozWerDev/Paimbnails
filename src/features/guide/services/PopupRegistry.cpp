#include "PopupRegistry.hpp"

#include "../../../utils/Localization.hpp"
#include "../ui/PaimonGuideChatPopup.hpp"

// Keep popup includes and their open() lambdas in sync when moving entries.
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
#include "../../scorecell/ui/LeaderboardLayoutPopup.hpp"
#include "../../texture-studio/ui/TextureStudioLayer.hpp"
#include "../../colorful-icons/ui/PaimonIconsConfigPopup.hpp"
#include "../../capture/ui/CaptureMenuPopup.hpp"
#include "../../community/ui/CommunityHubLayer.hpp"
#include "../../editor-filters/ui/MyLevelFilterPopup.hpp"
#include "../../death-effects/ui/DeathEffectPopup.hpp"
#include "../../gameplay-performance/ui/GameplayPerformancePopup.hpp"
#include "../../icon-copy/ui/MyIconSetsPopup.hpp"
#include "../../icon-gallery/ui/IconStoreLayer.hpp"
#include "../../icon-maker/ui/IconEditorLayer.hpp"
#include "../../icon-maker/ui/IconGalleryLayer.hpp"
#include "../../icon-gradients/ui/GradientLayer.hpp"
#include "../../foryou/ui/TagPreferencesPopup.hpp"
#include "../../settings-panel/services/SettingsPanelManager.hpp"
#include "../../twitch-requests/ui/TwitchRequestsLayer.hpp"
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

std::function<void(PaimonGuideChatPopup*)> openIconMakerGallery() {
    return [](PaimonGuideChatPopup*) {
        paimon::icon_maker::IconGalleryLayer::open();
    };
}

std::function<void(PaimonGuideChatPopup*)> openIconMakerEditor() {
    return [](PaimonGuideChatPopup*) {
        paimon::icon_maker::IconEditorLayer::open(""); // opens the editor for the current/default slot
    };
}

std::function<void(PaimonGuideChatPopup*)> openIconStore() {
    return [](PaimonGuideChatPopup*) {
        paimon::icon_gallery::IconStoreLayer::open();
    };
}

std::function<void(PaimonGuideChatPopup*)> openIconGradients() {
    return [](PaimonGuideChatPopup*) {
        if (auto* popup = paimon::icon_gradients::GradientLayer::create()) popup->show();
    };
}

std::function<void(PaimonGuideChatPopup*)> openLevelRequests() {
    return [](PaimonGuideChatPopup*) {
        paimon::twitch::TwitchRequestsLayer::open();
    };
}

std::function<void(PaimonGuideChatPopup*)> openDeathEffects() {
    return [](PaimonGuideChatPopup*) {
        if (auto* popup = paimon::death_effects::DeathEffectPopup::create()) popup->show();
    };
}

std::function<void(PaimonGuideChatPopup*)> openGameplayPerformance() {
    return [](PaimonGuideChatPopup*) {
        if (auto* popup = paimon::gameplayperf::GameplayPerformancePopup::create()) popup->show();
    };
}

std::function<void(PaimonGuideChatPopup*)> openMyIconSets() {
    return [](PaimonGuideChatPopup*) {
        if (auto* popup = paimon::iconcopy::MyIconSetsPopup::create()) popup->show();
    };
}

std::function<void(PaimonGuideChatPopup*)> openSettingsPanel() {
    return [](PaimonGuideChatPopup*) {
        SettingsPanelManager::get().open(0);
    };
}

}

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
        // Cannot open without an account ID; point users to the profile editor.
        PopupEntry e;
        e.id = "profile-background";
        e.category = PopupCategory::Profile;
        e.weight = 130;  // Most specific profile match.
        e.displayNameByLang["english"] = "Profile Background";
        e.displayNameByLang["spanish"] = "Fondo de Perfil";
        e.aliasesByLang["english"]     = {"profile bg", "profile wallpaper", "pfp background"};
        e.aliasesByLang["spanish"]     = {"fondo perfil", "wallpaper perfil", "fondo del perfil"};
        e.searchPhrasesByLang["english"] = {
            "change my profile background", "profile page wallpaper", "background on my profile"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar fondo de perfil", "fondo en mi perfil", "wallpaper del perfil"
        };
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
        e.searchPhrasesByLang["english"] = {
            "change my profile picture", "change my avatar", "edit profile photo"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar foto de perfil", "cambiar avatar", "editar foto de perfil"
        };
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
        e.aliasesByLang["english"]     = {"profile config", "configure profile", "profile options", "profile privacy"};
        e.aliasesByLang["spanish"]     = {"configuracion de perfil", "ajustes perfil", "opciones de perfil", "privacidad perfil"};
        e.searchPhrasesByLang["english"] = {
            "profile privacy", "configure my profile", "profile options gear"
        };
        e.searchPhrasesByLang["spanish"] = {
            "privacidad del perfil", "configurar mi perfil", "opciones del perfil"
        };
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
        e.weight = 95;
        e.displayNameByLang["english"] = "Profile Music";
        e.displayNameByLang["spanish"] = "Musica de Perfil";
        e.aliasesByLang["english"]     = {"profile song", "profile soundtrack", "profile track"};
        e.aliasesByLang["spanish"]     = {"cancion de perfil", "musica del perfil", "soundtrack de perfil"};
        e.searchPhrasesByLang["english"] = {
            "music on my profile", "song when visiting profile", "play music on profile"
        };
        e.searchPhrasesByLang["spanish"] = {
            "musica en mi perfil", "cancion al visitar perfil", "poner musica en el perfil"
        };
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
        // Requires account ID and ProfileConfig, so it is description-only here.
        PopupEntry e;
        e.id = "comment-background";
        e.category = PopupCategory::Profile;
        e.weight = 100;
        e.displayNameByLang["english"] = "Comment Background";
        e.displayNameByLang["spanish"] = "Fondo de Comentarios";
        e.aliasesByLang["english"]     = {"comments bg", "comments wallpaper", "comment bg"};
        e.aliasesByLang["spanish"]     = {"fondo comentarios", "fondo de comment", "bg comentarios"};
        e.searchPhrasesByLang["english"] = {
            "change comment background", "customize comments look"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar fondo de comentarios", "personalizar comentarios"
        };
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
        e.aliasesByLang["english"]     = {"profile badge", "user badge", "badge", "badges", "role badge"};
        e.aliasesByLang["spanish"]     = {"badge perfil", "insignia", "badge", "insignias", "badge de rol"};
        e.searchPhrasesByLang["english"] = {
            "badge next to my name", "custom name badge", "show badge"
        };
        e.searchPhrasesByLang["spanish"] = {
            "badge al lado del nombre", "insignia personalizada", "mostrar badge"
        };
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
        e.displayNameByLang["spanish"] = "Resenas de Perfil";
        e.aliasesByLang["english"]     = {"reviews", "ratings", "feedback", "profile review"};
        e.aliasesByLang["spanish"]     = {"resenas", "valoraciones", "resenas", "review de perfil"};
        e.searchPhrasesByLang["english"] = {
            "write a review", "rate a profile", "profile feedback"
        };
        e.searchPhrasesByLang["spanish"] = {
            "escribir una resena", "valorar un perfil", "dejar feedback"
        };
        e.descriptionByLang["english"] =
            "<cy>Profile Reviews!</c> See and write reviews on profiles. "
            "Open a profile and tap the reviews icon.";
        e.descriptionByLang["spanish"] =
            "<cy>Resenas de Perfil!</c> Mira y escribe resenas en perfiles. "
            "Abre un perfil y toca el icono de resenas.";
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
        e.aliasesByLang["english"]     = {"visitors", "who viewed", "profile views", "viewers"};
        e.aliasesByLang["spanish"]     = {"visitas", "quien me visito", "visitas de perfil", "quien vio mi perfil"};
        e.searchPhrasesByLang["english"] = {
            "who visited my profile", "see my visitors", "profile view list"
        };
        e.searchPhrasesByLang["spanish"] = {
            "quien visito mi perfil", "ver mis visitas", "lista de visitas"
        };
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
        e.weight = 70; // Keep profile-background ahead of this generic match.
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
        e.searchPhrasesByLang["english"] = {
            "blur menu background", "video background", "custom menu wallpaper",
            "change the menu background", "make the menu blurry"
        };
        e.searchPhrasesByLang["spanish"] = {
            "fondo con blur", "fondo con video", "cambiar fondo del menu",
            "hacer el menu borroso", "fondo personalizado del menu"
        };
        e.descriptionByLang["english"] =
            "<cy>Scene Background!</c> Configure the per-screen background "
            "(menu, search, gauntlet, level select). Images, gradients, video, shaders.";
        e.descriptionByLang["spanish"] =
            "<cy>Fondo de Escena!</c> Configura el fondo por pantalla "
            "(menu, busqueda, gauntlet, level select). Imagenes, gradientes, video, shaders.";
        // Background editor, opened on its Backgrounds tab.
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
            "menu song", "vinyl", "music", "song", "songs",
            "main menu music", "main menu song"
        };
        e.aliasesByLang["spanish"]     = {
            "musica menu", "cancion menu", "vinilo", "musica", "cancion", "canciones",
            "musica principal"
        };
        e.searchPhrasesByLang["english"] = {
            "change menu music", "custom menu song", "play music in menu",
            "replace the menu song", "music in the menu", "menu music player"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar musica del menu", "cancion personalizada del menu",
            "poner musica en el menu", "reemplazar cancion del menu",
            "musica en el menu", "musica del menu"
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
        e.aliasesByLang["english"]     = {"library", "song library", "my songs", "downloaded songs"};
        e.aliasesByLang["spanish"]     = {"biblioteca", "mis canciones", "canciones descargadas"};
        e.searchPhrasesByLang["english"] = {
            "see my downloaded songs", "open music library", "where are my songs"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ver mis canciones descargadas", "abrir biblioteca de musica", "donde estan mis canciones"
        };
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
        e.aliasesByLang["english"]     = {"playlists", "playlist", "song list", "music playlist"};
        e.aliasesByLang["spanish"]     = {"playlists", "lista canciones", "playlist", "listas de reproduccion"};
        e.searchPhrasesByLang["english"] = {
            "create a playlist", "manage playlists", "menu music playlist"
        };
        e.searchPhrasesByLang["spanish"] = {
            "crear una playlist", "gestionar playlists", "playlist del menu"
        };
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
        e.searchPhrasesByLang["english"] = {
            "custom mouse", "change the cursor", "replace cursor", "cursor image"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cursor personalizado", "cambiar el cursor", "reemplazar cursor",
            "imagen de cursor"
        };
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
        e.searchPhrasesByLang["english"] = {
            "show on discord", "discord status", "what im playing on discord"
        };
        e.searchPhrasesByLang["spanish"] = {
            "mostrar en discord", "estado de discord", "a que estoy jugando en discord"
        };
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
        e.aliasesByLang["spanish"]     = {"mascota", "pet", "companero", "pez"};
        e.searchPhrasesByLang["english"] = {
            "floating companion", "pet that follows me", "enable pet"
        };
        e.searchPhrasesByLang["spanish"] = {
            "companero flotante", "mascota que me sigue", "activar mascota"
        };
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
        e.aliasesByLang["english"]     = {"transition", "transitions", "popup animation", "scene transition", "fade"};
        e.aliasesByLang["spanish"]     = {"transicion", "transiciones", "animacion popup", "fundido"};
        e.searchPhrasesByLang["english"] = {
            "change popup animation", "screen transition style", "how screens fade"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar animacion de popups", "estilo de transicion", "como se funden pantallas"
        };
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
        e.category = PopupCategory::Visuals;
        e.weight = 70;
        e.displayNameByLang["english"] = "Extra Effects";
        e.displayNameByLang["spanish"] = "Efectos Extra";
        e.aliasesByLang["english"]     = {"effects", "shaders", "visual effects", "fx"};
        e.aliasesByLang["spanish"]     = {"efectos", "shaders", "fx"};
        e.searchPhrasesByLang["english"] = {
            "extra visual effects", "enable shaders", "fancy effects"
        };
        e.searchPhrasesByLang["spanish"] = {
            "efectos visuales extra", "activar shaders", "efectos fancy"
        };
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
        e.category = PopupCategory::Visuals;
        e.weight = 80;
        e.displayNameByLang["english"] = "Custom Progress Bar";
        e.displayNameByLang["spanish"] = "Barra de Progreso Personalizada";
        e.aliasesByLang["english"]     = {"progress bar", "progressbar", "loading bar", "practice bar"};
        e.aliasesByLang["spanish"]     = {"barra de progreso", "barra progreso", "barra de carga", "barra practice"};
        e.searchPhrasesByLang["english"] = {
            "customize progress bar", "change loading bar style", "style the progress bar"
        };
        e.searchPhrasesByLang["spanish"] = {
            "personalizar barra de progreso", "cambiar estilo de la barra", "estilo de barra de carga"
        };
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
        e.searchPhrasesByLang["english"] = {
            "shortcut wheel", "radial shortcuts", "quick access menu"
        };
        e.searchPhrasesByLang["spanish"] = {
            "rueda de atajos", "atajos rapidos", "menu de acceso rapido"
        };
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
        e.searchPhrasesByLang["english"] = {
            "thumbnails not showing", "thumbnails not loading", "level previews broken",
            "no thumbnails", "enable thumbnails", "fix level previews"
        };
        e.searchPhrasesByLang["spanish"] = {
            "no se ven miniaturas", "miniaturas no cargan", "miniaturas rotas",
            "no aparecen miniaturas", "activar miniaturas", "previews de niveles"
        };
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
        e.aliasesByLang["english"]     = {"thumbnail order", "thumb order", "sort thumbnails", "reorder thumbs"};
        e.aliasesByLang["spanish"]     = {"orden miniaturas", "ordenar miniaturas", "reordenar miniaturas"};
        e.searchPhrasesByLang["english"] = {
            "reorder level thumbnails", "change thumbnail order", "sort my thumbs"
        };
        e.searchPhrasesByLang["spanish"] = {
            "reordenar miniaturas del nivel", "cambiar orden de miniaturas"
        };
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
        e.aliasesByLang["english"]     = {"level cell", "levelcell", "list settings", "level list", "browser cells"};
        e.aliasesByLang["spanish"]     = {"lista niveles", "lista de niveles", "celdas de nivel"};
        e.searchPhrasesByLang["english"] = {
            "how level lists look", "style level cells", "browser cell settings"
        };
        e.searchPhrasesByLang["spanish"] = {
            "como se ven las listas", "estilo de celdas", "ajustes de browser"
        };
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
        e.aliasesByLang["english"]     = {"volume", "scroll volume", "music volume", "sfx volume", "keybinds", "hotkeys"};
        e.aliasesByLang["spanish"]     = {"volumen", "scroll volumen", "subir volumen", "bajar volumen", "atajos teclado", "teclas"};
        e.searchPhrasesByLang["english"] = {
            "bind volume keys", "scroll to change volume", "volume hotkeys"
        };
        e.searchPhrasesByLang["spanish"] = {
            "atar teclas de volumen", "scroll para cambiar volumen", "atajos de volumen"
        };
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
        e.aliasesByLang["english"]     = {"for you", "foryou", "feed", "recommendations", "recommended", "for you prefs"};
        e.aliasesByLang["spanish"]     = {"para ti", "feed", "recomendaciones", "recomendados", "preferencias para ti"};
        e.searchPhrasesByLang["english"] = {
            "tune the for you feed", "recommended content settings", "for you preferences"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ajustar el feed para ti", "preferencias de recomendaciones"
        };
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
        e.id = "foryou-tags";
        e.category = PopupCategory::None;
        e.weight = 80;
        e.displayNameByLang["english"] = "For You Tags";
        e.displayNameByLang["spanish"] = "Etiquetas Para Ti";
        e.aliasesByLang["english"]     = {"tags", "level tags", "tag preferences", "favourite tags", "favorite tags"};
        e.aliasesByLang["spanish"]     = {"etiquetas", "tags", "level tags", "etiquetas favoritas"};
        e.searchPhrasesByLang["english"] = {
            "pick the tags i like", "level tag preferences", "hide levels with a tag"
        };
        e.searchPhrasesByLang["spanish"] = {
            "elegir etiquetas que me gustan", "preferencias de etiquetas", "ocultar niveles con una etiqueta"
        };
        e.descriptionByLang["english"] =
            "<cy>For You Tags!</c> Pick the Level Tags you want more of and the ones "
            "you never want to see. Needs the Level Tags mod.";
        e.descriptionByLang["spanish"] =
            "<cy>Etiquetas Para Ti!</c> Elige las etiquetas que quieres ver mas y las "
            "que no quieres ver nunca. Necesita el mod Level Tags.";
        e.open = openSimple<paimon::foryou::TagPreferencesPopup>();
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
            "clear cache", "delete cache", "paimon config"
        };
        e.aliasesByLang["spanish"]     = {
            "paiconfig", "ajustes", "config", "extras", "cache",
            "limpiar cache", "borrar cache", "config de paimon"
        };
        e.searchPhrasesByLang["english"] = {
            "clear the cache", "open extras settings", "delete thumbnail cache"
        };
        e.searchPhrasesByLang["spanish"] = {
            "limpiar la cache", "abrir extras", "borrar cache de miniaturas"
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
            "paimon hub", "community hub"
        };
        e.aliasesByLang["spanish"]     = {
            "hub", "foro", "comunidad", "noticias", "publicaciones", "paimon hub"
        };
        e.searchPhrasesByLang["english"] = {
            "open the forum", "community posts", "paimon news"
        };
        e.searchPhrasesByLang["spanish"] = {
            "abrir el foro", "publicaciones de la comunidad", "noticias de paimon"
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
            "settings", "preferences", "options", "language", "translate", "mod settings", "geode settings"
        };
        e.aliasesByLang["spanish"]     = {
            "ajustes", "preferencias", "opciones", "idioma", "lenguaje", "ajustes del mod", "ajustes geode"
        };
        e.searchPhrasesByLang["english"] = {
            "change the language", "translate the mod", "open mod settings"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar el idioma", "traducir el mod", "abrir ajustes del mod"
        };
        e.descriptionByLang["english"] =
            "<cy>Mod Settings!</c> Language, and other Geode settings for Paimbnails.";
        e.descriptionByLang["spanish"] =
            "<cy>Ajustes del Mod!</c> Idioma y demas settings de Geode para Paimbnails.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "smooth-scroll";
        e.category = PopupCategory::Visuals;
        e.weight = 80;
        e.displayNameByLang["english"] = "Smooth Scroll";
        e.displayNameByLang["spanish"] = "Scroll Suave";
        e.aliasesByLang["english"] = {"smooth scroll", "smooth scrolling", "scroll suave", "list scrolling"};
        e.aliasesByLang["spanish"] = {"scroll suave", "desplazamiento suave", "scroll fluido"};
        e.searchPhrasesByLang["english"] = {
            "lists scroll too fast", "smooth the scrolling", "laggy scroll", "jerky scroll"
        };
        e.searchPhrasesByLang["spanish"] = {
            "listas se mueven muy rapido", "suavizar el scroll", "scroll entrecortado",
            "scroll con lag"
        };
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
        e.category = PopupCategory::Visuals;
        e.weight = 80;
        e.displayNameByLang["english"] = "Custom Slider";
        e.displayNameByLang["spanish"] = "Slider Personalizado";
        e.aliasesByLang["english"] = {"slider", "slider thumb", "custom slider", "slider skin"};
        e.aliasesByLang["spanish"] = {"slider", "deslizador", "slider personalizado"};
        e.searchPhrasesByLang["english"] = {
            "custom slider thumb", "icon on sliders", "replace slider look"
        };
        e.searchPhrasesByLang["spanish"] = {
            "thumb personalizado del slider", "icono en sliders", "cambiar aspecto del slider"
        };
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
        e.category = PopupCategory::Visuals;
        e.weight = 80;
        e.displayNameByLang["english"] = "Beat Shaders";
        e.displayNameByLang["spanish"] = "Beat Shaders";
        e.aliasesByLang["english"] = {"beat shaders", "audio shaders", "music shaders", "reactive shaders", "beat"};
        e.aliasesByLang["spanish"] = {"beat shaders", "shaders de audio", "shaders de musica", "shaders reactivos", "beat"};
        e.searchPhrasesByLang["english"] = {
            "shaders that pulse to music", "music reactive effects", "beat reactive shaders"
        };
        e.searchPhrasesByLang["spanish"] = {
            "shaders que laten con la musica", "efectos reactivos a la musica"
        };
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
        e.category = PopupCategory::Visuals;
        e.weight = 80;
        e.displayNameByLang["english"] = "Leaderboard Layout";
        e.displayNameByLang["spanish"] = "Layout de Leaderboards";
        e.aliasesByLang["english"] = {"leaderboard layout", "score cell", "scorecell", "cell modules"};
        e.aliasesByLang["spanish"] = {"layout de leaderboard", "celda de puntaje", "modulos de ranking", "score cell"};
        e.searchPhrasesByLang["english"] = {
            "customize leaderboard cells", "hide leaderboard stats", "score cell presets"
        };
        e.searchPhrasesByLang["spanish"] = {
            "personalizar celdas de ranking", "ocultar estadisticas del leaderboard", "presets de score cells"
        };
        e.descriptionByLang["english"] =
            "<cy>Leaderboard Layout!</c> Presets, modules and effects for RobTop leaderboard cells.";
        e.descriptionByLang["spanish"] =
            "<cy>Layout de Leaderboards!</c> Presets, modulos y efectos para las celdas de RobTop.";
        e.open = openSimple<paimon::scorecell::LeaderboardLayoutPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "texture-studio";
        e.category = PopupCategory::Visuals;
        e.weight = 85;
        e.displayNameByLang["english"] = "Texture Studio";
        e.displayNameByLang["spanish"] = "Texture Studio";
        e.aliasesByLang["english"] = {"texture studio", "texture pack", "sprite editor", "retexture", "textures"};
        e.aliasesByLang["spanish"] = {"texture studio", "paquete de texturas", "editor de sprites", "texturas"};
        e.searchPhrasesByLang["english"] = {
            "edit texture packs", "recolor sprites", "make a texture pack"
        };
        e.searchPhrasesByLang["spanish"] = {
            "editar paquetes de texturas", "recolorear sprites", "crear texture pack"
        };
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
        e.category = PopupCategory::Visuals;
        e.weight = 85;
        e.displayNameByLang["english"] = "Paimon Icons (Recolor)";
        e.displayNameByLang["spanish"] = "Iconos Paimon (Recolor)";
        e.aliasesByLang["english"] = {"colorful icons", "recolor icons", "paimon icons", "icon colors", "rainbow icons", "icon recolor"};
        e.aliasesByLang["spanish"] = {"iconos coloridos", "recolorear iconos", "iconos paimon", "colores de iconos", "recolor de iconos"};
        e.searchPhrasesByLang["english"] = {
            "change icon colors", "rainbow icons", "recolor my icons", "paint icons"
        };
        e.searchPhrasesByLang["spanish"] = {
            "cambiar color de iconos", "iconos arcoiris", "recolorear mis iconos",
            "pintar iconos"
        };
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
        e.searchPhrasesByLang["english"] = {
            "take a picture of my level", "screenshot my level", "capture my level",
            "clean level thumbnail capture"
        };
        e.searchPhrasesByLang["spanish"] = {
            "sacar foto del nivel", "captura limpia del nivel", "capturar mi nivel",
            "screenshot del nivel"
        };
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
        e.category = PopupCategory::Forum;
        e.weight = 80;
        e.displayNameByLang["english"] = "Community Leaderboards";
        e.displayNameByLang["spanish"] = "Clasificaciones de la Comunidad";
        e.aliasesByLang["english"] = {"leaderboard", "leaderboards", "ranking", "top creators", "top players"};
        e.aliasesByLang["spanish"] = {"clasificacion", "clasificaciones", "ranking", "tabla", "mejores jugadores", "mejores creadores"};
        e.searchPhrasesByLang["english"] = {
            "community ranking", "top players list", "see leaderboards"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ranking de la comunidad", "lista de mejores jugadores", "ver clasificaciones"
        };
        e.descriptionByLang["english"] =
            "<cy>Community Leaderboards!</c> Top creators and players, and moderators of the community.";
        e.descriptionByLang["spanish"] =
            "<cy>Clasificaciones de la Comunidad!</c> Mejores creadores y jugadores, y los moderadores.";
        e.open = openCommunity();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "mod-updates";
        e.category = PopupCategory::Update;
        e.weight = 80;
        e.displayNameByLang["english"] = "Update Paimbnails";
        e.displayNameByLang["spanish"] = "Actualizar Paimbnails";
        e.aliasesByLang["english"] = {"update", "updates", "version", "upgrade", "check for updates", "new version"};
        e.aliasesByLang["spanish"] = {"actualizar", "actualizacion", "version", "nueva version", "buscar actualizaciones"};
        e.searchPhrasesByLang["english"] = {
            "update the mod", "is there a new version", "install latest paimbnails"
        };
        e.searchPhrasesByLang["spanish"] = {
            "actualizar el mod", "hay una version nueva", "instalar ultima version"
        };
        e.descriptionByLang["english"] =
            "<cy>Update Paimbnails!</c> Check for and install the latest version from "
            "<cy>Paimon Hub > Extras > Actualizar</c>. Auto-update can be toggled in Mod Settings.";
        e.descriptionByLang["spanish"] =
            "<cy>Actualizar Paimbnails!</c> Busca e instala la ultima version desde "
            "<cy>Paimon Hub > Extras > Actualizar</c>. La auto-actualizacion se activa en Ajustes del Mod.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "profile-redesign";
        e.category = PopupCategory::Profile;
        e.weight = 80;
        e.displayNameByLang["english"] = "Profile Redesign";
        e.displayNameByLang["spanish"] = "Rediseno de Perfil";
        e.aliasesByLang["english"] = {"profile redesign", "redesign profile", "profile layout", "new profile design"};
        e.aliasesByLang["spanish"] = {"rediseno de perfil", "redisenar perfil", "perfil moderno", "nuevo perfil"};
        e.searchPhrasesByLang["english"] = {
            "modern profile page", "new look for profiles", "enable profile redesign"
        };
        e.searchPhrasesByLang["spanish"] = {
            "perfil moderno", "nuevo aspecto de perfiles", "activar rediseno de perfil"
        };
        e.descriptionByLang["english"] =
            "<cy>Profile Redesign!</c> A modern layered-card profile page. Toggle it in "
            "<cy>Mod Settings</c> (Redesign Profile).";
        e.descriptionByLang["spanish"] =
            "<cy>Rediseno de Perfil!</c> Una pagina de perfil moderna con tarjetas. Activalo en "
            "<cy>Ajustes del Mod</c> (Redesign Profile).";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "global-icons";
        e.category = PopupCategory::Visuals;
        e.weight = 75;
        e.displayNameByLang["english"] = "Global Icons";
        e.displayNameByLang["spanish"] = "Iconos Globales";
        e.aliasesByLang["english"] = {"global icons", "shared icons", "global icon", "more icons sync"};
        e.aliasesByLang["spanish"] = {"iconos globales", "iconos compartidos", "sincronizar iconos"};
        e.searchPhrasesByLang["english"] = {
            "share my icons", "see other players icons", "global icon sync"
        };
        e.searchPhrasesByLang["spanish"] = {
            "compartir mis iconos", "ver iconos de otros", "sincronizar iconos globales"
        };
        e.descriptionByLang["english"] =
            "<cy>Global Icons!</c> Share your custom icons and see others' on their profiles. "
            "Enable it in <cy>Mod Settings</c> (needs the More Icons mod).";
        e.descriptionByLang["spanish"] =
            "<cy>Iconos Globales!</c> Comparte tus iconos y ve los de otros en sus perfiles. "
            "Activalo en <cy>Ajustes del Mod</c> (requiere el mod More Icons).";
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
        e.searchPhrasesByLang["english"] = {
            "add emoji to comments", "use stickers in chat", "emote picker"
        };
        e.searchPhrasesByLang["spanish"] = {
            "poner emoji en comentarios", "stickers en el chat", "selector de emotes"
        };
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
        e.category = PopupCategory::Emote;
        e.weight = 75;
        e.displayNameByLang["english"] = "Custom Fonts";
        e.displayNameByLang["spanish"] = "Fuentes Personalizadas";
        e.aliasesByLang["english"] = {"fonts", "custom font", "typography", "font picker"};
        e.aliasesByLang["spanish"] = {"fuentes", "fuente", "tipografia", "letra"};
        e.searchPhrasesByLang["english"] = {
            "use custom fonts", "styled text fonts", "font in comments"
        };
        e.searchPhrasesByLang["spanish"] = {
            "usar fuentes personalizadas", "texto con tipografia", "fuentes en comentarios"
        };
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
        e.category = PopupCategory::Help;
        e.weight = 80;
        e.displayNameByLang["english"] = "PaiDraw";
        e.displayNameByLang["spanish"] = "PaiDraw";
        e.aliasesByLang["english"] = {"paidraw", "pai draw", "draw", "drawing", "canvas"};
        e.aliasesByLang["spanish"] = {"paidraw", "dibujar", "dibujo", "lienzo", "pizarra"};
        e.searchPhrasesByLang["english"] = {
            "open drawing canvas", "draw something", "paimon draw"
        };
        e.searchPhrasesByLang["spanish"] = {
            "abrir lienzo de dibujo", "quiero dibujar", "paimon dibujar"
        };
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
        e.searchPhrasesByLang["english"] = {
            "move main menu buttons", "rearrange the menu", "customize main menu"
        };
        e.searchPhrasesByLang["spanish"] = {
            "mover botones del menu", "reordenar el menu", "personalizar menu principal"
        };
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
        e.aliasesByLang["english"] = {"search history", "recent searches", "incognito", "search log"};
        e.aliasesByLang["spanish"] = {"historial de busqueda", "busquedas recientes", "incognito", "historial"};
        e.searchPhrasesByLang["english"] = {
            "see recent searches", "incognito mode search", "search history button"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ver busquedas recientes", "modo incognito busqueda", "historial de busquedas"
        };
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
        e.searchPhrasesByLang["english"] = {
            "auto generate thumbnails", "levels without preview", "automatic level previews"
        };
        e.searchPhrasesByLang["spanish"] = {
            "generar miniaturas automaticas", "niveles sin preview", "previews automaticas"
        };
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
        e.category = PopupCategory::Music;
        e.weight = 70;
        e.displayNameByLang["english"] = "Dynamic Song";
        e.displayNameByLang["spanish"] = "Cancion Dinamica";
        e.aliasesByLang["english"] = {"dynamic song", "level song preview", "info screen song"};
        e.aliasesByLang["spanish"] = {"cancion dinamica", "preview de cancion", "cancion del nivel"};
        e.searchPhrasesByLang["english"] = {
            "play song on level info", "hear the song on info screen"
        };
        e.searchPhrasesByLang["spanish"] = {
            "reproducir cancion en info del nivel", "escuchar cancion en pantalla de info"
        };
        e.descriptionByLang["english"] =
            "<cy>Dynamic Song!</c> Plays the level's song while you view its info screen. Toggle it in "
            "<cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Cancion Dinamica!</c> Reproduce la cancion del nivel mientras ves su info. Activala en "
            "<cy>Ajustes del Mod</c>.";
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
        e.searchPhrasesByLang["english"] = {
            "blur level info screen", "style level info background"
        };
        e.searchPhrasesByLang["spanish"] = {
            "blur en pantalla de info", "estilo del fondo de info del nivel"
        };
        e.descriptionByLang["english"] =
            "<cy>Level Info Background!</c> Visual style of the level info screen background (blur, "
            "grayscale, shaders...). Pick it in <cy>Paimon Hub > Nivel</c> or Mod Settings.";
        e.descriptionByLang["spanish"] =
            "<cy>Fondo de Info del Nivel!</c> Estilo del fondo de la pantalla de info (blur, escala de "
            "grises, shaders...). Eligelo en <cy>Paimon Hub > Nivel</c> o Ajustes del Mod.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "editor-filters";
        e.category = PopupCategory::Editor;
        e.weight = 82;
        e.displayNameByLang["english"] = "My Levels Filters";
        e.displayNameByLang["spanish"] = "Filtros de Mis Niveles";
        e.aliasesByLang["english"] = {
            "my levels filters", "level filters", "editor filters", "filter my levels", "my levels"
        };
        e.aliasesByLang["spanish"] = {
            "filtros de mis niveles", "filtros del editor", "filtrar mis niveles", "mis niveles"
        };
        e.searchPhrasesByLang["english"] = {
            "filter my created levels", "filter by song in my levels", "sort my levels list"
        };
        e.searchPhrasesByLang["spanish"] = {
            "filtrar niveles creados", "filtrar por cancion en mis niveles", "ordenar mis niveles"
        };
        e.descriptionByLang["english"] =
            "<cy>My Levels Filters!</c> Extra filters for the My Levels browser "
            "(song, length, and more).";
        e.descriptionByLang["spanish"] =
            "<cy>Filtros de Mis Niveles!</c> Filtros extra para el browser de Mis Niveles "
            "(cancion, duracion y mas).";
        e.open = openSimple<paimon::editorfilters::MyLevelFilterPopup>();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "autobuild";
        e.category = PopupCategory::Editor;
        e.weight = 86;
        e.displayNameByLang["english"] = "Autobuild";
        e.displayNameByLang["spanish"] = "Autobuild";
        e.aliasesByLang["english"] = {
            "autobuild", "auto build", "auto builder", "wave function collapse", "wfc",
            "templates", "ctrl b", "decorate automatically"
        };
        e.aliasesByLang["spanish"] = {
            "autobuild", "auto build", "construir solo", "plantillas", "ctrl b",
            "decorar automatico", "onda", "sellos"
        };
        e.searchPhrasesByLang["english"] = {
            "decorate a level automatically", "repeat my decoration", "fill an area with deco",
            "capture a template", "autobuild templates"
        };
        e.searchPhrasesByLang["spanish"] = {
            "decorar el nivel automatico", "repetir mi decoracion", "rellenar un area con deco",
            "capturar una plantilla", "plantillas de autobuild"
        };
        e.descriptionByLang["english"] =
            "<cy>Autobuild!</c> Capture a decorated selection as a template and rebuild it on "
            "markers, the selection or a whole area. Open it in the editor with <cy>Ctrl+B</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Autobuild!</c> Captura una zona decorada como plantilla y repitela en "
            "marcadores, en la seleccion o en un area entera. Abrelo en el editor con "
            "<cy>Ctrl+B</c>.";
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "editor-colorpicker";
        e.category = PopupCategory::Editor;
        e.weight = 78;
        e.displayNameByLang["english"] = "Editor Color Picker";
        e.displayNameByLang["spanish"] = "Color Picker del Editor";
        e.aliasesByLang["english"] = {
            "color picker", "editor colors", "ctrl g", "color format", "hsv picker"
        };
        e.aliasesByLang["spanish"] = {
            "color picker", "colores del editor", "selector de color", "formato de color"
        };
        e.searchPhrasesByLang["english"] = {
            "pick a color in the editor", "advanced color picker", "ctrl g color"
        };
        e.searchPhrasesByLang["spanish"] = {
            "elegir color en el editor", "selector de color avanzado", "color con ctrl g"
        };
        e.descriptionByLang["english"] =
            "<cy>Editor Color Picker!</c> Advanced color picker in the editor. "
            "Open with <cy>Ctrl+G</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Color Picker del Editor!</c> Selector de color avanzado en el editor. "
            "Abrelo con <cy>Ctrl+G</c>.";
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "collab-editor";
        e.category = PopupCategory::Editor;
        e.weight = 90;
        e.displayNameByLang["english"] = "Collab Editor";
        e.displayNameByLang["spanish"] = "Editor Collab";
        e.aliasesByLang["english"] = {
            "collab", "collab editor", "collaboration", "multiplayer editor",
            "live collab", "editor multiplayer", "co edit"
        };
        e.aliasesByLang["spanish"] = {
            "collab", "editor collab", "colaboracion", "editor multijugador",
            "collab en vivo", "co editar"
        };
        e.searchPhrasesByLang["english"] = {
            "edit levels with friends", "live multiplayer editor", "start a collab room",
            "invite to collab", "collaborative editing"
        };
        e.searchPhrasesByLang["spanish"] = {
            "editar niveles con amigos", "editor multijugador en vivo", "iniciar sala collab",
            "invitar a collab", "edicion colaborativa", "edito con amigos", "como edito con amigos",
            "editar con amigos"
        };
        e.descriptionByLang["english"] =
            "<cy>Collab Editor!</c> Live multiplayer editing with friends (closed beta until "
            "v1.1.0 / July 20 2026). Use the collab tools from the editor when available.";
        e.descriptionByLang["spanish"] =
            "<cy>Editor Collab!</c> Edicion multijugador en vivo con amigos (beta cerrada hasta "
            "v1.1.0 / 20 jul 2026). Usa las herramientas collab desde el editor cuando esten disponibles.";
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "menu-physics";
        e.category = PopupCategory::Layout;
        e.weight = 72;
        e.displayNameByLang["english"] = "Menu Physics";
        e.displayNameByLang["spanish"] = "Fisica del Menu";
        e.aliasesByLang["english"] = {
            "menu physics", "physics menu", "button physics", "menu bounce"
        };
        e.aliasesByLang["spanish"] = {
            "fisica del menu", "fisica menu", "botones con fisica", "rebote menu"
        };
        e.searchPhrasesByLang["english"] = {
            "physics on main menu", "buttons bounce", "menu button physics"
        };
        e.searchPhrasesByLang["spanish"] = {
            "fisica en el menu principal", "botones con rebote", "fisica de botones"
        };
        e.descriptionByLang["english"] =
            "<cy>Menu Physics!</c> Playful physics on main-menu buttons. Toggle and tune in "
            "<cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Fisica del Menu!</c> Fisica divertida en los botones del menu. Activalo y ajustalo en "
            "<cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "song-search";
        e.category = PopupCategory::Music;
        e.weight = 74;
        e.displayNameByLang["english"] = "Song Search";
        e.displayNameByLang["spanish"] = "Busqueda de Canciones";
        e.aliasesByLang["english"] = {
            "song search", "search songs", "find song", "song name search", "custom song search"
        };
        e.aliasesByLang["spanish"] = {
            "busqueda de canciones", "buscar cancion", "buscar canciones", "buscar por nombre de cancion"
        };
        e.searchPhrasesByLang["english"] = {
            "search songs by name", "find a custom song", "song browser search"
        };
        e.searchPhrasesByLang["spanish"] = {
            "buscar canciones por nombre", "encontrar una cancion custom", "buscar en el song browser"
        };
        e.descriptionByLang["english"] =
            "<cy>Song Search!</c> Search custom songs by name in the song browser / Newgrounds picker.";
        e.descriptionByLang["spanish"] =
            "<cy>Busqueda de Canciones!</c> Busca canciones custom por nombre en el browser / selector de Newgrounds.";
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "comment-mentions";
        e.category = PopupCategory::Profile;
        e.weight = 70;
        e.displayNameByLang["english"] = "Comment Mentions";
        e.displayNameByLang["spanish"] = "Menciones en Comentarios";
        e.aliasesByLang["english"] = {
            "mentions", "comment mentions", "mention user", "at mention", "ping in comments"
        };
        e.aliasesByLang["spanish"] = {
            "menciones", "menciones en comentarios", "mencionar usuario", "arroba en comentarios"
        };
        e.searchPhrasesByLang["english"] = {
            "mention someone in comments", "clickable @ mentions", "ping a player"
        };
        e.searchPhrasesByLang["spanish"] = {
            "mencionar a alguien en comentarios", "menciones con @ clickeables", "ping a un jugador"
        };
        e.descriptionByLang["english"] =
            "<cy>Comment Mentions!</c> @mentions in comments become clickable links to profiles.";
        e.descriptionByLang["spanish"] =
            "<cy>Menciones en Comentarios!</c> Las @menciones en comentarios son enlaces clickeables al perfil.";
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "message-notifications";
        e.category = PopupCategory::Help;
        e.weight = 68;
        e.displayNameByLang["english"] = "Message Notifications";
        e.displayNameByLang["spanish"] = "Notificaciones de Mensajes";
        e.aliasesByLang["english"] = {
            "message notifications", "dm notifications", "message alerts", "inbox alerts"
        };
        e.aliasesByLang["spanish"] = {
            "notificaciones de mensajes", "alertas de mensajes", "avisos de dm", "notificaciones inbox"
        };
        e.searchPhrasesByLang["english"] = {
            "notify me of new messages", "dm alert", "message popup notification"
        };
        e.searchPhrasesByLang["spanish"] = {
            "avisarme de mensajes nuevos", "alerta de dm", "notificacion de mensajes"
        };
        e.descriptionByLang["english"] =
            "<cy>Message Notifications!</c> Alerts when you get new DMs / messages. "
            "Configure related options in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Notificaciones de Mensajes!</c> Avisos cuando llegan DMs / mensajes. "
            "Configura opciones relacionadas en <cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "mod-previews";
        e.category = PopupCategory::Help;
        e.weight = 70;
        e.displayNameByLang["english"] = "Mod Previews";
        e.displayNameByLang["spanish"] = "Previews de Mods";
        e.aliasesByLang["english"] = {
            "mod previews", "mod gallery", "mod screenshots", "geode previews"
        };
        e.aliasesByLang["spanish"] = {
            "previews de mods", "galeria de mods", "capturas de mods", "previews geode"
        };
        e.searchPhrasesByLang["english"] = {
            "see mod screenshots", "mod preview gallery", "preview images for mods"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ver capturas de mods", "galeria de previews", "imagenes de preview de mods"
        };
        e.descriptionByLang["english"] =
            "<cy>Mod Previews!</c> Screenshot galleries for mods in the Geode index. "
            "Opens from mod pages that provide preview images.";
        e.descriptionByLang["spanish"] =
            "<cy>Previews de Mods!</c> Galerias de capturas de mods en el indice de Geode. "
            "Se abren desde paginas de mods que traen imagenes de preview.";
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "settings-panel";
        e.category = PopupCategory::Cache;
        e.weight = 78;
        e.displayNameByLang["english"] = "Paimon Settings Panel";
        e.displayNameByLang["spanish"] = "Panel de Ajustes Paimon";
        e.aliasesByLang["english"] = {
            "settings panel", "paimon panel", "multi settings", "settings keybind", "control center"
        };
        e.aliasesByLang["spanish"] = {
            "panel de ajustes", "panel paimon", "ajustes multipanel", "tecla de ajustes", "centro de control"
        };
        e.searchPhrasesByLang["english"] = {
            "open paimon settings panel", "settings keybind panel", "quick settings panel"
        };
        e.searchPhrasesByLang["spanish"] = {
            "abrir panel de ajustes paimon", "panel rapido de ajustes", "tecla del panel de ajustes"
        };
        e.descriptionByLang["english"] =
            "<cy>Paimon Settings Panel!</c> In-game multi-category settings panel. "
            "Bind its key in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Panel de Ajustes Paimon!</c> Panel multicategoria de ajustes en el juego. "
            "Configura su tecla en <cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "smooth-ui";
        e.category = PopupCategory::Visuals;
        e.weight = 72;
        e.displayNameByLang["english"] = "Smooth UI";
        e.displayNameByLang["spanish"] = "UI Suave";
        e.aliasesByLang["english"] = {
            "smooth ui", "soft popups", "button motion", "smooth transitions ui", "control center smooth"
        };
        e.aliasesByLang["spanish"] = {
            "ui suave", "popups suaves", "animacion de botones", "transiciones suaves ui"
        };
        e.searchPhrasesByLang["english"] = {
            "softer popups", "smooth button animations", "enable smooth ui"
        };
        e.searchPhrasesByLang["spanish"] = {
            "popups mas suaves", "animaciones suaves de botones", "activar ui suave"
        };
        e.descriptionByLang["english"] =
            "<cy>Smooth UI!</c> Softer popups, button motion, blur fades and transition tuning. "
            "Enable in <cy>Paimon Hub > Extras</c> / Mod Settings.";
        e.descriptionByLang["spanish"] =
            "<cy>UI Suave!</c> Popups mas suaves, movimiento de botones, fades de blur y transiciones. "
            "Activalo en <cy>Paimon Hub > Extras</c> / Ajustes del Mod.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "icon-maker";
        e.category = PopupCategory::Visuals;
        e.weight = 95;
        e.displayNameByLang["english"] = "Icon Maker";
        e.displayNameByLang["spanish"] = "Creador de Iconos";
        e.aliasesByLang["english"] = {
            "icon maker", "icon creator", "make icons", "create icons",
            "custom icon editor", "draw icons", "icon editor"
        };
        e.aliasesByLang["spanish"] = {
            "creador de iconos", "hacer iconos", "crear iconos",
            "editor de iconos", "dibujar iconos", "icono personalizado"
        };
        e.searchPhrasesByLang["english"] = {
            "make my own icon", "design a custom icon", "create a new icon"
        };
        e.searchPhrasesByLang["spanish"] = {
            "hacer mi propio icono", "disenar un icono", "crear un icono nuevo"
        };
        e.descriptionByLang["english"] =
            "<cy>Icon Maker!</c> Build and apply your own custom icons with layers, "
            "gradients and images. Open from the garage.";
        e.descriptionByLang["spanish"] =
            "<cy>Creador de Iconos!</c> Crea y aplica tus propios iconos con capas, "
            "degradados e imagenes. Se abre desde el garage.";
        e.open = openIconMakerGallery();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "icon-gallery";
        e.category = PopupCategory::Visuals;
        e.weight = 90;
        e.displayNameByLang["english"] = "Icon Gallery";
        e.displayNameByLang["spanish"] = "Tienda de Iconos";
        e.aliasesByLang["english"] = {
            "icon gallery", "icon store", "icon shop", "download icons",
            "community icons", "icons gallery"
        };
        e.aliasesByLang["spanish"] = {
            "tienda de iconos", "galeria de iconos", "descargar iconos",
            "iconos de la comunidad", "icon shop"
        };
        e.searchPhrasesByLang["english"] = {
            "download new icons", "browse community icons", "get more icons"
        };
        e.searchPhrasesByLang["spanish"] = {
            "descargar iconos nuevos", "ver iconos de la comunidad", "conseguir mas iconos"
        };
        e.descriptionByLang["english"] =
            "<cy>Icon Gallery!</c> Download icons made by the community. "
            "Open from the garage.";
        e.descriptionByLang["spanish"] =
            "<cy>Tienda de Iconos!</c> Descarga iconos hechos por la comunidad. "
            "Se abre desde el garage.";
        e.open = openIconStore();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "icon-gradients";
        e.category = PopupCategory::Visuals;
        e.weight = 88;
        e.displayNameByLang["english"] = "Icon Gradients";
        e.displayNameByLang["spanish"] = "Degradados de Iconos";
        e.aliasesByLang["english"] = {
            "icon gradients", "gradient icons", "icon gradient editor", "icon colors gradient"
        };
        e.aliasesByLang["spanish"] = {
            "degradados de iconos", "iconos con degradado", "editor de degradados"
        };
        e.searchPhrasesByLang["english"] = {
            "gradient on my icons", "icon gradient editor", "add gradient to icons"
        };
        e.searchPhrasesByLang["spanish"] = {
            "degradado en mis iconos", "editor de degradado de iconos", "poner degradado a iconos"
        };
        e.descriptionByLang["english"] =
            "<cy>Icon Gradients!</c> Custom GPU gradient fills for your icons. "
            "Configure them with the button next to the shards in the garage.";
        e.descriptionByLang["spanish"] =
            "<cy>Degradados de Iconos!</c> Rellenos de degradado GPU para tus iconos. "
            "Configuralos con el boton junto a los fragmentos en el garage.";
        e.open = openIconGradients();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "separate-dual";
        e.category = PopupCategory::None;
        e.weight = 78;
        e.displayNameByLang["english"] = "Separate Dual Icons";
        e.displayNameByLang["spanish"] = "Iconos Duales Separados";
        e.aliasesByLang["english"] = {
            "separate dual", "dual icons", "p2 icons", "second player icons", "dual kit"
        };
        e.aliasesByLang["spanish"] = {
            "dual separado", "iconos del dual", "iconos del jugador 2", "kit del dual"
        };
        e.searchPhrasesByLang["english"] = {
            "different icons for second player", "separate p2 kit", "own icons in dual mode",
            "different icons in dual"
        };
        e.searchPhrasesByLang["spanish"] = {
            "iconos distintos para el jugador 2", "kit separado para el dual",
            "iconos propios en dual", "iconos distintos en el dual"
        };
        e.descriptionByLang["english"] =
            "<cy>Separate Dual Icons!</c> Give the 2nd player its own kit: icons, colors, "
            "trails and death effect. Toggle in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Iconos Duales Separados!</c> Dale al jugador 2 su propio kit: iconos, colores, "
            "estelas y efecto de muerte. Se activa en <cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "golden-best";
        e.category = PopupCategory::None;
        e.weight = 72;
        e.displayNameByLang["english"] = "Golden Best";
        e.displayNameByLang["spanish"] = "Golden Best";
        e.aliasesByLang["english"] = {
            "golden best", "gold percentage", "new best gold", "gold percent"
        };
        e.aliasesByLang["spanish"] = {
            "golden best", "porcentaje dorado", "nuevo record dorado", "oro en el record"
        };
        e.searchPhrasesByLang["english"] = {
            "gold when beating my record", "new best color", "gold percentage on record"
        };
        e.searchPhrasesByLang["spanish"] = {
            "dorado al batir mi record", "color de nuevo record", "porcentaje dorado en record"
        };
        e.descriptionByLang["english"] =
            "<cy>Golden Best!</c> Turns the percentage gold while you beat your record. "
            "Configure colors in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Golden Best!</c> Pone el porcentaje dorado mientras bates tu record. "
            "Configura colores en <cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "death-effects";
        e.category = PopupCategory::Visuals;
        e.weight = 80;
        e.displayNameByLang["english"] = "Death Effects";
        e.displayNameByLang["spanish"] = "Efectos de Muerte";
        e.aliasesByLang["english"] = {
            "death effects", "death effect", "custom death", "death animation",
            "explosion effect", "death sound"
        };
        e.aliasesByLang["spanish"] = {
            "efectos de muerte", "efecto de muerte", "muerte personalizada",
            "animacion de muerte", "explosion", "sonido de muerte"
        };
        e.searchPhrasesByLang["english"] = {
            "customize my death effect", "change death animation", "import death effects"
        };
        e.searchPhrasesByLang["spanish"] = {
            "personalizar mi efecto de muerte", "cambiar animacion de muerte", "importar efectos de muerte"
        };
        e.descriptionByLang["english"] =
            "<cy>Death Effects!</c> Custom effects and sounds when you die. "
            "Open from the pause menu or <cy>Paimon Hub > Gameplay</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Efectos de Muerte!</c> Efectos y sonidos personalizados al morir. "
            "Se abre desde el menu de pausa o <cy>Paimon Hub > Gameplay</c>.";
        e.open = openDeathEffects();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "gameplay-performance";
        e.category = PopupCategory::None;
        e.weight = 78;
        e.displayNameByLang["english"] = "Performance Mode";
        e.displayNameByLang["spanish"] = "Modo Rendimiento";
        e.aliasesByLang["english"] = {
            "performance mode", "gameplay performance", "fps boost", "performance settings",
            "reduce lag", "optimization"
        };
        e.aliasesByLang["spanish"] = {
            "modo rendimiento", "rendimiento", "mejorar fps", "reducir lag",
            "optimizacion", "rendimiento del juego"
        };
        e.searchPhrasesByLang["english"] = {
            "make the game run faster", "boost fps", "fix lag while playing",
            "disable effects for performance"
        };
        e.searchPhrasesByLang["spanish"] = {
            "hacer que el juego vaya mas rapido", "subir fps", "arreglar lag jugando",
            "desactivar efectos por rendimiento"
        };
        e.descriptionByLang["english"] =
            "<cy>Performance Mode!</c> Improve FPS with configurable CPU, GPU and visual cuts. "
            "Open it from the level info screen or <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Modo Rendimiento!</c> Mejora los FPS con recortes de CPU, GPU y visuales. "
            "Se abre desde la pantalla de info del nivel o <cy>Ajustes del Mod</c>.";
        e.open = openGameplayPerformance();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "icon-copy";
        e.category = PopupCategory::Profile;
        e.weight = 80;
        e.displayNameByLang["english"] = "Copy Icons";
        e.displayNameByLang["spanish"] = "Copiar Iconos";
        e.aliasesByLang["english"] = {
            "copy icons", "copy icon set", "copy someone icons", "icon sets",
            "my icon sets", "copy player icons"
        };
        e.aliasesByLang["spanish"] = {
            "copiar iconos", "copiar set de iconos", "copiar iconos de alguien",
            "mis sets de iconos", "conjuntos de iconos"
        };
        e.searchPhrasesByLang["english"] = {
            "copy a player icon kit", "save my icon set", "use someone elses icons"
        };
        e.searchPhrasesByLang["spanish"] = {
            "copiar kit de iconos de un jugador", "guardar mi set de iconos", "usar iconos de otro"
        };
        e.descriptionByLang["english"] =
            "<cy>Copy Icons!</c> Copy any player's icon set and wear it, or save your own "
            "style in My Icon Sets. Open a profile and tap the folder icon.";
        e.descriptionByLang["spanish"] =
            "<cy>Copiar Iconos!</c> Copia el set de iconos de cualquier jugador y ponte lo, o "
            "guarda tu estilo en Mis Sets de Iconos. Abre un perfil y toca el icono de carpeta.";
        e.open = openMyIconSets();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "level-requests";
        e.category = PopupCategory::Forum;
        e.weight = 85;
        e.displayNameByLang["english"] = "Level Requests";
        e.displayNameByLang["spanish"] = "Level Requests";
        e.aliasesByLang["english"] = {
            "level requests", "level request", "stream requests",
            "twitch requests", "request queue", "song requests"
        };
        e.aliasesByLang["spanish"] = {
            "level requests", "pedidos de niveles", "peticiones", "solicitudes de nivel",
            "cola de requests", "cola de pedidos", "pedidos del chat"
        };
        e.searchPhrasesByLang["english"] = {
            "view my level requests", "streamer request queue",
            "requests from my chat", "where are my requests"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ver mis pedidos de niveles", "cola de pedidos del stream",
            "pedidos de mi chat", "donde estan mis pedidos"
        };
        e.descriptionByLang["english"] =
            "<cy>Level Requests!</c> Accept levels from the web and live chats. "
            "Open from the search screen or <cy>Paimon Hub > Comunidad</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Level Requests!</c> Acepta niveles desde la web y chats en vivo. "
            "Se abre desde la pantalla de busqueda o <cy>Paimon Hub > Comunidad</c>.";
        e.open = openLevelRequests();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "dynamic-volume";
        e.category = PopupCategory::Music;
        e.weight = 75;
        e.displayNameByLang["english"] = "Dynamic Volume";
        e.displayNameByLang["spanish"] = "Volumen Dinamico";
        e.aliasesByLang["english"] = {
            "dynamic volume", "auto volume", "volume leveling", "lufs", "loudness",
            "even out song volumes", "normalize volume"
        };
        e.aliasesByLang["spanish"] = {
            "volumen dinamico", "volumen automatico", "nivelacion de volumen", "lufs",
            "igualar volumen de canciones", "normalizar volumen"
        };
        e.searchPhrasesByLang["english"] = {
            "even out song volumes", "stop volume jumps between songs", "normalize audio",
            "songs play at different volumes"
        };
        e.searchPhrasesByLang["spanish"] = {
            "igualar volumen de canciones", "evitar saltos de volumen", "normalizar audio",
            "canciones a distinto volumen"
        };
        e.descriptionByLang["english"] =
            "<cy>Dynamic Volume!</c> Measures real song loudness (LUFS) and eases jumps "
            "between quiet and loud songs. Configure in <cy>Paimon Hub > Audio</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Volumen Dinamico!</c> Mide la sonoridad real (LUFS) y suaviza los saltos "
            "entre canciones. Configuralo en <cy>Paimon Hub > Audio</c>.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }
    {
        PopupEntry e;
        e.id = "menu-loop";
        e.category = PopupCategory::Music;
        e.weight = 78;
        e.displayNameByLang["english"] = "Menu Loop Control";
        e.displayNameByLang["spanish"] = "Control de Menu Loop";
        e.aliasesByLang["english"] = {
            "menu loop control", "menuloop", "now playing", "loop control",
            "song controls", "now playing card"
        };
        e.aliasesByLang["spanish"] = {
            "menu loop control", "control de loop", "now playing", "controles de cancion"
        };
        e.searchPhrasesByLang["english"] = {
            "loop the menu song", "now playing card", "shuffle menu music",
            "skip menu songs"
        };
        e.searchPhrasesByLang["spanish"] = {
            "repetir la cancion del menu", "tarjeta now playing",
            "aleatorio en musica del menu", "saltar canciones del menu"
        };
        e.descriptionByLang["english"] =
            "<cy>Menu Loop Control!</c> Now playing card, hotkeys, shuffle and loop tools "
            "for the menu music. Configure in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Control de Menu Loop!</c> Tarjeta now playing, teclas, shuffle y herramientas "
            "de loop para la musica del menu. Configuralo en <cy>Ajustes del Mod</c>.";
        e.open = openHub();
        m_entries.push_back(std::move(e));
    }

    {
        PopupEntry e;
        e.id = "info-suite";
        e.category = PopupCategory::None;
        e.weight = 75;
        e.displayNameByLang["english"] = "Info Suite";
        e.displayNameByLang["spanish"] = "Info Suite";
        e.aliasesByLang["english"] = {
            "info suite", "level info", "level stats", "extended info", "visible ids",
            "jump to page", "progress tracking", "death heatmap"
        };
        e.aliasesByLang["spanish"] = {
            "info suite", "info del nivel", "estadisticas del nivel", "info extendida",
            "ids visibles", "saltar a pagina", "seguimiento de progreso", "mapa de muertes"
        };
        e.searchPhrasesByLang["english"] = {
            "see hidden level info", "jump to a page in the browser", "track my progress",
            "see where i die most"
        };
        e.searchPhrasesByLang["spanish"] = {
            "ver info oculta del nivel", "saltar a una pagina del browser", "seguir mi progreso",
            "ver donde muero mas"
        };
        e.descriptionByLang["english"] =
            "<cy>Info Suite!</c> Level stats, visible ids, jump to page, search presets, "
            "progress tracking and death heatmap. Toggle modules in <cy>Mod Settings</c>.";
        e.descriptionByLang["spanish"] =
            "<cy>Info Suite!</c> Stats del nivel, ids visibles, salto de pagina, presets de "
            "busqueda, seguimiento de progreso y mapa de muertes. Activa modulos en "
            "<cy>Ajustes del Mod</c>.";
        e.open = openGeodeSettings();
        m_entries.push_back(std::move(e));
    }
}

char const* categoryIdString(PopupCategory cat) {
    switch (cat) {
        case PopupCategory::Background: return "background";
        case PopupCategory::Music:      return "music";
        case PopupCategory::Profile:    return "profile";
        case PopupCategory::Capture:    return "capture";
        case PopupCategory::Cursor:     return "cursor";
        case PopupCategory::Pet:        return "pet";
        case PopupCategory::Discord:    return "discord";
        case PopupCategory::Forum:      return "forum";
        case PopupCategory::Emote:      return "emote";
        case PopupCategory::Transition: return "transition";
        case PopupCategory::Layout:     return "layout";
        case PopupCategory::Volume:     return "volume";
        case PopupCategory::Cache:      return "cache";
        case PopupCategory::Update:     return "update";
        case PopupCategory::Language:   return "language";
        case PopupCategory::QuickHub:   return "quickhub";
        case PopupCategory::Thumbnail:  return "thumbnail";
        case PopupCategory::Help:       return "help";
        case PopupCategory::Editor:     return "editor";
        case PopupCategory::Visuals:    return "visuals";
        case PopupCategory::None:
        default:                        return "";
    }
}

PopupCategory categoryFromId(std::string const& id) {
    if (id == "background") return PopupCategory::Background;
    if (id == "music")      return PopupCategory::Music;
    if (id == "profile")    return PopupCategory::Profile;
    if (id == "capture")    return PopupCategory::Capture;
    if (id == "cursor")     return PopupCategory::Cursor;
    if (id == "pet")        return PopupCategory::Pet;
    if (id == "discord")    return PopupCategory::Discord;
    if (id == "forum")      return PopupCategory::Forum;
    if (id == "emote")      return PopupCategory::Emote;
    if (id == "transition") return PopupCategory::Transition;
    if (id == "layout")     return PopupCategory::Layout;
    if (id == "volume")     return PopupCategory::Volume;
    if (id == "cache")      return PopupCategory::Cache;
    if (id == "update")     return PopupCategory::Update;
    if (id == "language")   return PopupCategory::Language;
    if (id == "quickhub")   return PopupCategory::QuickHub;
    if (id == "thumbnail")  return PopupCategory::Thumbnail;
    if (id == "help")       return PopupCategory::Help;
    if (id == "editor")     return PopupCategory::Editor;
    if (id == "visuals")    return PopupCategory::Visuals;
    return PopupCategory::None;
}

std::string categoryDisplayName(PopupCategory cat, std::string const& langId) {
    bool es = (langId == "spanish");
    switch (cat) {
        case PopupCategory::Background: return es ? "Fondos" : "Backgrounds";
        case PopupCategory::Music:      return es ? "Musica" : "Music";
        case PopupCategory::Profile:    return es ? "Perfil" : "Profile";
        case PopupCategory::Capture:    return es ? "Captura" : "Capture";
        case PopupCategory::Cursor:     return es ? "Cursor" : "Cursor";
        case PopupCategory::Pet:        return es ? "Mascota" : "Pet";
        case PopupCategory::Discord:    return "Discord";
        case PopupCategory::Forum:      return es ? "Comunidad" : "Community";
        case PopupCategory::Emote:      return "Emotes";
        case PopupCategory::Transition: return es ? "Transiciones" : "Transitions";
        case PopupCategory::Layout:     return es ? "Layout" : "Layout";
        case PopupCategory::Volume:     return es ? "Volumen" : "Volume";
        case PopupCategory::Cache:      return es ? "Ajustes" : "Settings";
        case PopupCategory::Update:     return es ? "Actualizaciones" : "Updates";
        case PopupCategory::Language:   return es ? "Idioma" : "Language";
        case PopupCategory::QuickHub:   return "Quick Hub";
        case PopupCategory::Thumbnail:  return es ? "Miniaturas" : "Thumbnails";
        case PopupCategory::Help:       return es ? "Ayuda" : "Help";
        case PopupCategory::Editor:     return es ? "Editor" : "Editor";
        case PopupCategory::Visuals:    return es ? "Visuales" : "Visuals";
        case PopupCategory::None:
        default:                        return es ? "Funciones" : "Features";
    }
}

GuideIntent PopupRegistry::toIntent(PopupEntry const& entry) {
    GuideIntent intent;
    intent.id = entry.id;
    intent.kind = IntentKind::Functional;
    intent.priority = 50;
    intent.weight = entry.weight;
    intent.animation = entry.animation;
    intent.categoryId = categoryIdString(entry.category);

    // Keywords combine the displayed name and aliases.
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

    // Natural-language problem and how-to phrases.
    auto copyPhrases = [&](std::string const& lang) {
        auto it = entry.searchPhrasesByLang.find(lang);
        if (it != entry.searchPhrasesByLang.end()) {
            intent.searchPhrasesByLang[lang] = it->second;
        }
    };
    copyPhrases("english");
    copyPhrases("spanish");

    // Response and description text used for matching.
    if (entry.descriptionByLang.count("english")) {
        intent.responseByLang["english"] = entry.descriptionByLang.at("english");
        intent.descriptionByLang["english"] = entry.descriptionByLang.at("english");
    }
    if (entry.descriptionByLang.count("spanish")) {
        intent.responseByLang["spanish"] = entry.descriptionByLang.at("spanish");
        intent.descriptionByLang["spanish"] = entry.descriptionByLang.at("spanish");
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
    // Use a readable ID when no localized display name exists.
    std::string pretty = id;
    std::replace(pretty.begin(), pretty.end(), '-', ' ');
    if (!pretty.empty()) pretty[0] = static_cast<char>(std::toupper(
        static_cast<unsigned char>(pretty[0])));
    return pretty;
}

PopupEntry const* PopupRegistry::findById(std::string const& id) const {
    for (auto const& e : m_entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::vector<PopupEntry const*> PopupRegistry::entriesInCategory(PopupCategory cat) const {
    std::vector<PopupEntry const*> out;
    if (cat == PopupCategory::None) return out;
    for (auto const& e : m_entries) {
        if (e.category == cat) out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](PopupEntry const* a, PopupEntry const* b) {
                  if (a->weight != b->weight) return a->weight > b->weight;
                  return a->id < b->id;
              });
    return out;
}

PopupEntry const* PopupRegistry::categoryLead(PopupCategory cat) const {
    auto list = entriesInCategory(cat);
    return list.empty() ? nullptr : list.front();
}

}
