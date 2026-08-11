#pragma once

// Convierte la respuesta cruda de history.geometrydash.eu en la linea de tiempo
// que dibuja LevelHistoryPopup.
//
// La API guarda un snapshot cada vez que alguien miro el nivel: vienen sin
// ordenar y con casi todos los campos en null cuando quien lo miro solo vio la
// lista de busqueda. Aqui se ordenan por fecha, se deduce lo que falta (la cara
// de dificultad sale de las estrellas cuando el snapshot no trae los votos) y se
// marcan los cambios reales entre snapshots: el rate, el feature y las
// versiones que subio el creador.

#include <Geode/Enums.hpp>
#include <matjson.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::info {

enum class HistoryMilestone {
    First,       // el snapshot mas viejo que existe del nivel
    Rated,       // aparecieron las estrellas
    Restarred,   // cambio la cantidad de estrellas
    Unrated,     // le quitaron el rate
    Difficulty,  // cambio la cara sin cambiar las estrellas
    Featured,
    Epic,        // epic, legendary o mythic: el tier sale de `feature`
    Unfeatured,
    Version,     // el creador subio una version nueva
};

struct HistoryEntry {
    matjson::Value raw;      // el registro tal cual, para la ficha de detalle
    std::string date;        // YYYY-MM-DD
    std::string clock;       // HH:MM, vacio cuando el snapshot solo guarda el dia
    std::string levelName;
    std::string username;
    std::string song;
    std::string source;      // de donde salio el snapshot, ya legible
    int64_t recordID = 0;
    int64_t downloads = -1;  // -1 = el snapshot no lo trae
    int64_t likes = -1;
    int64_t objects = -1;
    int version = 0;
    int stars = 0;
    int face = 0;            // valor que entiende GJDifficultySprite
    int length = -1;
    int coins = -1;
    GJFeatureState feature = GJFeatureState::None;
    bool hasRating = false;  // el snapshot sabe si el nivel tenia rate
    bool coinsVerified = false;
    bool invalid = false;
    std::vector<HistoryMilestone> milestones;  // el mas importante va primero
};

struct LevelHistory {
    std::vector<HistoryEntry> entries;  // cronologico, del mas viejo al mas nuevo

    // Estado de hoy, tal como lo tiene cacheado la API.
    std::string levelName;
    std::string username;
    std::string song;
    std::string deletedDate;
    int64_t downloads = -1;
    int64_t likes = -1;
    int64_t objects = -1;
    int stars = 0;
    int face = 0;
    int length = -1;
    int version = 0;
    int dailyID = 0;
    GJFeatureState feature = GJFeatureState::None;
    bool deleted = false;

    // Indices dentro de `entries`. -1 cuando el momento no quedo registrado:
    // pasa cuando el nivel ya llegaba rateado al primer snapshot.
    int rateIndex = -1;
    int featureIndex = -1;
    int versionIndex = -1;
};

struct HistoryField {
    std::string label;
    std::string value;
    bool accent = false;
};

// Devuelve una historia vacia si la respuesta no trae registros.
LevelHistory parseLevelHistory(matjson::Value const& root);

// Texto corto del hito, ya con sus numeros ("RATE 10", "v3", "LEGENDARY").
std::string milestoneLabel(HistoryEntry const& entry, HistoryMilestone milestone);

// Todo lo que ese snapshot llego a guardar, en orden de lectura y sin los
// campos vacios. Es lo que dibuja la ficha de detalle.
std::vector<HistoryField> describeEntry(HistoryEntry const& entry);

} // namespace paimon::info
