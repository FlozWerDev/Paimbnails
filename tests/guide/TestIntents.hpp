#pragma once

#include "../../src/features/guide/services/GuideIntents.hpp"
#include <vector>
#include <string>

// Test fixture mirroring PopupRegistry (functional intents) and
// PaimonGuideService (conversational intents). Kept in sync by hand; it exists
// so PaigoritV1 can be exercised host-side without the Geode SDK or real popups.
// If the registry weights/keywords change, update this table.

namespace paimon::guide::test {

inline GuideIntent func(std::string id, int weight,
                        std::string enName, std::vector<std::string> enAliases,
                        std::string esName, std::vector<std::string> esAliases,
                        std::string categoryId = {},
                        std::vector<std::string> enSearch = {},
                        std::vector<std::string> esSearch = {}) {
    GuideIntent it;
    it.id = std::move(id);
    it.kind = IntentKind::Functional;
    it.weight = weight;
    it.priority = 50;
    it.categoryId = std::move(categoryId);
    std::vector<std::string> en{enName};
    for (auto& a : enAliases) en.push_back(a);
    std::vector<std::string> es{esName};
    for (auto& a : esAliases) es.push_back(a);
    it.keywordsByLang["english"] = std::move(en);
    it.keywordsByLang["spanish"] = std::move(es);
    if (!enSearch.empty()) it.searchPhrasesByLang["english"] = std::move(enSearch);
    if (!esSearch.empty()) it.searchPhrasesByLang["spanish"] = std::move(esSearch);
    return it;
}

inline GuideIntent conv(std::string id, int weight,
                        std::vector<std::string> en,
                        std::vector<std::string> es) {
    GuideIntent it;
    it.id = std::move(id);
    it.kind = IntentKind::Conversational;
    it.weight = weight;
    it.priority = 20;
    it.keywordsByLang["english"] = std::move(en);
    it.keywordsByLang["spanish"] = std::move(es);
    return it;
}

inline std::vector<GuideIntent> makeIntents() {
    std::vector<GuideIntent> v;

    // ---- Functional (mirrors PopupRegistry::registerAll) ----
    v.push_back(func("profile-background", 130,
        "Profile Background", {"profile bg", "profile wallpaper", "pfp background"},
        "Fondo de Perfil", {"fondo perfil", "wallpaper perfil", "fondo del perfil"}));
    v.push_back(func("profile-photo-editor", 120,
        "Profile Photo Editor", {"pfp", "avatar", "profile picture", "profile pic"},
        "Editor de Foto de Perfil", {"foto de perfil", "avatar", "imagen de perfil"}));
    v.push_back(func("profile-settings", 115,
        "Profile Settings", {"profile config", "configure profile"},
        "Ajustes de Perfil", {"configuracion de perfil", "ajustes perfil"}));
    v.push_back(func("profile-music", 95,
        "Profile Music", {"profile song"},
        "Musica de Perfil", {"cancion de perfil", "musica del perfil"}));
    v.push_back(func("comment-background", 100,
        "Comment Background", {"comments bg", "comments wallpaper"},
        "Fondo de Comentarios", {"fondo comentarios"}));
    v.push_back(func("custom-badge", 95,
        "Custom Badge", {"profile badge", "user badge"},
        "Badge Personalizado", {"badge perfil", "insignia"}));
    v.push_back(func("profile-reviews", 95,
        "Profile Reviews", {"reviews", "ratings", "feedback"},
        "Resenas de Perfil", {"resenas", "valoraciones"}));
    v.push_back(func("profile-views", 95,
        "Profile Views", {"visitors", "who viewed"},
        "Visitas de Perfil", {"visitas", "quien me visito"}));
    v.push_back(func("scene-background", 70,
        "Scene Background", {"background", "backgrounds", "wallpaper", "scene wallpaper",
            "menu background", "search background", "level select background",
            "background config", "bg"},
        "Fondo de Escena", {"fondo", "fondos", "wallpaper", "escenario", "fondo menu",
            "fondo busqueda", "fondo seleccion", "configurar fondo"},
        "background",
        {"blur menu background", "video background", "custom menu wallpaper",
         "change the menu background", "make the menu blurry"},
        {"fondo con blur", "fondo con video", "cambiar fondo del menu",
         "hacer el menu borroso", "fondo personalizado del menu"}));
    v.push_back(func("menu-music", 100,
        "Menu Music", {"menu song", "vinyl", "music", "song", "songs", "soundtrack",
            "main menu music", "main menu song"},
        "Musica del Menu", {"musica menu", "cancion menu", "vinilo", "musica", "cancion", "canciones",
            "musica principal"},
        "music",
        {"change menu music", "custom menu song", "play music in menu",
         "replace the menu song", "music in the menu", "menu music player"},
        {"cambiar musica del menu", "cancion personalizada del menu",
         "poner musica en el menu", "reemplazar cancion del menu",
         "musica en el menu", "musica del menu"}));
    v.push_back(func("music-library", 90,
        "Music Library", {"library", "song library", "my songs"},
        "Biblioteca de Musica", {"biblioteca", "mis canciones"}));
    v.push_back(func("music-playlists", 90,
        "Music Playlists", {"playlists", "playlist", "song list"},
        "Playlists de Musica", {"playlists", "lista canciones", "playlist"}));
    v.push_back(func("custom-cursor", 95,
        "Custom Cursor", {"cursor", "mouse pointer", "pointer", "mouse"},
        "Cursor Personalizado", {"cursor", "raton", "puntero", "mouse"},
        "cursor",
        {"custom mouse", "change the cursor", "replace cursor", "cursor image"},
        {"cursor personalizado", "cambiar el cursor", "reemplazar cursor",
         "imagen de cursor"}));
    v.push_back(func("discord-rich-presence", 95,
        "Discord Rich Presence", {"discord", "rpc", "rich presence", "presence", "status"},
        "Discord Rich Presence", {"discord", "rpc", "presencia", "estado"},
        "discord",
        {"show on discord", "discord status", "what im playing on discord"},
        {"mostrar en discord", "estado de discord", "a que estoy jugando en discord"}));
    v.push_back(func("pet", 90,
        "Pet / Mascot", {"pet", "mascot", "companion", "fish"},
        "Mascota", {"mascota", "pet", "companero", "pez"},
        "pet",
        {"floating companion", "pet that follows me", "enable pet"},
        {"companero flotante", "mascota que me sigue", "activar mascota"}));
    v.push_back(func("transition-settings", 75,
        "Transition Settings", {"transition", "transitions", "popup animation", "scene transition"},
        "Ajustes de Transiciones", {"transicion", "transiciones", "animacion popup"}));
    v.push_back(func("extra-effects", 70,
        "Extra Effects", {"effects", "shaders", "visual effects", "fx"},
        "Efectos Extra", {"efectos", "shaders", "fx"}));
    v.push_back(func("progress-bar", 80,
        "Custom Progress Bar", {"progress bar", "progressbar", "loading bar"},
        "Barra de Progreso Personalizada", {"barra de progreso", "barra progreso", "barra de carga"}));
    v.push_back(func("quick-hub", 95,
        "Quick Hub", {"qh", "radial menu", "wheel menu", "shortcut wheel"},
        "Quick Hub", {"qh", "menu radial", "rueda atajos"}));
    v.push_back(func("thumbnail-settings", 90,
        "Thumbnail Settings", {"thumbnail", "thumbnails", "thumbs", "preview"},
        "Ajustes de Miniaturas", {"miniatura", "miniaturas", "preview"},
        "thumbnail",
        {"thumbnails not showing", "thumbnails not loading", "level previews broken",
         "no thumbnails", "enable thumbnails", "fix level previews"},
        {"no se ven miniaturas", "miniaturas no cargan", "miniaturas rotas",
         "no aparecen miniaturas", "activar miniaturas", "previews de niveles"}));
    v.push_back(func("thumbnail-order", 85,
        "Thumbnail Order", {"thumbnail order", "thumb order", "sort thumbnails"},
        "Orden de Miniaturas", {"orden miniaturas", "ordenar miniaturas"}));
    v.push_back(func("level-cell-settings", 85,
        "LevelCell Settings", {"level cell", "levelcell", "list settings", "level list"},
        "Ajustes de Lista de Niveles", {"lista niveles", "lista de niveles"}));
    v.push_back(func("scroll-keybinds", 85,
        "Scroll Keybinds", {"volume", "scroll volume", "music volume", "sfx volume", "keybinds"},
        "Atajos de Teclado", {"volumen", "scroll volumen", "subir volumen", "bajar volumen", "atajos teclado"}));
    v.push_back(func("foryou-preferences", 80,
        "For You Preferences", {"for you", "foryou", "feed", "recommendations", "recommended"},
        "Preferencias Para Ti", {"para ti", "feed", "recomendaciones", "recomendados"}));
    v.push_back(func("paiconfig", 90,
        "PaiConfig", {"paiconfig", "settings", "config", "extras", "cache", "clear cache", "delete cache"},
        "PaiConfig", {"paiconfig", "ajustes", "config", "extras", "cache", "limpiar cache", "borrar cache"}));
    v.push_back(func("hub", 85,
        "Paimon Hub", {"hub", "forum", "community", "news", "posts", "paimon hub"},
        "Paimon Hub", {"hub", "foro", "comunidad", "noticias", "publicaciones"}));
    v.push_back(func("geode-settings", 60,
        "Mod Settings", {"settings", "preferences", "options", "language", "translate"},
        "Ajustes del Mod", {"ajustes", "preferencias", "opciones", "idioma", "lenguaje"}));

    // New feature intents (mirror PopupRegistry round added for full coverage)
    v.push_back(func("smooth-scroll", 80,
        "Smooth Scroll", {"smooth scroll", "smooth scrolling", "scroll suave", "list scrolling"},
        "Scroll Suave", {"scroll suave", "desplazamiento suave", "scroll fluido"},
        "",
        {"lists scroll too fast", "smooth the scrolling", "laggy scroll", "jerky scroll"},
        {"listas se mueven muy rapido", "suavizar el scroll", "scroll entrecortado",
         "scroll con lag"}));
    v.push_back(func("custom-slider", 80,
        "Custom Slider", {"slider", "slider thumb", "custom slider", "slider skin"},
        "Slider Personalizado", {"slider", "deslizador", "slider personalizado"}));
    v.push_back(func("beat-shaders", 80,
        "Beat Shaders", {"beat shaders", "audio shaders", "music shaders", "reactive shaders"},
        "Beat Shaders", {"beat shaders", "shaders de audio", "shaders de musica", "shaders reactivos"}));
    v.push_back(func("score-cell", 80,
        "Score Cell Style", {"score cell", "scorecell", "score cell style", "cell style"},
        "Estilo de Celda de Puntaje", {"celda de puntaje", "estilo de celda", "score cell"}));
    v.push_back(func("texture-studio", 85,
        "Texture Studio", {"texture studio", "texture pack", "sprite editor", "retexture", "textures"},
        "Texture Studio", {"texture studio", "paquete de texturas", "editor de sprites", "texturas"}));
    v.push_back(func("colorful-icons", 85,
        "Paimon Icons (Recolor)", {"colorful icons", "recolor icons", "paimon icons", "icon colors", "rainbow icons", "icon recolor"},
        "Iconos Paimon (Recolor)", {"iconos coloridos", "recolorear iconos", "iconos paimon", "colores de iconos", "recolor de iconos"},
        "visuals",
        {"change icon colors", "rainbow icons", "recolor my icons", "paint icons"},
        {"cambiar color de iconos", "iconos arcoiris", "recolorear mis iconos",
         "pintar iconos"}));
    v.push_back(func("capture", 90,
        "Capture Menu", {"capture", "screenshot", "snap", "take screenshot", "thumbnail capture", "capture menu"},
        "Menu de Captura", {"captura", "capturar", "capturadora", "screenshot", "tomar captura", "menu de captura"},
        "capture",
        {"take a picture of my level", "screenshot my level", "capture my level", "clean level thumbnail capture"},
        {"sacar foto del nivel", "captura limpia del nivel", "capturar mi nivel", "screenshot del nivel"}));
    v.push_back(func("leaderboards", 80,
        "Community Leaderboards", {"leaderboard", "leaderboards", "ranking", "top creators", "top players"},
        "Clasificaciones de la Comunidad", {"clasificacion", "clasificaciones", "ranking", "tabla", "mejores jugadores", "mejores creadores"}));
    v.push_back(func("mod-updates", 80,
        "Update Paimbnails", {"update", "updates", "version", "upgrade", "check for updates", "new version"},
        "Actualizar Paimbnails", {"actualizar", "actualizacion", "version", "nueva version", "buscar actualizaciones"}));
    v.push_back(func("profile-redesign", 80,
        "Profile Redesign", {"profile redesign", "redesign profile", "profile layout", "new profile design"},
        "Rediseno de Perfil", {"rediseno de perfil", "redisenar perfil", "perfil moderno", "nuevo perfil"}));
    v.push_back(func("global-icons", 75,
        "Global Icons", {"global icons", "shared icons", "global icon"},
        "Iconos Globales", {"iconos globales", "iconos compartidos"}));
    v.push_back(func("emotes", 80,
        "Emotes", {"emotes", "emote", "emoji", "stickers", "emoticons"},
        "Emotes", {"emotes", "emote", "emoji", "stickers", "emoticonos"}));
    v.push_back(func("fonts", 75,
        "Custom Fonts", {"fonts", "custom font", "typography", "font picker"},
        "Fuentes Personalizadas", {"fuentes", "fuente", "tipografia", "letra"}));
    v.push_back(func("paidraw", 80,
        "PaiDraw", {"paidraw", "pai draw", "draw", "drawing", "canvas"},
        "PaiDraw", {"paidraw", "dibujar", "dibujo", "lienzo", "pizarra"}));
    v.push_back(func("layout-editor", 80,
        "Main Menu Layout", {"layout editor", "menu layout", "main menu layout", "customize menu", "move buttons"},
        "Layout del Menu Principal", {"editor de layout", "layout del menu", "mover botones", "personalizar menu", "editar menu"}));
    v.push_back(func("search-history", 75,
        "Search History", {"search history", "recent searches", "incognito"},
        "Historial de Busqueda", {"historial de busqueda", "busquedas recientes", "incognito"}));
    v.push_back(func("auto-preview", 80,
        "Auto Previews", {"auto preview", "auto previews", "auto thumbnail", "generate thumbnail"},
        "Vistas Previas Automaticas", {"vista previa automatica", "miniatura automatica", "generar miniatura"}));
    v.push_back(func("dynamic-song", 70,
        "Dynamic Song", {"dynamic song", "level song preview", "info screen song"},
        "Cancion Dinamica", {"cancion dinamica", "preview de cancion", "cancion del nivel"}));
    v.push_back(func("editor-music", 78,
        "Editor Music", {"editor music", "music in editor", "editor player"},
        "Musica del Editor", {"musica del editor", "musica en el editor", "reproductor del editor"}));
    v.push_back(func("level-info-background", 72,
        "Level Info Background", {"level info background", "info screen background", "level background style"},
        "Fondo de Info del Nivel", {"fondo de info del nivel", "fondo de la pantalla de info", "estilo de fondo del nivel"},
        "background",
        {"blur level info screen", "style level info background"},
        {"blur en pantalla de info", "estilo del fondo de info del nivel"}));

    // ---- Editor + misc (full mod coverage) ----
    v.push_back(func("editor-history", 88,
        "Editor History", {"editor history", "undo history", "redo", "undo browser", "object history", "ctrl h"},
        "Historial del Editor", {"historial del editor", "historial de undo", "deshacer", "rehacer", "historial de objetos"},
        "editor",
        {"undo to a previous step", "see object history", "editor undo browser", "non linear undo", "history in editor"},
        {"deshacer a un paso anterior", "ver historial de objetos", "historial de deshacer", "undo no lineal", "historial en el editor"}));
    v.push_back(func("editor-filters", 82,
        "My Levels Filters", {"my levels filters", "level filters", "editor filters", "filter my levels", "my levels"},
        "Filtros de Mis Niveles", {"filtros de mis niveles", "filtros del editor", "filtrar mis niveles", "mis niveles"},
        "editor",
        {"filter my created levels", "filter by song in my levels", "sort my levels list"},
        {"filtrar niveles creados", "filtrar por cancion en mis niveles", "ordenar mis niveles"}));
    v.push_back(func("editor-colorpicker", 78,
        "Editor Color Picker", {"color picker", "editor colors", "ctrl g", "color format", "hsv picker"},
        "Color Picker del Editor", {"color picker", "colores del editor", "selector de color", "formato de color"},
        "editor",
        {"pick a color in the editor", "advanced color picker", "ctrl g color"},
        {"elegir color en el editor", "selector de color avanzado", "color con ctrl g"}));
    v.push_back(func("editor-rotate", 76,
        "Editor Free Rotate", {"free rotate", "editor rotate", "rotate objects", "alt right click", "object rotation"},
        "Rotacion Libre del Editor", {"rotacion libre", "rotar objetos", "rotacion del editor", "alt click derecho"},
        "editor",
        {"rotate objects freely", "free rotate in editor", "alt right click rotate"},
        {"rotar objetos libremente", "rotacion libre en el editor", "rotar con alt click"}));
    v.push_back(func("collab-editor", 90,
        "Collab Editor", {"collab", "collab editor", "collaboration", "multiplayer editor", "live collab", "editor multiplayer", "co edit"},
        "Editor Collab", {"collab", "editor collab", "colaboracion", "editor multijugador", "collab en vivo", "co editar"},
        "editor",
        {"edit levels with friends", "live multiplayer editor", "start a collab room", "invite to collab", "collaborative editing"},
        {"editar niveles con amigos", "editor multijugador en vivo", "iniciar sala collab", "invitar a collab", "edicion colaborativa", "edito con amigos", "como edito con amigos", "editar con amigos"}));
    v.push_back(func("menu-physics", 72,
        "Menu Physics", {"menu physics", "physics menu", "button physics", "menu bounce"},
        "Fisica del Menu", {"fisica del menu", "fisica menu", "botones con fisica", "rebote menu"},
        "layout",
        {"physics on main menu", "buttons bounce", "menu button physics"},
        {"fisica en el menu principal", "botones con rebote", "fisica de botones"}));
    v.push_back(func("song-search", 74,
        "Song Search", {"song search", "search songs", "find song", "song name search", "custom song search"},
        "Busqueda de Canciones", {"busqueda de canciones", "buscar cancion", "buscar canciones", "buscar por nombre de cancion"},
        "music",
        {"search songs by name", "find a custom song", "song browser search"},
        {"buscar canciones por nombre", "encontrar una cancion custom", "buscar en el song browser"}));
    v.push_back(func("comment-mentions", 70,
        "Comment Mentions", {"mentions", "comment mentions", "mention user", "at mention", "ping in comments"},
        "Menciones en Comentarios", {"menciones", "menciones en comentarios", "mencionar usuario", "arroba en comentarios"},
        "profile",
        {"mention someone in comments", "clickable @ mentions", "ping a player"},
        {"mencionar a alguien en comentarios", "menciones con @ clickeables", "ping a un jugador"}));
    v.push_back(func("message-notifications", 68,
        "Message Notifications", {"message notifications", "dm notifications", "message alerts", "inbox alerts"},
        "Notificaciones de Mensajes", {"notificaciones de mensajes", "alertas de mensajes", "avisos de dm", "notificaciones inbox"},
        "help",
        {"notify me of new messages", "dm alert", "message popup notification"},
        {"avisarme de mensajes nuevos", "alerta de dm", "notificacion de mensajes"}));
    v.push_back(func("mod-previews", 70,
        "Mod Previews", {"mod previews", "mod gallery", "mod screenshots", "geode previews"},
        "Previews de Mods", {"previews de mods", "galeria de mods", "capturas de mods", "previews geode"},
        "help",
        {"see mod screenshots", "mod preview gallery", "preview images for mods"},
        {"ver capturas de mods", "galeria de previews", "imagenes de preview de mods"}));
    v.push_back(func("settings-panel", 78,
        "Paimon Settings Panel", {"settings panel", "paimon panel", "multi settings", "settings keybind", "control center"},
        "Panel de Ajustes Paimon", {"panel de ajustes", "panel paimon", "ajustes multipanel", "tecla de ajustes", "centro de control"},
        "cache",
        {"open paimon settings panel", "settings keybind panel", "quick settings panel"},
        {"abrir panel de ajustes paimon", "panel rapido de ajustes", "tecla del panel de ajustes"}));
    v.push_back(func("smooth-ui", 72,
        "Smooth UI", {"smooth ui", "soft popups", "button motion", "smooth transitions ui", "control center smooth"},
        "UI Suave", {"ui suave", "popups suaves", "animacion de botones", "transiciones suaves ui"},
        "visuals",
        {"softer popups", "smooth button animations", "enable smooth ui"},
        {"popups mas suaves", "animaciones suaves de botones", "activar ui suave"}));

    // ---- Round 9: new features (mirror PopupRegistry additions) ----
    v.push_back(func("icon-maker", 95,
        "Icon Maker", {"icon maker", "icon creator", "make icons", "create icons", "custom icon editor", "draw icons", "icon editor"},
        "Creador de Iconos", {"creador de iconos", "hacer iconos", "crear iconos", "editor de iconos", "dibujar iconos", "icono personalizado"},
        "visuals",
        {"make my own icon", "design a custom icon", "create a new icon"},
        {"hacer mi propio icono", "disenar un icono", "crear un icono nuevo"}));
    v.push_back(func("icon-gallery", 90,
        "Icon Gallery", {"icon gallery", "icon store", "icon shop", "download icons", "community icons", "icons gallery"},
        "Tienda de Iconos", {"tienda de iconos", "galeria de iconos", "descargar iconos", "iconos de la comunidad", "icon shop"},
        "visuals",
        {"download new icons", "browse community icons", "get more icons"},
        {"descargar iconos nuevos", "ver iconos de la comunidad", "conseguir mas iconos"}));
    v.push_back(func("icon-gradients", 88,
        "Icon Gradients", {"icon gradients", "gradient icons", "icon gradient editor", "icon colors gradient"},
        "Degradados de Iconos", {"degradados de iconos", "iconos con degradado", "editor de degradados"},
        "visuals",
        {"gradient on my icons", "icon gradient editor", "add gradient to icons"},
        {"degradado en mis iconos", "editor de degradado de iconos", "poner degradado a iconos"}));
    v.push_back(func("separate-dual", 78,
        "Separate Dual Icons", {"separate dual", "dual icons", "p2 icons", "second player icons", "dual kit"},
        "Iconos Duales Separados", {"dual separado", "iconos del dual", "iconos del jugador 2", "kit del dual"},
        "",
        {"different icons for second player", "separate p2 kit", "own icons in dual mode", "different icons in dual"},
        {"iconos distintos para el jugador 2", "kit separado para el dual", "iconos propios en dual", "iconos distintos en el dual"}));
    v.push_back(func("golden-best", 72,
        "Golden Best", {"golden best", "gold percentage", "new best gold", "gold percent"},
        "Golden Best", {"golden best", "porcentaje dorado", "nuevo record dorado", "oro en el record"},
        "",
        {"gold when beating my record", "new best color", "gold percentage on record"},
        {"dorado al batir mi record", "color de nuevo record", "porcentaje dorado en record"}));
    v.push_back(func("death-effects", 80,
        "Death Effects", {"death effects", "death effect", "custom death", "death animation", "explosion effect", "death sound"},
        "Efectos de Muerte", {"efectos de muerte", "efecto de muerte", "muerte personalizada", "animacion de muerte", "explosion", "sonido de muerte"},
        "visuals",
        {"customize my death effect", "change death animation", "import death effects"},
        {"personalizar mi efecto de muerte", "cambiar animacion de muerte", "importar efectos de muerte"}));
    v.push_back(func("gameplay-performance", 78,
        "Performance Mode", {"performance mode", "gameplay performance", "fps boost", "performance settings", "reduce lag", "optimization"},
        "Modo Rendimiento", {"modo rendimiento", "rendimiento", "mejorar fps", "reducir lag", "optimizacion", "rendimiento del juego"},
        "",
        {"make the game run faster", "boost fps", "fix lag while playing", "disable effects for performance"},
        {"hacer que el juego vaya mas rapido", "subir fps", "arreglar lag jugando", "desactivar efectos por rendimiento"}));
    v.push_back(func("icon-copy", 80,
        "Copy Icons", {"copy icons", "copy icon set", "copy someone icons", "icon sets", "my icon sets", "copy player icons"},
        "Copiar Iconos", {"copiar iconos", "copiar set de iconos", "copiar iconos de alguien", "mis sets de iconos", "conjuntos de iconos"},
        "profile",
        {"copy a player icon kit", "save my icon set", "use someone elses icons"},
        {"copiar kit de iconos de un jugador", "guardar mi set de iconos", "usar iconos de otro"}));
    v.push_back(func("level-requests", 85,
        "Level Requests", {"level requests", "level request", "stream requests", "twitch requests", "request queue", "song requests"},
        "Level Requests", {"level requests", "pedidos de niveles", "peticiones", "solicitudes de nivel", "cola de requests", "cola de pedidos", "pedidos del chat"},
        "forum",
        {"view my level requests", "streamer request queue", "requests from my chat", "where are my requests"},
        {"ver mis pedidos de niveles", "cola de pedidos del stream", "pedidos de mi chat", "donde estan mis pedidos"}));
    v.push_back(func("dynamic-volume", 75,
        "Dynamic Volume", {"dynamic volume", "auto volume", "volume leveling", "lufs", "loudness", "even out song volumes", "normalize volume"},
        "Volumen Dinamico", {"volumen dinamico", "volumen automatico", "nivelacion de volumen", "lufs", "igualar volumen de canciones", "normalizar volumen"},
        "music",
        {"even out song volumes", "stop volume jumps between songs", "normalize audio", "songs play at different volumes"},
        {"igualar volumen de canciones", "evitar saltos de volumen", "normalizar audio", "canciones a distinto volumen"}));
    v.push_back(func("menu-loop", 78,
        "Menu Loop Control", {"menu loop control", "menuloop", "now playing", "loop control", "song controls", "now playing card"},
        "Control de Menu Loop", {"menu loop control", "control de loop", "now playing", "controles de cancion"},
        "music",
        {"loop the menu song", "now playing card", "shuffle menu music", "skip menu songs"},
        {"repetir la cancion del menu", "tarjeta now playing", "aleatorio en musica del menu", "saltar canciones del menu"}));
    v.push_back(func("info-suite", 75,
        "Info Suite", {"info suite", "level info", "level stats", "extended info", "visible ids", "jump to page", "progress tracking", "death heatmap"},
        "Info Suite", {"info suite", "info del nivel", "estadisticas del nivel", "info extendida", "ids visibles", "saltar a pagina", "seguimiento de progreso", "mapa de muertes"},
        "",
        {"see hidden level info", "jump to a page in the browser", "track my progress", "see where i die most"},
        {"ver info oculta del nivel", "saltar a una pagina del browser", "seguir mi progreso", "ver donde muero mas"}));

    // ---- Conversational (mirrors PaimonGuideService::registerIntents) ----
    v.push_back(conv("help-general", 40,
        {"help", "guide", "tutorial", "what can you do", "options"},
        {"ayuda", "guia", "tutorial", "que puedes hacer", "opciones"}));
    v.push_back(conv("who-are-you", 35,
        {"who are you", "what are you", "who is paimon", "your name"},
        {"quien eres", "que eres", "como te llamas", "quien es paimon"}));
    v.push_back(conv("thanks", 30,
        {"thanks", "thank you", "ty", "thx", "appreciate"},
        {"gracias", "muchas gracias", "thank you", "te agradezco"}));
    v.push_back(conv("greeting", 30,
        {"hi", "hello", "hey", "good morning", "good evening", "good afternoon", "yo", "sup"},
        {"hola", "buenas", "buenos dias", "buenas tardes", "buenas noches", "ey", "que tal", "saludos"}));
    v.push_back(conv("how-are-you", 30,
        {"how are you", "are you ok", "how do you do", "you fine"},
        {"como estas", "que tal estas", "como te va", "estas bien", "como andas"}));
    v.push_back(conv("compliment", 30,
        {"you are great", "you are awesome", "you are cool", "i love you", "best", "amazing", "wonderful"},
        {"eres genial", "eres la mejor", "te amo", "te quiero", "que linda", "que bonita", "increible"}));
    v.push_back(conv("goodbye", 30,
        {"bye", "goodbye", "see you", "see ya", "later", "cya"},
        {"adios", "chao", "nos vemos", "hasta luego", "hasta pronto", "bye"}));
    v.push_back(conv("joke", 30,
        {"joke", "tell me a joke", "make me laugh", "funny", "haha"},
        {"chiste", "cuentame un chiste", "hazme reir", "gracioso", "broma", "jaja"}));
    v.push_back(conv("what-can-you-do", 35,
        {"what can you do", "what do you do", "your features", "your capabilities"},
        {"que sabes hacer", "que puedes hacer", "tus funciones", "que opciones hay"}));

    // Mod-knowledge intents (mirror PaimonGuideService step 3)
    v.push_back(conv("mod-about", 42,
        {"what is paimbnails", "about paimbnails", "about the mod", "what is this mod",
         "tell me about paimbnails", "what does this mod do", "paimbnails"},
        {"que es paimbnails", "sobre el mod", "de que trata", "que es este mod",
         "sobre paimbnails", "que hace este mod", "paimbnails"}));
    v.push_back(conv("mod-author", 40,
        {"who made paimbnails", "who made you", "who created this",
         "who is the developer", "creator", "author", "developer"},
        {"quien hizo paimbnails", "quien te creo", "quien creo esto",
         "quien es el desarrollador", "creador", "autor", "desarrollador"}));
    v.push_back(conv("mod-free", 40,
        {"is it free", "is paimbnails free", "does it cost",
         "how much does it cost", "price", "is this free"},
        {"es gratis", "es gratuito", "cuesta dinero", "cuanto cuesta", "precio", "es de pago"}));
    v.push_back(conv("mod-install", 40,
        {"how to install", "how do i install", "install paimbnails", "installation", "how to get it"},
        {"como instalar", "como se instala", "instalar paimbnails", "instalacion", "como conseguirlo"}));
    v.push_back(conv("feature-list", 42,
        {"list of features", "all features", "feature list", "list features",
         "what features does it have", "show me all features"},
        {"lista de funciones", "todas las funciones", "lista de features",
         "que funciones tiene", "muestrame todas las funciones", "todas las features"}));
    v.push_back(conv("mod-support", 40,
        {"support", "contact", "report a bug", "report bug", "get help", "i found a bug", "need support"},
        {"soporte", "contacto", "reportar un error", "reportar bug", "encontre un error", "necesito soporte"}));

    // Small-talk intents (mirror PaimonGuideService step 4)
    v.push_back(conv("weather", 22,
        {"what is the weather", "is it cold", "is it hot", "is it raining", "temperature"},
        {"que tiempo hace", "hace frio", "hace calor", "esta lloviendo", "temperatura"}));
    v.push_back(conv("favorite-things", 22,
        {"what do you like", "whats your favorite", "what is your favorite", "favorite food", "favorite color", "do you like"},
        {"que te gusta", "cual es tu favorito", "cual es tu favorita", "comida favorita", "color favorito", "te gusta"}));
    v.push_back(conv("feelings", 22,
        {"are you sad", "are you happy", "are you tired", "do you sleep", "are you bored", "how do you feel"},
        {"estas triste", "estas feliz", "estas cansada", "duermes", "te aburres", "como te sientes"}));
    v.push_back(conv("capabilities", 24,
        {"can you play", "can you do", "are you smart", "are you an ai", "are you a bot", "do you know everything"},
        {"puedes jugar", "puedes hacer", "eres inteligente", "eres una ia", "eres un bot", "sabes de todo"}));
    v.push_back(conv("origin", 22,
        {"where are you from", "where do you live", "where were you born", "are you from teyvat", "your home"},
        {"de donde eres", "donde vives", "donde naciste", "eres de teyvat", "tu hogar"}));

    return v;
}

} // namespace paimon::guide::test
