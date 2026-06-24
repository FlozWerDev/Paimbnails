#pragma once

#include "FeatureInfoPopup.hpp"
#include <vector>

// Paimon Hub feature descriptions.

namespace paimon::ui {

inline std::vector<InfoSection> getGeneralInfo() {
    return {
        {"Idioma", "Cambia el idioma de toda la interfaz de Paimbnails. Afecta textos del Hub, popups, notificaciones y paneles.", {120, 255, 120}},
        {"Auto Update", "Cuando se detecta una nueva version al iniciar el juego, se descarga en segundo plano y se aplica al cerrar GD.", {100, 200, 255}},
        {"Quick Search Key", "Tecla para buscar niveles rapido en el buscador. Se puede cambiar a cualquier tecla.", {255, 200, 100}},
        {"Realtime Search", "Muestra resultados mientras escribes en el buscador. Tiene un delay configurable para no saturar el servidor.", {255, 170, 220}},
        {"Debug Logs", "Activa logs detallados en la consola de Geode. Solo util para desarrolladores.", {180, 180, 180}},
        {"Mantenimiento", "Limpia archivos temporales, cache de thumbnails y GIFs. Util si el mod se siente lento.", {255, 150, 150}},
    };
}

inline std::vector<InfoSection> getThumbnailsInfo() {
    return {
        {"Thumbnail Size", "Controla el ancho del area de miniatura en las celdas de nivel. Se aplica en listas de niveles (busqueda, saved, featured).", {100, 200, 255}},
        {"Background Style", "Elige entre gradiente de colores o miniatura borrosa como fondo de la celda.", {100, 200, 255}},
        {"Background Blur/Darkness", "Intensidad del blur y oscuridad del fondo. Solo aplica cuando el estilo es 'thumbnail'.", {100, 200, 255}},
        {"Galeria Auto-Cycle", "Si un nivel tiene varias fotos, las rota automaticamente en la celda de la lista.", {180, 255, 140}},
        {"Transiciones de Galeria", "Estilo de animacion al cambiar entre fotos (crossfade, slide, cube, etc). Aplica en celdas y en el popup de galeria.", {180, 255, 140}},
        {"Hover Effects", "Animacion al pasar el mouse sobre una celda de nivel. Incluye zoom, bounce, shake, etc.", {255, 200, 100}},
        {"Color Effect", "Filtro de color durante la animacion hover (sepia, glitch, rainbow, etc).", {255, 200, 100}},
        {"Compact Mode", "Hace las celdas mas cortas para ver mas niveles a la vez. Aplica en listas.", {255, 170, 220}},
        {"Captura", "Boton en el menu de pausa para tomar screenshots como thumbnails. La tecla es configurable.", {255, 150, 150}},
    };
}

inline std::vector<InfoSection> getLevelInfoScreenInfo() {
    return {
        {"Background Style", "Estilo visual del fondo en la pantalla de info del nivel. Opciones: blur, pixel, grayscale, neon-pulse, matrix, CRT, etc.", {180, 220, 255}},
        {"Extra Effects", "Efectos adicionales que se aplican encima del estilo principal (max 4, separados por coma). Multi-pass de shaders.", {180, 220, 255}},
        {"Effect Intensity", "Fuerza del efecto de fondo (1-10). Afecta blur radius, pixel size, etc segun el estilo.", {180, 220, 255}},
        {"Background Darkness", "Oscuridad del overlay sobre el thumbnail de fondo. 0 = transparente, 50 = negro total.", {180, 220, 255}},
        {"Dynamic Song", "Reproduce la cancion del nivel en una posicion aleatoria al ver su info. Se detiene al salir.", {255, 200, 100}},
        {"Stream Preview", "Si la cancion no esta descargada, hace streaming de un preview que se repite hasta que la descargues.", {255, 200, 100}},
        {"Popup Transitions", "Animacion al navegar entre fotos en el popup de galeria del nivel.", {180, 255, 140}},
        {"Background Transitions", "Animacion del fondo cuando cambia la foto (auto-cycle o navegacion manual).", {180, 255, 140}},
    };
}

inline std::vector<InfoSection> getAudioInfo() {
    return {
        {"Profile Music", "Musica personalizada que suena al ver perfiles de otros jugadores. Cada usuario puede configurar su fragmento.", {255, 170, 220}},
        {"Crossfade", "Transicion suave entre la musica de fondo y la musica de perfil, en vez de cortar abruptamente.", {255, 170, 220}},
        {"Menu Music Player", "Reproductor con vinilo, biblioteca, playlists y descargas via yt-dlp. Se abre desde el boton de vinilo en el menu.", {100, 200, 255}},
        {"Menu Loop", "Sistema de randomizacion del menu loop de GD. Soporta shuffle, blacklist, favoritos y playlists .txt.", {100, 200, 255}},
        {"Capas Musicales", "Musica diferente por pantalla (menu, editor, browser, etc). Soporta filtros de audio como cave, underwater, nightcore.", {180, 255, 140}},
    };
}

inline std::vector<InfoSection> getBackgroundsInfo() {
    return {
        {"Fondos por Capa", "Cada pantalla del juego puede tener su propio fondo: imagen, video, random o un ID de GD.", {180, 255, 140}},
        {"Capas disponibles", "Menu, Level Info, Level Select, Creator, Browser, Search, Leaderboards, Profile.", {180, 255, 140}},
        {"Dark Mode", "Overlay oscuro sobre el fondo. Intensidad configurable por capa.", {100, 200, 255}},
        {"Shaders", "Filtros visuales por capa: grayscale, sepia, bloom, chromatic, pixelate, etc.", {100, 200, 255}},
        {"Video Backgrounds", "Soporta videos como fondo. Configurable: FPS, audio, calidad, blur, rotacion.", {255, 200, 100}},
        {"Transiciones", "Animaciones al cambiar de escena. Estilos: fade, slide, zoom, flip, cube, etc.", {255, 170, 220}},
        {"Transparent Background", "Quita el tinte azul de GD en pantallas que no tienen fondo custom de Paimbnails.", {180, 180, 180}},
    };
}

inline std::vector<InfoSection> getExtrasInfo() {
    return {
        {"Mascota (Pet)", "Un companero animado que sigue tu cursor. Configurable: escala, bounce, trail, idle animation, squish.", {255, 200, 100}},
        {"Custom Cursor", "Reemplaza el cursor del sistema con imagenes propias. Estados Idle/Move/Hover/Click, trail, e importacion de cursores de Windows (.cur/.ani) y packs .zip. Se oculta en gameplay.", {100, 200, 255}},
        {"Beat Shaders", "Fondos shader animados que reaccionan al ritmo de la musica. Aplican sobre cualquier fondo (default, imagen, video, GIF).", {255, 100, 220}},
        {"Dynamic Popup", "Animaciones custom para todos los popups de Paimbnails: slide, elastic, bounce, flip, fold.", {180, 255, 140}},
        {"Popup Blur", "Blur del fondo detras de los popups. Estilos: gaussian o paimonblur. Configurable intensidad y bordes.", {180, 255, 140}},
        {"Profile Image", "Editor de foto de perfil con formas, decoraciones y preview en vivo.", {255, 170, 220}},
        {"Rendimiento", "Cache de GIFs en RAM, threads de descarga, disk cache. Ajusta segun tu hardware.", {255, 150, 150}},
        {"Para Ti (For You)", "Recomendaciones personalizadas de niveles basadas en tu historial de juego.", {120, 255, 120}},
    };
}

inline std::vector<InfoSection> getDiscordInfo() {
    return {
        {"Rich Presence", "Muestra tu actividad de Paimbnails/GD en tu perfil de Discord. Se actualiza en tiempo real.", {110, 150, 255}},
        {"Activity Type", "Como Discord etiqueta tu actividad: Playing, Listening, Watching o Competing.", {110, 150, 255}},
        {"Banner", "La imagen grande de tu status. Puede ser: logo de Paimbnails, tu fondo de perfil, un asset de Discord o una URL custom.", {100, 200, 255}},
        {"Private Mode", "Oculta detalles sensibles como nombres de niveles, creadores e info de perfil.", {255, 200, 100}},
        {"Custom Texts", "Puedes reemplazar las lineas auto-generadas con texto personalizado (Details y State).", {180, 255, 140}},
        {"Buttons", "Dos botones configurables debajo de tu status con texto y URL custom (max 32 chars por label).", {255, 170, 220}},
        {"Idle When Unfocused", "Cambia tu status a idle cuando GD no es la ventana activa.", {180, 180, 180}},
        {"Elapsed Time", "Muestra un contador de tiempo desde que Paimbnails inicio.", {180, 180, 180}},
    };
}

} // namespace paimon::ui
