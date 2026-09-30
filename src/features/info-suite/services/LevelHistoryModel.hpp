#pragma once

// snapshots de history.geometrydash.eu a linea de tiempo: se ordenan, se deduce
// lo que falta y se marcan los cambios reales (rate, feature, versiones).

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
    std::string date;        // yyyy-mm-dd
    std::string clock;       // hh:mm, vacio cuando el snapshot solo guarda el dia
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
    int face = 0;            // valor que entiende gjdifficultysprite
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

    // estado de hoy, tal como lo tiene cacheado la api.
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

    // indices dentro de `entries`. -1 cuando el momento no quedo registrado:
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

// devuelve una historia vacia si la respuesta no trae registros.
LevelHistory parseLevelHistory(matjson::Value const& root);

// texto corto del hito, ya con sus numeros ("rate 10", "v3", "legendary").
std::string milestoneLabel(HistoryEntry const& entry, HistoryMilestone milestone);

// todo lo que ese snapshot llego a guardar, en orden de lectura y sin los
// campos vacios. es lo que dibuja la ficha de detalle.
std::vector<HistoryField> describeEntry(HistoryEntry const& entry);

} // namespace paimon::info
