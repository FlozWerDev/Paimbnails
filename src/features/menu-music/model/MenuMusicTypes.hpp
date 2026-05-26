#pragma once

// MenuMusicTypes — tipos de datos centrales del sistema "Menu Music".
//
// El objetivo es separar con claridad el MODELO de datos (track, playlist,
// origen) de los servicios que lo consumen (library, player, downloader).
// Esto evita el antipatron del reference mod donde un unico SongManager
// conoce de todo (FMOD, disco, playlists, sort, search...).

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace paimon::menumusic {

// ── Origen del track ──────────────────────────────────────────
//
// Sirve para decidir el icono mostrado y la lógica de remove
// (borramos archivos descargados, no importados).
enum class TrackSource : std::uint8_t {
    Unknown = 0,
    Local,          // archivo importado por el usuario, no lo tocamos
    Downloaded,     // descargado via yt-dlp, lo podemos re-descargar / borrar
    Vanilla,        // cancion del propio GD (menuLoop.mp3 etc). No se lista.
};

// ── Un track en la libreria ──────────────────────────────────
//
// displayName = lo que se muestra en toda la UI.
// audioPath = path absoluto en disco (.mp3/.ogg/.wav/.flac/.m4a/.opus).
// coverPath = imagen local, o vacio si no hay.
// sourceUrl = link original, solo si fue descargado.
// addedUnixMs = timestamp para ordenar por fecha.
// durationMs = duracion si se conoce (0 = desconocida).
struct MusicTrack {
    std::string id;
    std::string audioPath;
    std::string coverPath;
    std::string displayName;
    std::string artist;
    std::string sourceUrl;
    TrackSource source = TrackSource::Local;
    std::int64_t addedUnixMs = 0;
    std::int32_t durationMs = 0;
};

// ── Playlist ──────────────────────────────────────────────────
//
// Simple wrapper sobre ids; no duplica datos de track.
struct MusicPlaylist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
    std::int64_t createdUnixMs = 0;
};

// ── Modo de reproduccion ──────────────────────────────────────
//
// Library = shuffle sobre TODA la libreria (default).
// Playlist = shuffle sobre la playlist activa.
// Queue = reproduce un track especifico pinchado desde la UI.
// Disabled = no override, deja al menu loop vanilla del juego.
enum class PlaybackMode : std::uint8_t {
    Disabled = 0,
    Library,
    Playlist,
    Queue,
};

// ── Estado de reproduccion ────────────────────────────────────
//
// Lo usa MenuMusicPlayer para notificar a la UI.
struct PlaybackState {
    std::string currentTrackId;
    std::string currentAudioPath;
    PlaybackMode mode = PlaybackMode::Disabled;
    bool isPlaying = false;
    std::int32_t positionMs = 0;
    std::int32_t lengthMs = 0;
};

} // namespace paimon::menumusic
