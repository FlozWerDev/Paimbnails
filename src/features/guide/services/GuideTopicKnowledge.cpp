#include "GuideTopicKnowledge.hpp"

namespace paimon::guide {

std::vector<TopicKnowledge> buildTopicKnowledge() {
    std::vector<TopicKnowledge> out;

    {
        TopicKnowledge t;
        t.topicId = "custom-cursor";
        t.enName = "Custom Cursor";
        t.esName = "Cursor Personalizado";
        t.subtopics = {
            {
                "cursor-color",
                {"color", "tint", "hue", "colour", "colours"},
                {"color", "tono", "matiz", "colores"},
                "For the cursor color: open <cy>Custom Cursor</c> and tap the color swatch. "
                "You can also pick a gradient with <cy>Icon Gradients</c>.",
                "Para el color del cursor: abre <cy>Cursor Personalizado</c> y toca la muestra "
                "de color. Tambien puedes usar un degradado con <cy>Iconos Gradientes</c>.",
                "Color", "Color",
            },
            {
                "cursor-size",
                {"size", "scale", "bigger", "smaller", "large"},
                {"tamano", "escala", "mas grande", "mas pequeno", "grande"},
                "Cursor size is in <cy>Custom Cursor</c>: use the scale slider to make it "
                "bigger or smaller.",
                "El tamano del cursor esta en <cy>Cursor Personalizado</c>: usa el slider de "
                "escala para hacerlo mas grande o pequeno.",
                "Size", "Tamano",
            },
            {
                "cursor-image",
                {"image", "picture", "custom", "own", "png", "sprite"},
                {"imagen", "foto", "propio", "propia", "png", "sprite"},
                "To use your own image: open <cy>Custom Cursor</c>, pick an image file "
                "(PNG) and it replaces the OS cursor.",
                "Para usar tu propia imagen: abre <cy>Cursor Personalizado</c>, elige un "
                "archivo de imagen (PNG) y reemplazara el cursor del sistema.",
                "Own image", "Imagen propia",
            },
            {
                "cursor-transitions",
                {"transition", "animation", "smooth", "move", "trail"},
                {"transicion", "animacion", "suave", "movimiento", "estela"},
                "Cursor transitions and the trail are configured in <cy>Cursor Config</c> "
                "(trail effects, colors and animation).",
                "Las transiciones del cursor y la estela se configuran en <cy>Cursor Config</c> "
                "(efectos de estela, colores y animacion).",
                "Effects", "Efectos",
            },
        };
        t.enMoreReply = "With <cy>Custom Cursor</c> you can also: <cy>color</c>, "
            "<cy>size</c>, <cy>own image</c> and <cy>trail effects</c>.";
        t.esMoreReply = "Con <cy>Cursor Personalizado</c> tambien puedes: <cy>color</c>, "
            "<cy>tamano</c>, <cy>imagen propia</c> y <cy>efectos de estela</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "menu-music";
        t.enName = "Menu Music";
        t.esName = "Musica del Menu";
        t.subtopics = {
            {
                "music-playlists",
                {"playlist", "playlists", "list", "song list"},
                {"playlist", "playlists", "lista", "lista de canciones"},
                "Playlists live in <cy>Menu Music</c>: open the <cy>Playlists</c> tab to "
                "create and manage your song lists.",
                "Las playlists viven en <cy>Musica del Menu</c>: abre la pestana "
                "<cy>Playlists</c> para crear y gestionar tus listas.",
                "Playlists", "Playlists",
            },
            {
                "music-library",
                {"library", "songs", "download", "my songs", "browse"},
                {"biblioteca", "canciones", "descargar", "mis canciones", "navegar"},
                "Your whole song library is in <cy>Menu Music</c>: the <cy>Library</c> tab "
                "shows every downloaded song.",
                "Toda tu biblioteca esta en <cy>Musica del Menu</c>: la pestana <cy>Biblioteca</c> "
                "muestra todas tus canciones descargadas.",
                "Library", "Biblioteca",
            },
            {
                "music-hotkeys",
                {"hotkey", "hotkeys", "key", "keys", "shortcut", "seek"},
                {"tecla", "teclas", "atajo", "atajos", "buscar"},
                "Menu Music hotkeys: <cy>0-9</c> seek, <cy>Ctrl+S</c> shuffle and more. "
                "Toggle them in <cy>Mod Settings</c> (Menu Music Hotkeys).",
                "Las teclas de Musica del Menu: <cy>0-9</c> buscan, <cy>Ctrl+S</c> aleatorio "
                "y mas. Activalas en <cy>Ajustes del Mod</c> (Menu Music Hotkeys).",
                "Hotkeys", "Teclas",
            },
            {
                "music-shuffle",
                {"shuffle", "random", "constant", "loop"},
                {"aleatorio", "random", "constante", "loop"},
                "To shuffle menu songs automatically: enable <cy>Menu Loop Shuffle</c> in "
                "<cy>Mod Settings</c> and the loop changes when a song ends.",
                "Para cambiar la cancion sola: activa <cy>Menu Loop Shuffle</c> en "
                "<cy>Ajustes del Mod</c> y el loop cambiara al terminar.",
                "Shuffle", "Aleatorio",
            },
        };
        t.enMoreReply = "With <cy>Menu Music</c> you can also: <cy>playlists</c>, "
            "<cy>library</c>, <cy>hotkeys</c> and <cy>shuffle</c>.";
        t.esMoreReply = "Con <cy>Musica del Menu</c> tambien puedes: <cy>playlists</c>, "
            "<cy>biblioteca</c>, <cy>teclas</c> y <cy>aleatorio</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "scene-background";
        t.enName = "Scene Background";
        t.esName = "Fondo de Escena";
        t.subtopics = {
            {
                "bg-video",
                {"video", "mp4", "animated", "gif"},
                {"video", "mp4", "animado", "gif"},
                "Video backgrounds are in <cy>Scene Background</c>: pick a video file "
                "(MP4) per screen.",
                "Los fondos de video estan en <cy>Fondo de Escena</c>: elige un archivo de "
                "video (MP4) por pantalla.",
                "Video", "Video",
            },
            {
                "bg-blur",
                {"blur", "blurry", "blurred", "blur effect"},
                {"blur", "borroso", "desenfoque", "efecto blur"},
                "Blur is in <cy>Scene Background</c>: set the background style to blur and "
                "tune the intensity.",
                "El blur esta en <cy>Fondo de Escena</c>: pon el estilo en blur y ajusta la "
                "intensidad.",
                "Blur", "Blur",
            },
            {
                "bg-per-screen",
                {"per screen", "per layer", "menu", "search", "level select", "profile"},
                {"por pantalla", "por capa", "menu", "busqueda", "seleccion", "perfil"},
                "Backgrounds are per screen: <cy>menu</c>, <cy>search</c>, <cy>level select</c> "
                "and <cy>profile</c> each have their own in <cy>Scene Background</c>.",
                "Los fondos son por pantalla: <cy>menu</c>, <cy>busqueda</c>, <cy>seleccion</c> "
                "y <cy>perfil</c> tienen el suyo en <cy>Fondo de Escena</c>.",
                "Per screen", "Por pantalla",
            },
            {
                "bg-gradient",
                {"gradient", "shader", "gradients", "color"},
                {"degradado", "shader", "gradientes", "color"},
                "Gradient and shader backgrounds are in <cy>Scene Background</c>: choose the "
                "gradient or shader style for the screen.",
                "Los fondos con degradado y shaders estan en <cy>Fondo de Escena</c>: elige "
                "el estilo degradado o shader para la pantalla.",
                "Gradient", "Degradado",
            },
        };
        t.enMoreReply = "With <cy>Scene Background</c> you can also: <cy>video</c>, "
            "<cy>blur</c>, <cy>per-screen</c> and <cy>gradient</c>.";
        t.esMoreReply = "Con <cy>Fondo de Escena</c> tambien puedes: <cy>video</c>, "
            "<cy>blur</c>, <cy>por pantalla</c> y <cy>degradado</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "thumbnail-settings";
        t.enName = "Thumbnail Settings";
        t.esName = "Ajustes de Miniaturas";
        t.subtopics = {
            {
                "thumb-size",
                {"size", "width", "bigger", "smaller", "large"},
                {"tamano", "ancho", "mas grande", "mas pequeno", "grande"},
                "Thumbnail size is in <cy>Thumbnail Settings</c>: the <cy>Thumbnail Size</c> "
                "slider widens or narrows the cell preview.",
                "El tamano de las miniaturas esta en <cy>Ajustes de Miniaturas</c>: el slider "
                "<cy>Thumbnail Size</c> agranda o encoge la preview de la celda.",
                "Size", "Tamano",
            },
            {
                "thumb-quality",
                {"quality", "resolution", "sharp", "hd"},
                {"calidad", "resolucion", "nitida", "hd"},
                "Quality is in <cy>Auto Previews</c> settings: pick <cy>tiny</c>, "
                "<cy>small</c> or <cy>medium</c>.",
                "La calidad esta en los ajustes de <cy>Vistas Previas</c>: elige <cy>tiny</c>, "
                "<cy>small</c> o <cy>medium</c>.",
                "Quality", "Calidad",
            },
            {
                "thumb-order",
                {"order", "reorder", "sort", "arrange"},
                {"orden", "reordenar", "ordenar", "acomodar"},
                "To reorder a level's thumbnails: open the level and tap the "
                "<cy>thumbnail order</c> icon.",
                "Para reordenar las miniaturas de un nivel: abre el nivel y toca el icono de "
                "<cy>orden de miniaturas</c>.",
                "Order", "Orden",
            },
            {
                "thumb-capture",
                {"capture", "take", "shoot", "screenshot", "clean"},
                {"capturar", "tomar", "sacar", "screenshot", "limpia"},
                "To take a clean thumbnail: use <cy>Capture</c> (right-click or your capture "
                "key). It saves a clean frame of the level.",
                "Para tomar una miniatura limpia: usa <cy>Captura</c> (click derecho o tu tecla "
                "de captura). Guarda un frame limpio del nivel.",
                "Capture", "Captura",
            },
        };
        t.enMoreReply = "With <cy>Thumbnails</c> you can also: <cy>size</c>, "
            "<cy>quality</c>, <cy>order</c> and <cy>capture</c>.";
        t.esMoreReply = "Con <cy>Miniaturas</c> tambien puedes: <cy>tamano</c>, "
            "<cy>calidad</cy>, <cy>orden</c> y <cy>captura</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "colorful-icons";
        t.enName = "Paimon Icons (Recolor)";
        t.esName = "Iconos Paimon (Recolor)";
        t.subtopics = {
            {
                "icons-recolor",
                {"recolor", "recolour", "color", "paint", "change color"},
                {"recolorear", "color", "pintar", "cambiar color"},
                "To recolor your icons: open <cy>Paimon Icons</c> from the round cube button "
                "in the garage and pick a color mode.",
                "Para recolorear tus iconos: abre <cy>Iconos Paimon</c> desde el boton redondo "
                "del garage y elige un modo de color.",
                "Recolor", "Recolor",
            },
            {
                "icons-gradient",
                {"gradient", "gradients", "fade"},
                {"degradado", "degradados", "fade"},
                "Gradients on icons: in <cy>Icon Gradients</c> (button next to the shards in "
                "the garage) or as a color mode in <cy>Paimon Icons</c>.",
                "Los degradados de iconos: en <cy>Iconos Degradados</c> (boton junto a los "
                "fragmentos del garage) o como modo de color en <cy>Iconos Paimon</c>.",
                "Gradient", "Degradado",
            },
            {
                "icons-rainbow",
                {"rainbow", "rainbow", "multi color"},
                {"arcoiris", "multicolor"},
                "Rainbow mode is in <cy>Paimon Icons</c>: set the color mode to rainbow and "
                "every icon cycles colors.",
                "El modo arcoiris esta en <cy>Iconos Paimon</c>: pon el modo de color en "
                "arcoiris y todos los iconos ciclan colores.",
                "Rainbow", "Arcoiris",
            },
        };
        t.enMoreReply = "With <cy>Paimon Icons</c> you can also: <cy>recolor</c>, "
            "<cy>gradient</c> and <cy>rainbow</c>.";
        t.esMoreReply = "Con <cy>Iconos Paimon</c> tambien puedes: <cy>recolorear</c>, "
            "<cy>degradado</c> y <cy>arcoiris</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "profile-photo-editor";
        t.enName = "Profile Photo Editor";
        t.esName = "Editor de Foto de Perfil";
        t.subtopics = {
            {
                "pp-photo",
                {"photo", "picture", "image", "avatar", "profile pic"},
                {"foto", "imagen", "avatar", "pic"},
                "Your profile photo is set in <cy>Profile Photo Editor</c>: pick an image, "
                "shape and badge there.",
                "Tu foto de perfil se pone en <cy>Editor de Foto de Perfil</c>: elige una "
                "imagen, forma y badge ahi.",
                "Photo", "Foto",
            },
            {
                "pp-shape",
                {"shape", "frame", "border", "round", "square"},
                {"forma", "marco", "borde", "redondo", "cuadrado"},
                "The photo shape is in <cy>Profile Photo Editor</c>: choose a frame or shape "
                "for your profile picture.",
                "La forma de la foto esta en <cy>Editor de Foto de Perfil</c>: elige un marco "
                "o forma para tu foto.",
                "Shape", "Forma",
            },
            {
                "pp-badge",
                {"badge", "insignia", "role", "icon badge"},
                {"badge", "insignia", "rol"},
                "Badges are in <cy>Profile Photo Editor</c>: pick a badge icon that shows "
                "next to your name.",
                "Los badges estan en <cy>Editor de Foto de Perfil</c>: elige un icono que sale "
                "al lado de tu nombre.",
                "Badge", "Badge",
            },
        };
        t.enMoreReply = "In <cy>Profile Photo Editor</c> you can also: <cy>photo</c>, "
            "<cy>shape</c> and <cy>badge</c>.";
        t.esMoreReply = "En <cy>Editor de Foto de Perfil</c> tambien puedes: <cy>foto</c>, "
            "<cy>forma</c> y <cy>badge</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "discord-rich-presence";
        t.enName = "Discord Rich Presence";
        t.esName = "Discord Rich Presence";
        t.subtopics = {
            {
                "discord-status",
                {"status", "presence", "show", "activity"},
                {"estado", "presencia", "mostrar", "actividad"},
                "Discord status is in <cy>Discord Rich Presence</c>: it shows what you're "
                "playing. Toggle it in <cy>Mod Settings</c>.",
                "El estado de Discord esta en <cy>Discord Rich Presence</c>: muestra a que "
                "juegas. Activalo en <cy>Ajustes del Mod</c>.",
                "Status", "Estado",
            },
            {
                "discord-details",
                {"details", "customize", "configure", "info"},
                {"detalles", "personalizar", "configurar", "info"},
                "Details are configured in <cy>Paimon Hub > Discord</c>: what text appears "
                "on your presence.",
                "Los detalles se configuran en <cy>Paimon Hub > Discord</c>: que texto sale "
                "en tu presencia.",
                "Details", "Detalles",
            },
            {
                "discord-enable",
                {"enable", "on", "activate", "turn on"},
                {"activar", "encender", "prender"},
                "Enable Discord Rich Presence in <cy>Mod Settings</c> (Enable Discord Rich "
                "Presence).",
                "Activa Discord Rich Presence en <cy>Ajustes del Mod</c> (Enable Discord Rich "
                "Presence).",
                "Enable", "Activar",
            },
        };
        t.enMoreReply = "With <cy>Discord</c> you can also: <cy>status</c>, "
            "<cy>details</c> and <cy>enable</c>.";
        t.esMoreReply = "Con <cy>Discord</c> tambien puedes: <cy>estado</c>, "
            "<cy>detalles</c> y <cy>activar</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "capture";
        t.enName = "Capture Menu";
        t.esName = "Menu de Captura";
        t.subtopics = {
            {
                "capture-resolution",
                {"resolution", "quality", "1080p", "1440p", "4k"},
                {"resolucion", "calidad", "1080p", "1440p", "4k"},
                "Capture resolution is in <cy>Mod Settings</c>: pick <cy>1080p</c>, "
                "<cy>1440p</c> or <cy>4k</c>.",
                "La resolucion de captura esta en <cy>Ajustes del Mod</c>: elige <cy>1080p</c>, "
                "<cy>1440p</c> o <cy>4k</c>.",
                "Resolution", "Resolucion",
            },
            {
                "capture-key",
                {"key", "keybind", "button", "shortcut"},
                {"tecla", "atajo", "boton", "shortcut"},
                "The capture key is in <cy>Mod Settings</c> (<cy>Capture Thumbnail</c> key) "
                "and the menu key (<cy>Open Capture Menu</c>).",
                "La tecla de captura esta en <cy>Ajustes del Mod</c> (tecla <cy>Capture "
                "Thumbnail</c>) y la del menu (<cy>Open Capture Menu</c>).",
                "Key", "Tecla",
            },
            {
                "capture-rightclick",
                {"right click", "rightclick", "right", "mouse"},
                {"click derecho", "derecho", "raton"},
                "Right-click opens the capture menu by default. Disable it in <cy>Mod "
                "Settings</c> if you prefer only the key.",
                "El click derecho abre el menu de captura por defecto. Desactivalo en "
                "<cy>Ajustes del Mod</c> si prefieres solo la tecla.",
                "Right-click", "Click derecho",
            },
        };
        t.enMoreReply = "With <cy>Capture</c> you can also: <cy>resolution</c>, "
            "<cy>key</c> and <cy>right-click</c>.";
        t.esMoreReply = "Con <cy>Captura</c> tambien puedes: <cy>resolucion</c>, "
            "<cy>tecla</c> y <cy>click derecho</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "collab-editor";
        t.enName = "Collab Editor";
        t.esName = "Editor Collab";
        t.subtopics = {
            {
                "collab-invite",
                {"invite", "invite", "add friend", "room", "join"},
                {"invitar", "agregar amigo", "sala", "unirse"},
                "To invite someone: from the collab tools in the editor, copy the invite "
                "link and share it. They join your room.",
                "Para invitar a alguien: desde las herramientas collab del editor, copia el "
                "link de invitacion y compartelo. Se uniran a tu sala.",
                "Invite", "Invitar",
            },
            {
                "collab-cursors",
                {"cursor", "cursors", "show", "players"},
                {"cursor", "cursores", "mostrar", "jugadores"},
                "Collaborator cursors are a collab setting: show other players' custom "
                "cursors while editing together.",
                "Los cursores de colaboradores son un ajuste del collab: muestra los cursores "
                "de otros jugadores mientras editan juntos.",
                "Cursors", "Cursores",
            },
            {
                "collab-sync",
                {"sync", "synchronize", "lag", "desync", "objects"},
                {"sync", "sincronizar", "lag", "desync", "objetos"},
                "Collab sync is automatic: sends with confirmation and retries, and "
                "auto-repairs desyncs. Big selections sync reliably.",
                "La sincronizacion del collab es automatica: envia con confirmacion y "
                "reintentos, y repara desyncs solo. Las selecciones grandes se sincronizan.",
                "Sync", "Sync",
            },
        };
        t.enMoreReply = "With <cy>Collab Editor</c> you can also: <cy>invite</c>, "
            "<cy>cursors</c> and <cy>sync</c>.";
        t.esMoreReply = "Con <cy>Editor Collab</c> tambien puedes: <cy>invitar</c>, "
            "<cy>cursores</c> y <cy>sync</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "quick-hub";
        t.enName = "Quick Hub";
        t.esName = "Quick Hub";
        t.subtopics = {
            {
                "qh-key",
                {"key", "keybind", "hold", "open"},
                {"tecla", "atajo", "mantener", "abrir"},
                "The Quick Hub key is in <cy>Mod Settings</c>: bind the key or hold it to "
                "open the radial menu.",
                "La tecla del Quick Hub esta en <cy>Ajustes del Mod</c>: configura la tecla o "
                "mantenla para abrir el menu radial.",
                "Key", "Tecla",
            },
            {
                "qh-customize",
                {"customize", "buttons", "shortcuts", "edit", "add"},
                {"personalizar", "botones", "atajos", "editar", "agregar"},
                "Customize the wheel in <cy>Quick Hub</c>: add or edit the shortcuts that "
                "appear around the radial menu.",
                "Personaliza la rueda en <cy>Quick Hub</c>: agrega o edita los atajos que "
                "aparecen alrededor del menu radial.",
                "Customize", "Personalizar",
            },
        };
        t.enMoreReply = "With <cy>Quick Hub</c> you can also: <cy>key</c> and "
            "<cy>customize</c>.";
        t.esMoreReply = "Con <cy>Quick Hub</c> tambien puedes: <cy>tecla</c> y "
            "<cy>personalizar</c>.";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "pet";
        t.enName = "Pet / Mascot";
        t.esName = "Mascota";
        t.subtopics = {
            {
                "pet-enable",
                {"enable", "on", "activate", "show", "turn on"},
                {"activar", "encender", "mostrar", "prender"},
                "Enable the pet in <cy>Mod Settings</c> (Paimon Pet) and it follows your "
                "cursor across layers.",
                "Activa la mascota en <cy>Ajustes del Mod</c> (Paimon Pet) y seguira tu "
                "cursor por todas las capas.",
                "Enable", "Activar",
            },
            {
                "pet-skin",
                {"skin", "look", "color", "appearance", "style"},
                {"skin", "aspecto", "color", "apariencia", "estilo"},
                "The pet skin is chosen in <cy>Pet Config</c>: pick a Paimon-style companion "
                "and its look.",
                "El skin de la mascota se elige en <cy>Pet Config</c>: elige una companera "
                "estilo Paimon y su aspecto.",
                "Skin", "Skin",
            },
        };
        t.enMoreReply = "With <cy>Pet</c> you can also: <cy>enable</c> and <cy>skin</c>.";
        t.esMoreReply = "Con <cy>Mascota</c> tambien puedes: <cy>activar</c> y <cy>skin</c>.";
        out.push_back(std::move(t));
    }

    return out;
}

} // namespace paimon::guide
