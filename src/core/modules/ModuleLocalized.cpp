#include "ModuleRegistry.hpp"
#include "../../utils/Localization.hpp"
#include <string_view>
#include <unordered_map>

// Spanish display names/descriptions for the module catalog. English is the
// canonical registry text and stays as the fallback; this only overrides it
// while the mod runs in Spanish. The Level/Web "Requests" modules are kept in
// English on purpose.

namespace paimon::modules {

namespace {

bool isRequestModule(char const* id) {
    std::string_view view(id ? id : "");
    return view == "paimbnails.twitchrequests.menu" || view == "paimbnails.webrequests.menu";
}

using Map = std::unordered_map<std::string_view, char const*>;

Map const& spanishNames() {
    static Map const map = {
        {"paimbnails.smoothui.global", "UI Suave"},
        {"paimbnails.dynamicpopups.global", "Popups Dinamicos"},
        {"paimbnails.popupblur.global", "Desenfoque de Popups"},
        {"paimbnails.smoothscroll.global", "Scroll Suave"},
        {"paimbnails.smoothtext.global", "Entrada de Texto Suave"},
        {"paimbnails.cursor.global", "Cursor Personalizado"},
        {"paimbnails.cursortransition.global", "Transiciones de Cursor"},
        {"paimbnails.cursorclick.global", "Efectos de Click"},
        {"paimbnails.customslider.global", "Pulgar de Slider Personalizado"},
        {"paimbnails.paimonicons.global", "Iconos Paimon"},
        {"paimbnails.transitions.global", "Transiciones de Escena"},
        {"paimbnails.volumescroll.global", "Scroll de Volumen"},
        {"paimbnails.dynamicvolume.global", "Volumen Dinamico"},
        {"paimbnails.safedrop.global", "Caida Segura"},
        {"paimbnails.backgrounds.global", "Fondos Personalizados"},
        {"paimbnails.beatshaders.global", "Shaders al Ritmo"},
        {"paimbnails.quickhub.global", "Quick Hub"},
        {"paimbnails.separatedual.global", "Iconos Duales Separados"},
        {"paimbnails.icongradients.global", "Gradientes de Iconos"},
        {"paimbnails.settingspanel.menu", "Panel de Ajustes"},
        {"paimbnails.menumusic.menu", "Reproductor de Musica del Menu"},
        {"paimbnails.musiceffects.menu", "Efectos de Musica del Menu"},
        {"paimbnails.spatialaudio.menu", "Audio Espacial"},
        {"paimbnails.menuloop.menu", "Control del Loop del Menu"},
        {"paimbnails.menuloopshuffle.menu", "Aleatorio del Loop del Menu"},
        {"paimbnails.menuphysics.menu", "Fisica del Menu"},
        {"paimbnails.menulayout.menu", "Editor de Layout"},
        {"paimbnails.pet.menu", "Mascota Paimon"},
        {"paimbnails.guide.menu", "Guia de Paimon"},
        {"paimbnails.paidraw.menu", "PaiDraw"},
        {"paimbnails.iconmaker.menu", "Creador de Iconos"},
        {"paimbnails.texturestudio.menu", "Texture Studio"},
        {"paimbnails.devtools.menu", "Herramientas de Dev"},
        {"paimbnails.community.menu", "Centro Comunitario"},
        {"paimbnails.forum.menu", "Foro"},
        {"paimbnails.modpreviews.menu", "Previews de Mods"},
        {"paimbnails.hovereffects.browser", "Efectos de Hover"},
        {"paimbnails.compactlist.browser", "Modo Compacto"},
        {"paimbnails.autopreview.browser", "Previews Automaticos"},
        {"paimbnails.leaderboardcells.browser", "Celdas de Clasificacion"},
        {"paimbnails.searchpreview.browser", "Busqueda en Tiempo Real"},
        {"paimbnails.quicksearch.browser", "Busqueda Rapida"},
        {"paimbnails.searchhistory.browser", "Historial de Busquedas"},
        {"paimbnails.incognito.browser", "Modo Incognito"},
        {"paimbnails.foryou.browser", "Para Ti"},
        {"paimbnails.levelfilters.browser", "Filtros de Mis Niveles"},
        {"paimbnails.levelbackground.level", "Fondo de Info del Nivel"},
        {"paimbnails.dynamicsong.level", "Cancion Dinamica"},
        {"paimbnails.imagewarning.level", "Advertencia de Imagen"},
        {"paimbnails.extendedinfo.info", "Informacion Extendida del Nivel"},
        {"paimbnails.visibleids.info", "IDs Visibles"},
        {"paimbnails.jumptopage.info", "Ir a Pagina"},
        {"paimbnails.advancedsearch.info", "Busqueda Avanzada"},
        {"paimbnails.searchpresets.info", "Presets de Busqueda"},
        {"paimbnails.progresstracking.info", "Seguimiento de Progreso"},
        {"paimbnails.deathheatmap.info", "Mapa de Calor de Muertes"},
        {"paimbnails.unregprofiles.info", "Perfiles No Registrados"},
        {"paimbnails.commenttools.info", "Herramientas de Comentarios"},
        {"paimbnails.greenusers.info", "Arreglar Usuarios Verdes"},
        {"paimbnails.gdhistory.info", "Enriquecimiento GDHistory"},
        {"paimbnails.performance.gameplay", "Modo Rendimiento"},
        {"paimbnails.nativeperf.gameplay", "Rendimiento Nativo"},
        {"paimbnails.glowcut.gameplay", "Desactivar Glow"},
        {"paimbnails.bgeffects.gameplay", "Desactivar Efectos de Fondo"},
        {"paimbnails.gameeffects.gameplay", "Desactivar Efectos de Gameplay"},
        {"paimbnails.playereffects.gameplay", "Desactivar Efectos del Jugador"},
        {"paimbnails.dynamicvolume.gameplay", "Pausar Volumen Dinamico"},
        {"paimbnails.modvisuals.gameplay", "Desactivar Efectos de Paimbnails"},
        {"paimbnails.autopreview.gameplay", "Desactivar Vista Previa Automatica"},
        {"paimbnails.perftransitions.gameplay", "Desactivar Transiciones de Nivel"},
        {"paimbnails.backgroundcut.gameplay", "Ocultar Fondo"},
        {"paimbnails.groundcut.gameplay", "Ocultar Suelo"},
        {"paimbnails.decocut.gameplay", "Ocultar Decoracion"},
        {"paimbnails.gradientcut.gameplay", "Desactivar Gradientes"},
        {"paimbnails.shadercut.gameplay", "Desactivar Shaders"},
        {"paimbnails.particlecut.gameplay", "Desactivar Particulas"},
        {"paimbnails.levelcut.gameplay", "Desactivar Flashes y Ondas"},
        {"paimbnails.capture.gameplay", "Captura de Miniaturas"},
        {"paimbnails.progressbar.gameplay", "Barra de Progreso Personalizada"},
        {"paimbnails.goldenbest.gameplay", "Golden Best"},
        {"paimbnails.deatheffects.gameplay", "Efectos de Muerte"},
        {"paimbnails.levelentry.gameplay", "Efectos de Entrada al Nivel"},
        {"paimbnails.pausezoom.gameplay", "Zoom en Pausa"},
        {"paimbnails.profileredesign.profile", "Rediseno de Perfil"},
        {"paimbnails.profilemusic.profile", "Musica de Perfil"},
        {"paimbnails.paimonprofiles.profile", "Perfiles Paimon"},
        {"paimbnails.globalicons.profile", "Iconos Globales"},
        {"paimbnails.badges.profile", "Insignias de Rol"},
        {"paimbnails.iconcopy.profile", "Copiar Iconos"},
        {"paimbnails.emotes.social", "Emotes"},
        {"paimbnails.fonts.social", "Fuentes de Comentarios"},
        {"paimbnails.mentions.social", "Menciones en Comentarios"},
        {"paimbnails.messagesredesign.social", "Rediseno de Mensajes"},
        {"paimbnails.messagenotifs.social", "Notificaciones de Mensajes"},
        {"paimbnails.thumbalerts.social", "Avisos de Miniaturas Nuevas"},
        {"paimbnails.colorpicker.editor", "Selector de Color"},
        {"paimbnails.gifimport.editor", "GIF a Objetos"},
        {"paimbnails.gifrender.editor", "Importacion Render"},
        {"paimbnails.physics.editor", "Simulador de Fisicas"},
        {"paimbnails.collab.editor", "Editor Colaborativo"},
        {"paimbnails.collabcursors.editor", "Cursores de Colab"},
        {"paimbnails.inputscroll.editor", "Scroll en Inputs"},
        {"paimbnails.songsearch.editor", "Buscar Cancion"},
        {"paimbnails.autoupdate.system", "Actualizacion Automatica"},
        {"paimbnails.diskcache.system", "Cache en Disco"},
        {"paimbnails.robtopcache.system", "Cache de Respuestas de RobTop"},
        {"paimbnails.discordrpc.system", "Discord Rich Presence"},
        {"paimbnails.moderation.system", "Herramientas de Moderacion"},
        {"paimbnails.debuglogs.system", "Logs de Depuracion"},
    };
    return map;
}

Map const& spanishDescs() {
    static Map const map = {
        {"paimbnails.smoothui.global", "Transiciones suaves en popups y pulsaciones de botones."},
        {"paimbnails.dynamicpopups.global", "Animaciones de entrada y salida en cada popup."},
        {"paimbnails.popupblur.global", "Desenfoca la escena detras de los popups."},
        {"paimbnails.smoothscroll.global", "Scroll inercial con la rueda en menus y listas."},
        {"paimbnails.smoothtext.global", "Letras animadas mientras escribes."},
        {"paimbnails.cursor.global", "Reemplaza el cursor del sistema por tus propias imagenes."},
        {"paimbnails.cursortransition.global", "Cambios animados entre estados del cursor."},
        {"paimbnails.cursorclick.global", "Estallidos, efectos al mantener y sonidos al hacer click."},
        {"paimbnails.customslider.global", "Usa tu icono como el pulgar del slider."},
        {"paimbnails.paimonicons.global", "Recolorea los iconos del juego con tu paleta."},
        {"paimbnails.transitions.global", "Transiciones personalizadas entre escenas."},
        {"paimbnails.volumescroll.global", "Mantiene un modificador y usa la rueda para cambiar el volumen."},
        {"paimbnails.dynamicvolume.global", "Ecualiza una cancion fuerte al nivel de la anterior, o mantiene todas al mismo nivel."},
        {"paimbnails.safedrop.global", "Evita picos de efecto y volumen antes de que lleguen."},
        {"paimbnails.backgrounds.global", "Imagenes, videos y shaders como fondo de capa."},
        {"paimbnails.beatshaders.global", "Efectos de shader sincronizados con el ritmo."},
        {"paimbnails.quickhub.global", "Menu radial de accesos rapidos con una tecla o mantener."},
        {"paimbnails.settingspanel.menu", "Panel de ajustes en el juego con vistas previas en vivo."},
        {"paimbnails.menumusic.menu", "Boton con forma de vinilo con tu biblioteca y playlists."},
        {"paimbnails.musiceffects.menu", "Velocidad, EQ, sonido espacial, reverb y eco para la musica del menu."},
        {"paimbnails.spatialaudio.menu", "Escenario virtual, profundidad de sala y sonido en movimiento para la musica del menu."},
        {"paimbnails.menuloop.menu", "Tarjeta de reproduccion, hotkeys y tools del loop del menu."},
        {"paimbnails.menuloopshuffle.menu", "Rota el loop del menu cuando se acaba la cancion."},
        {"paimbnails.menuphysics.menu", "Los botones del menu ganan gravedad, giran y rebotan."},
        {"paimbnails.menulayout.menu", "Mueve y redisena los botones del menu principal y de pausa."},
        {"paimbnails.pet.menu", "Una mascota que sigue tu cursor por las capas."},
        {"paimbnails.guide.menu", "Asistente en el juego que responde dudas sobre el mod."},
        {"paimbnails.paidraw.menu", "Canvas de dibujo colaborativo."},
        {"paimbnails.iconmaker.menu", "Crea y aplica iconos personalizados."},
        {"paimbnails.texturestudio.menu", "Generador de texture packs."},
        {"paimbnails.devtools.menu", "Convertidor de GIF a sprite sheet y utilidades."},
        {"paimbnails.community.menu", "Rankings de la comunidad e historial."},
        {"paimbnails.forum.menu", "Publicaciones y respuestas dentro del juego."},
        {"paimbnails.modpreviews.menu", "Imagenes de vista previa para los mods de Geode."},
        {"paimbnails.hovereffects.browser", "Anima las celdas de niveles bajo el cursor."},
        {"paimbnails.compactlist.browser", "Celdas mas cortas para que entren mas niveles."},
        {"paimbnails.autopreview.browser", "Genera una vista previa para niveles sin miniatura."},
        {"paimbnails.leaderboardcells.browser", "Layout y efectos personalizados en las celdas de clasificacion."},
        {"paimbnails.searchpreview.browser", "Resultados mientras escribes en el buscador."},
        {"paimbnails.quicksearch.browser", "Salta directo a un nivel por id desde el buscador."},
        {"paimbnails.searchhistory.browser", "Recuerda y sugiere busquedas anteriores."},
        {"paimbnails.incognito.browser", "Deja de guardar y oculta el historial de busquedas."},
        {"paimbnails.foryou.browser", "Recomienda niveles segun los que juegas."},
        {"paimbnails.levelfilters.browser", "Filtra tus niveles creados por longitud, cancion y mas."},
        {"paimbnails.dynamicsong.level", "Reproduce la cancion del nivel al ver su info."},
        {"paimbnails.imagewarning.level", "Avisa cuando un nivel contiene una imagen o GIF con marca de objetos."},
        {"paimbnails.extendedinfo.info", "Redisena los dos popups de info de un nivel: tus estadisticas con graficos y todos los datos ocultos."},
        {"paimbnails.visibleids.info", "IDs de nivel, lista, comentario y usuario de un vistazo."},
        {"paimbnails.jumptopage.info", "Salta a cualquier pagina del buscador, sin limite de 999."},
        {"paimbnails.advancedsearch.info", "Filtra por rango de id, version del juego, cantidad de objetos y mas."},
        {"paimbnails.searchpresets.info", "Guarda y recarga conjuntos de filtros con nombre."},
        {"paimbnails.progresstracking.info", "Intentos, saltos y muertes registrados por nivel."},
        {"paimbnails.deathheatmap.info", "Muestra donde mueres mas a lo largo del nivel."},
        {"paimbnails.unregprofiles.info", "Abre perfiles de usuarios verdes sin cuenta."},
        {"paimbnails.commenttools.info", "Salta a la pagina de un comentario, ids de comentario y fechas exactas."},
        {"paimbnails.greenusers.info", "Completa usernames que el juego no pudo resolver."},
        {"paimbnails.gdhistory.info", "Opt-in: fechas y autores exactos de history.geometrydash.eu."},
        {"paimbnails.performance.gameplay", "Mejora los FPS con recortes configurables de CPU, GPU y efectos visuales."},
        {"paimbnails.nativeperf.gameplay", "Usa el modo de rendimiento integrado de Geometry Dash."},
        {"paimbnails.glowcut.gameplay", "Evita que se cree el glow de los objetos."},
        {"paimbnails.bgeffects.gameplay", "Desactiva efectos adicionales del fondo durante el nivel."},
        {"paimbnails.gameeffects.gameplay", "Desactiva efectos de gravedad y glitter."},
        {"paimbnails.playereffects.gameplay", "Desactiva estelas, rastros y particulas del jugador."},
        {"paimbnails.dynamicvolume.gameplay", "Pausa el analisis y DSP de Volumen Dinamico durante el gameplay."},
        {"paimbnails.modvisuals.gameplay", "Pausa la mascota, barra custom, efectos de muerte y zoom de pausa."},
        {"paimbnails.autopreview.gameplay", "Evita capturas automaticas de miniaturas durante el nivel."},
        {"paimbnails.perftransitions.gameplay", "Omite las transiciones personalizadas de entrada y salida."},
        {"paimbnails.backgroundcut.gameplay", "Oculta el fondo y el middleground."},
        {"paimbnails.groundcut.gameplay", "Oculta ambas capas del suelo."},
        {"paimbnails.decocut.gameplay", "Oculta los objetos marcados como decoracion."},
        {"paimbnails.gradientcut.gameplay", "Evita actualizar y dibujar capas de gradiente."},
        {"paimbnails.shadercut.gameplay", "Detiene las actualizaciones de shaders del gameplay."},
        {"paimbnails.particlecut.gameplay", "Detiene la actualizacion y dibujo de particulas del nivel."},
        {"paimbnails.levelcut.gameplay", "Oculta flashes, glow y efectos de ondas circulares."},
        {"paimbnails.capture.gameplay", "Boton de captura y keybind para sacar miniaturas."},
        {"paimbnails.progressbar.gameplay", "Redisena, mueve y anima la barra de progreso."},
        {"paimbnails.goldenbest.gameplay", "Pone el porcentaje dorado mientras superas tu mejor marca."},
        {"paimbnails.deatheffects.gameplay", "Efectos personalizados cuando el jugador muere."},
        {"paimbnails.levelentry.gameplay", "Efectos al entrar y salir de un nivel."},
        {"paimbnails.pausezoom.gameplay", "Cerca y oculta el menu de pausa con keybinds."},
        {"paimbnails.profileredesign.profile", "Layout moderno para la pagina de perfil."},
        {"paimbnails.profilemusic.profile", "Musica personalizada al ver perfiles."},
        {"paimbnails.paimonprofiles.profile", "Fotos de perfil, resenas, ratings y visitas."},
        {"paimbnails.globalicons.profile", "Muestra los iconos personalizados de otros jugadores en su perfil."},
        {"paimbnails.badges.profile", "Insignias de rol de Paimbnails junto a los nombres."},
        {"paimbnails.iconcopy.profile", "Copia el set de iconos de alguien desde su perfil y usalo desde la garage."},
        {"paimbnails.emotes.social", "Selector de emotes y rendering en comentarios."},
        {"paimbnails.fonts.social", "Etiquetas de fuente y selector para comentarios."},
        {"paimbnails.mentions.social", "Te avisa cuando alguien te menciona."},
        {"paimbnails.messagesredesign.social", "Lista y lector de mensajes redisenados."},
        {"paimbnails.messagenotifs.social", "Alertas de nuevos mensajes y solicitudes de amistad."},
        {"paimbnails.thumbalerts.social", "Avisa de las miniaturas recien publicadas, sobre la propia miniatura."},
        {"paimbnails.colorpicker.editor", "Cuentagotas para elegir colores en cualquier parte del editor."},
        {"paimbnails.gifimport.editor", "Importa GIF como objetos y triggers nativos optimizados."},
        {"paimbnails.gifrender.editor", "Refina curvas en varias pasadas y construye objetos en segundo plano."},
        {"paimbnails.physics.editor", "Simula cuerpos compuestos y hornea su movimiento como triggers nativos."},
        {"paimbnails.collab.editor", "Edicion colaborativa en tiempo real."},
        {"paimbnails.collabcursors.editor", "Muestra los cursores personalizados de los colaboradores."},
        {"paimbnails.inputscroll.editor", "Usa la rueda sobre los inputs numericos para cambiarlos."},
        {"paimbnails.songsearch.editor", "Escribe un nombre de cancion en la caja de id."},
        {"paimbnails.autoupdate.system", "Descarga e instala actualizaciones al salir."},
        {"paimbnails.diskcache.system", "Mantiene miniaturas en disco para evitar re-descargas."},
        {"paimbnails.robtopcache.system", "Cachea las respuestas del servidor de Geometry Dash."},
        {"paimbnails.discordrpc.system", "Muestra tu actividad en tu perfil de Discord."},
        {"paimbnails.moderation.system", "Reportes, bans y verificacion para el staff."},
        {"paimbnails.debuglogs.system", "Logs detallados para resolver problemas."},
    };
    return map;
}

char const* localize(char const* id, Map const& byId, char const* fallback) {
    if (Localization::get().getLanguage() != Localization::Language::SPANISH) {
        return fallback;
    }
    if (isRequestModule(id)) {
        return fallback;
    }
    auto it = byId.find(id);
    return it != byId.end() ? it->second : fallback;
}

} // namespace

char const* localizedName(Module const& mod) {
    return localize(mod.id, spanishNames(), mod.name);
}

char const* localizedDescription(Module const& mod) {
    return localize(mod.id, spanishDescs(), mod.description);
}

char const* localizedSection(Section section) {
    if (Localization::get().getLanguage() != Localization::Language::SPANISH) {
        return sectionName(section);
    }
    switch (section) {
        case Section::Editor:   return "Editor";
        case Section::Menu:     return "Menu";
        case Section::Browser:  return "Niveles";
        case Section::Level:    return "Info de Nivel";
        case Section::Info:     return "Info";
        case Section::Gameplay: return "Jugabilidad";
        case Section::Profile:  return "Perfil";
        case Section::Social:   return "Social";
        case Section::Global:   return "Interfaz";
        case Section::System:   return "Sistema";
    }
    return "Interfaz";
}

char const* localizedGroup(char const* group) {
    if (Localization::get().getLanguage() != Localization::Language::SPANISH) {
        return group;
    }
    static Map const map = {
        {"Audio", "Audio"},
        {"Browsing", "Explorar"},
        {"Cache", "Cache"},
        {"Capture", "Captura"},
        {"Collab", "Colab"},
        {"Comments", "Comentarios"},
        {"Community", "Comunidad"},
        {"Companion", "Companero"},
        {"Core", "Nucleo"},
        {"Data", "Datos"},
        {"Diagnostics", "Diagnostico"},
        {"Effects", "Efectos"},
        {"Fixes", "Arreglos"},
        {"HUD", "HUD"},
        {"Input", "Entrada"},
        {"Integrations", "Integraciones"},
        {"Interface", "Interfaz"},
        {"Layout", "Layout"},
        {"Level", "Nivel"},
        {"Master", "Maestro"},
        {"Messages", "Mensajes"},
        {"Motion", "Movimiento"},
        {"Navigation", "Navegacion"},
        {"Objects", "Objetos"},
        {"Playtest", "Playtest"},
        {"Performance", "Rendimiento"},
        {"Progress", "Progreso"},
        {"Saving", "Guardado"},
        {"Search", "Busqueda"},
        {"Safety", "Seguridad"},
        {"Shortcuts", "Atajos"},
        {"Skin", "Skin"},
        {"Social", "Social"},
        {"Staff", "Staff"},
        {"Thumbnails", "Miniaturas"},
        {"Tools", "Herramientas"},
        {"Updates", "Actualizaciones"},
        {"Visual", "Visual"},
        {"Visual Cuts", "Recortes Visuales"},
    };
    auto it = map.find(group);
    return it != map.end() ? it->second : group;
}

} // namespace paimon::modules
