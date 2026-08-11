#pragma once

#include "../../src/features/guide/services/ConversationalEngine.hpp"
#include <string>
#include <vector>

// Test fixture mirroring GuideTopicKnowledge.cpp (hand-curated topics) so the
// conversation harness can exercise ConversationalEngine host-side without
// the Geode SDK. Keep in sync if the knowledge table changes.

namespace paimon::guide::test {

inline std::vector<TopicKnowledge> makeTopics() {
    std::vector<TopicKnowledge> out;

    {
        TopicKnowledge t;
        t.topicId = "custom-cursor";
        t.enName = "Custom Cursor";
        t.esName = "Cursor Personalizado";
        t.subtopics = {
            {"cursor-color", {"color", "tint", "hue", "colour", "colours"},
             {"color", "tono", "matiz", "colores"},
             "cursor color reply", "respuesta color cursor", "Color", "Color"},
            {"cursor-size", {"size", "scale", "bigger", "smaller", "large"},
             {"tamano", "escala", "mas grande", "mas pequeno", "grande"},
             "cursor size reply", "respuesta tamano cursor", "Size", "Tamano"},
            {"cursor-image", {"image", "picture", "custom", "own", "png", "sprite"},
             {"imagen", "foto", "propio", "propia", "png", "sprite"},
             "cursor image reply", "respuesta imagen cursor", "Own image", "Imagen propia"},
            {"cursor-transitions", {"transition", "animation", "smooth", "move", "trail"},
             {"transicion", "animacion", "suave", "movimiento", "estela"},
             "cursor transitions reply", "respuesta transiciones cursor", "Effects", "Efectos"},
        };
        t.enMoreReply = "more cursor";
        t.esMoreReply = "mas cursor";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "menu-music";
        t.enName = "Menu Music";
        t.esName = "Musica del Menu";
        t.subtopics = {
            {"music-playlists", {"playlist", "playlists", "list", "song list"},
             {"playlist", "playlists", "lista", "lista de canciones"},
             "playlist reply", "respuesta playlist", "Playlists", "Playlists"},
            {"music-library", {"library", "songs", "download", "my songs", "browse"},
             {"biblioteca", "canciones", "descargar", "mis canciones", "navegar"},
             "library reply", "respuesta biblioteca", "Library", "Biblioteca"},
            {"music-hotkeys", {"hotkey", "hotkeys", "key", "keys", "shortcut", "seek"},
             {"tecla", "teclas", "atajo", "atajos", "buscar"},
             "hotkeys reply", "respuesta teclas", "Hotkeys", "Teclas"},
            {"music-shuffle", {"shuffle", "random", "constant", "loop"},
             {"aleatorio", "random", "constante", "loop"},
             "shuffle reply", "respuesta aleatorio", "Shuffle", "Aleatorio"},
        };
        t.enMoreReply = "more music";
        t.esMoreReply = "mas musica";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "scene-background";
        t.enName = "Scene Background";
        t.esName = "Fondo de Escena";
        t.subtopics = {
            {"bg-video", {"video", "mp4", "animated", "gif"},
             {"video", "mp4", "animado", "gif"},
             "video reply", "respuesta video", "Video", "Video"},
            {"bg-blur", {"blur", "blurry", "blurred", "blur effect"},
             {"blur", "borroso", "desenfoque", "efecto blur"},
             "blur reply", "respuesta blur", "Blur", "Blur"},
            {"bg-per-screen", {"per screen", "per layer", "menu", "search", "level select", "profile"},
             {"por pantalla", "por capa", "menu", "busqueda", "seleccion", "perfil"},
             "per screen reply", "respuesta por pantalla", "Per screen", "Por pantalla"},
            {"bg-gradient", {"gradient", "shader", "gradients", "color"},
             {"degradado", "shader", "gradientes", "color"},
             "gradient reply", "respuesta degradado", "Gradient", "Degradado"},
        };
        t.enMoreReply = "more bg";
        t.esMoreReply = "mas fondo";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "thumbnail-settings";
        t.enName = "Thumbnail Settings";
        t.esName = "Ajustes de Miniaturas";
        t.subtopics = {
            {"thumb-size", {"size", "width", "bigger", "smaller", "large"},
             {"tamano", "ancho", "mas grande", "mas pequeno", "grande"},
             "thumb size reply", "respuesta tamano thumb", "Size", "Tamano"},
            {"thumb-quality", {"quality", "resolution", "sharp", "hd"},
             {"calidad", "resolucion", "nitida", "hd"},
             "thumb quality reply", "respuesta calidad thumb", "Quality", "Calidad"},
            {"thumb-order", {"order", "reorder", "sort", "arrange"},
             {"orden", "reordenar", "ordenar", "acomodar"},
             "thumb order reply", "respuesta orden thumb", "Order", "Orden"},
            {"thumb-capture", {"capture", "take", "shoot", "screenshot", "clean"},
             {"capturar", "tomar", "sacar", "screenshot", "limpia"},
             "thumb capture reply", "respuesta captura thumb", "Capture", "Captura"},
        };
        t.enMoreReply = "more thumbs";
        t.esMoreReply = "mas miniaturas";
        out.push_back(std::move(t));
    }

    {
        TopicKnowledge t;
        t.topicId = "colorful-icons";
        t.enName = "Paimon Icons (Recolor)";
        t.esName = "Iconos Paimon (Recolor)";
        t.subtopics = {
            {"icons-recolor", {"recolor", "recolour", "color", "paint", "change color"},
             {"recolorear", "color", "pintar", "cambiar color"},
             "recolor reply", "respuesta recolor", "Recolor", "Recolor"},
            {"icons-gradient", {"gradient", "gradients", "fade"},
             {"degradado", "degradados", "fade"},
             "icons gradient reply", "respuesta degradado iconos", "Gradient", "Degradado"},
            {"icons-rainbow", {"rainbow", "multi color"},
             {"arcoiris", "multicolor"},
             "rainbow reply", "respuesta arcoiris", "Rainbow", "Arcoiris"},
        };
        t.enMoreReply = "more icons";
        t.esMoreReply = "mas iconos";
        out.push_back(std::move(t));
    }

    return out;
}

} // namespace paimon::guide::test
