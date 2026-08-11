#include "LevelHistoryModel.hpp"
#include "LevelFacts.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::info {

namespace {

std::string text(matjson::Value const& value) {
    return value.asString().unwrapOr("");
}

int64_t number(matjson::Value const& value, int64_t fallback = 0) {
    if (!value.isNumber()) return fallback;
    return value.asInt().unwrapOr(fallback);
}

// La API mezcla booleanos de verdad con los 0/1 que devolvia el servidor.
bool truthy(matjson::Value const& value) {
    if (value.isBool()) return value.asBool().unwrapOr(false);
    return value.isNumber() && value.asInt().unwrapOr(0) != 0;
}

std::string dayOf(std::string const& iso) {
    return iso.size() >= 10 ? iso.substr(0, 10) : iso;
}

// Los snapshots que vienen de listados solo guardan el dia; los de descarga
// traen la hora exacta y vale la pena ensenarla.
std::string clockOf(std::string const& iso) {
    if (iso.size() < 19 || iso[10] != 'T') return "";
    if (iso.compare(11, 8, "00:00:00") == 0) return "";
    return iso.substr(11, 5);
}

std::string timestampOf(matjson::Value const& record) {
    auto stamp = text(record["real_date"]);
    if (stamp.empty()) stamp = text(record["cache_real_date"]);
    return stamp;
}

std::string sourceName(std::string const& type) {
    if (type == "download") return "Descarga";
    if (type == "get") return "Consulta";
    if (type == "manual") return "Manual";
    if (type.rfind("glm_", 0) == 0) return "GLM";
    return type.empty() ? "Registro" : type;
}

// Mismo mapa que usa el juego para pintar la cara de un nivel sin votos.
int faceFromStars(int stars) {
    switch (stars) {
        case 1:  return -1;
        case 2:  return 1;
        case 3:  return 2;
        case 4:
        case 5:  return 3;
        case 6:
        case 7:  return 4;
        case 8:
        case 9:  return 5;
        default: return stars >= 10 ? 6 : 0;
    }
}

int demonFace(int demonType) {
    switch (demonType) {
        case 3:  return 7;
        case 4:  return 8;
        case 5:  return 9;
        case 6:  return 10;
        default: return 6;
    }
}

// Los votos de dificultad llegan como numerador y denominador, igual que en la
// respuesta del servidor de RobTop.
int faceOf(matjson::Value const& record, int stars) {
    if (truthy(record["auto"])) return -1;
    if (truthy(record["demon"])) return demonFace(static_cast<int>(number(record["demon_type"])));

    auto denominator = number(record["rating"]);
    auto numerator = number(record["rating_sum"]);
    if (denominator > 0 && numerator > 0) {
        auto face = numerator / denominator;
        if (face >= 6) return 6;
        if (face >= 1) return static_cast<int>(face);
    }
    return faceFromStars(stars);
}

// Cierto solo cuando el snapshot guarda de verdad la dificultad, no cuando la
// deducimos de las estrellas: comparar caras aproximadas invents cambios que
// nunca ocurrieron.
bool faceIsExact(matjson::Value const& record) {
    if (truthy(record["demon"])) return record["demon_type"].isNumber();
    return number(record["rating"]) > 0;
}

GJFeatureState featureFromTiers(int64_t epic, int64_t featureScore) {
    switch (epic) {
        case 1:  return GJFeatureState::Epic;
        case 2:  return GJFeatureState::Legendary;
        case 3:  return GJFeatureState::Mythic;
        default: break;
    }
    return featureScore > 0 ? GJFeatureState::Featured : GJFeatureState::None;
}

// Hacen falta las dos mitades: sin el tier epic un nivel legendary pasaria por
// featured, y sin la puntuacion un nivel featured pasaria por normal. A medias
// sirven para pintar, no para decidir que cambio.
bool featureIsExact(matjson::Value const& record) {
    return record["epic"].isNumber() && record["feature_score"].isNumber();
}

// Los volcados de GLM son bases de datos enteras que sube la gente: la fecha es
// cuando se subio el volcado, no cuando se miro el nivel, asi que hay entradas
// de 2025 con datos de 2017 (y por eso las descargas van hacia atras). Se
// ensenan en la lista, pero no pueden decidir cuando cambio nada.
bool isLiveSnapshot(matjson::Value const& record) {
    return text(record["record_type"]).rfind("glm_", 0) != 0;
}

HistoryMilestone featureMilestone(GJFeatureState state) {
    switch (state) {
        case GJFeatureState::None:     return HistoryMilestone::Unfeatured;
        case GJFeatureState::Featured: return HistoryMilestone::Featured;
        default:                       return HistoryMilestone::Epic;
    }
}

std::string usernameOf(matjson::Value const& record) {
    auto name = text(record["username"]);
    if (name.empty()) name = text(record["cached_user_info"]["username"]);
    if (name.empty()) name = text(record["cached_user_info"]["non_player_username"]);
    if (name.empty()) name = text(record["real_user_record"]["username"]);
    return name;
}

std::string songOf(matjson::Value const& record) {
    auto name = text(record["song"]["song_name"]);
    if (name.empty()) return "";
    auto artist = text(record["song"]["artist_name"]);
    return artist.empty() ? name : fmt::format("{} - {}", name, artist);
}

HistoryEntry buildEntry(matjson::Value const& record) {
    HistoryEntry entry;
    entry.raw = record;

    auto stamp = timestampOf(record);
    entry.date = dayOf(stamp);
    entry.clock = clockOf(stamp);

    entry.levelName = text(record["level_name"]);
    entry.username = usernameOf(record);
    entry.song = songOf(record);
    entry.source = sourceName(text(record["record_type"]));
    entry.recordID = number(record["id"]);
    entry.invalid = truthy(record["is_invalid"]);

    entry.hasRating = record["stars"].isNumber();
    entry.stars = static_cast<int>(number(record["stars"]));
    entry.face = faceOf(record, entry.stars);
    entry.feature = featureFromTiers(number(record["epic"]), number(record["feature_score"]));

    entry.version = static_cast<int>(number(record["level_version"]));
    entry.downloads = number(record["downloads"], -1);
    entry.likes = number(record["likes"], -1);
    entry.objects = number(record["objects_count"], -1);
    entry.length = static_cast<int>(number(record["length"], -1));
    entry.coins = static_cast<int>(number(record["coins"], -1));
    entry.coinsVerified = truthy(record["coins_verified"]);
    return entry;
}

// El primer snapshot no marca hitos: solo sabemos que el nivel ya estaba asi,
// no cuando llego a estarlo. Marcarlo mentiria sobre la fecha del rate.
void markMilestones(std::vector<matjson::Value> const& records, LevelHistory& history) {
    bool ratingSeeded = false;
    bool featureSeeded = false;
    int lastStars = 0;
    int lastFace = 0;
    int lastVersion = 0;
    bool lastFaceExact = false;
    auto lastFeature = GJFeatureState::None;

    for (size_t i = 0; i < history.entries.size(); i++) {
        auto& entry = history.entries[i];
        auto const& record = records[i];
        auto index = static_cast<int>(i);

        if (i == 0) entry.milestones.push_back(HistoryMilestone::First);
        if (!isLiveSnapshot(record)) continue;

        if (entry.hasRating) {
            bool exact = faceIsExact(record);
            if (!ratingSeeded) {
                ratingSeeded = true;
            } else if (entry.stars > 0 && lastStars == 0) {
                entry.milestones.push_back(HistoryMilestone::Rated);
                if (history.rateIndex < 0) history.rateIndex = index;
            } else if (entry.stars == 0 && lastStars > 0) {
                entry.milestones.push_back(HistoryMilestone::Unrated);
            } else if (entry.stars != lastStars) {
                entry.milestones.push_back(HistoryMilestone::Restarred);
            } else if (entry.stars > 0 && exact && lastFaceExact && entry.face != lastFace) {
                // Solo cuenta despues del rate. En un nivel sin calificar la
                // cara sale de los votos de la gente y baila sola cada dia.
                entry.milestones.push_back(HistoryMilestone::Difficulty);
            }
            lastStars = entry.stars;
            if (exact || !lastFaceExact) {
                lastFace = entry.face;
                lastFaceExact = exact;
            }
        }

        if (featureIsExact(record)) {
            if (!featureSeeded) {
                featureSeeded = true;
            } else if (entry.feature != lastFeature) {
                entry.milestones.push_back(featureMilestone(entry.feature));
                if (history.featureIndex < 0 && entry.feature > lastFeature) {
                    history.featureIndex = index;
                }
            }
            lastFeature = entry.feature;
        }

        if (entry.version > 0) {
            if (lastVersion > 0 && entry.version > lastVersion) {
                entry.milestones.push_back(HistoryMilestone::Version);
                history.versionIndex = index;
            }
            lastVersion = std::max(lastVersion, entry.version);
        }
    }
}

// Lo de hoy sale del cache propio de la API: los snapshots viejos que suben los
// usuarios pueden llegar desordenados y con contadores atrasados.
void fillCurrentState(matjson::Value const& root, LevelHistory& history) {
    history.levelName = text(root["cache_level_name"]);
    history.username = text(root["cache_username"]);
    history.downloads = number(root["cache_downloads"], -1);
    history.likes = number(root["cache_likes"], -1);
    history.objects = number(root["cache_object_count"], -1);
    history.length = static_cast<int>(number(root["cache_length"], -1));
    history.stars = static_cast<int>(number(root["cache_stars"]));
    history.feature = featureFromTiers(number(root["cache_epic"]), number(root["cache_featured"]));
    history.dailyID = static_cast<int>(number(root["cache_daily_id"]));
    history.deleted = truthy(root["is_deleted"]);
    history.deletedDate = dayOf(text(root["deleted_date"]));

    // La cara necesita saber si es demon y de que tipo, y eso solo lo guardan
    // los snapshots completos: el cache de la API se queda en las estrellas.
    // Con un volcado de GLM nos conformamos si no hay nada en vivo.
    auto newestExactFace = [&history](bool liveOnly) {
        for (auto it = history.entries.rbegin(); it != history.entries.rend(); ++it) {
            if (!it->hasRating || it->face == 0 || !faceIsExact(it->raw)) continue;
            if (liveOnly && !isLiveSnapshot(it->raw)) continue;
            return it->face;
        }
        return 0;
    };

    history.face = faceFromStars(history.stars);
    if (int face = newestExactFace(true)) history.face = face;
    else if (int face = newestExactFace(false)) history.face = face;

    for (auto const& entry : history.entries) {
        history.version = std::max(history.version, entry.version);
    }

    for (auto it = history.entries.rbegin(); it != history.entries.rend(); ++it) {
        if (history.levelName.empty()) history.levelName = it->levelName;
        if (history.username.empty()) history.username = it->username;
        if (history.song.empty()) history.song = it->song;
        if (!history.levelName.empty() && !history.username.empty() && !history.song.empty()) {
            break;
        }
    }
}

} // namespace

LevelHistory parseLevelHistory(matjson::Value const& root) {
    LevelHistory history;
    if (!root.isObject()) return history;

    auto records = root["records"].asArray();
    if (!records.isOk()) return history;

    auto sorted = records.unwrap();
    if (sorted.empty()) return history;

    // El formato ISO ordena bien comparando texto, y el id desempata los
    // snapshots que cayeron el mismo dia.
    std::stable_sort(sorted.begin(), sorted.end(),
        [](matjson::Value const& a, matjson::Value const& b) {
            auto left = timestampOf(a);
            auto right = timestampOf(b);
            if (left != right) return left < right;
            return number(a["id"]) < number(b["id"]);
        });

    history.entries.reserve(sorted.size());
    for (auto const& record : sorted) {
        history.entries.push_back(buildEntry(record));
    }

    markMilestones(sorted, history);
    fillCurrentState(root, history);
    return history;
}

std::string milestoneLabel(HistoryEntry const& entry, HistoryMilestone milestone) {
    switch (milestone) {
        case HistoryMilestone::First:      return "PRIMER DATO";
        case HistoryMilestone::Rated:      return fmt::format("RATE {}", entry.stars);
        case HistoryMilestone::Restarred:  return fmt::format("AHORA {}", entry.stars);
        case HistoryMilestone::Unrated:    return "SIN RATE";
        case HistoryMilestone::Difficulty: return "NUEVA DIFICULTAD";
        case HistoryMilestone::Featured:   return "FEATURED";
        case HistoryMilestone::Unfeatured: return "SIN FEATURE";
        case HistoryMilestone::Version:    return fmt::format("v{}", entry.version);
        case HistoryMilestone::Epic:       break;
    }
    switch (entry.feature) {
        case GJFeatureState::Legendary: return "LEGENDARY";
        case GJFeatureState::Mythic:    return "MYTHIC";
        default:                        return "EPIC";
    }
}

std::vector<HistoryField> describeEntry(HistoryEntry const& entry) {
    std::vector<HistoryField> out;
    out.reserve(32);

    auto const& record = entry.raw;

    auto add = [&out](std::string label, std::string value, bool accent = false) {
        if (value.empty()) return;
        out.push_back({std::move(label), std::move(value), accent});
    };
    auto addCount = [&](std::string label, matjson::Value const& value, bool accent = false) {
        if (!value.isNumber()) return;
        add(std::move(label), formatThousands(number(value)), accent);
    };
    auto addFlag = [&](std::string label, matjson::Value const& value) {
        if (!value.isBool() && !value.isNumber()) return;
        add(std::move(label), truthy(value) ? "Si" : "No");
    };

    add("Fecha", entry.clock.empty() ? entry.date : fmt::format("{}  {}", entry.date, entry.clock),
        true);
    add("Origen", entry.source);
    add("ID del registro", fmt::format("#{}", entry.recordID));

    for (auto milestone : entry.milestones) {
        add("Hito", milestoneLabel(entry, milestone), true);
    }

    add("Nombre", entry.levelName);
    if (!truthy(record["description_encoded"])) add("Descripcion", text(record["description"]));

    add("Creador", entry.username);
    addCount("User ID", record["real_user_record"]["user_id"]);
    addCount("Account ID", record["cached_user_info"]["account_id"]);

    add("Dificultad", difficultyFaceName(entry.face), true);
    if (entry.hasRating) add("Estrellas", std::to_string(entry.stars), true);
    addCount("Estrellas pedidas", record["requested_stars"]);
    if (number(record["rating"]) > 0) {
        add("Votos de dificultad", fmt::format("{} / {}",
            number(record["rating_sum"]), number(record["rating"])));
    }
    add("Estado", featureName(static_cast<int>(number(record["feature_score"])),
                              static_cast<int>(number(record["epic"]))), true);

    addCount("Descargas", record["downloads"]);
    addCount("Likes", record["likes"]);
    addCount("Dislikes", record["dislikes"]);
    addCount("Objetos", record["objects_count"]);
    add("Duracion", lengthName(entry.length));

    if (entry.coins > 0) {
        add("Monedas", fmt::format("{} ({})", entry.coins,
            entry.coinsVerified ? "verificadas" : "sin verificar"));
    }
    addFlag("Dos jugadores", record["two_player"]);

    if (entry.version > 0) add("Version del nivel", fmt::format("v{}", entry.version));
    auto gameVersion = number(record["game_version"]);
    if (gameVersion > 0) {
        // RobTop guarda 2.0 como 20 y 2.1 como 21; por debajo de 10 es un 1.x.
        add("Version del juego", gameVersion < 10
            ? fmt::format("1.{}", gameVersion)
            : fmt::format("{}.{}", gameVersion / 10, gameVersion % 10));
    }
    if (number(record["original"]) > 0) {
        add("Copia de", fmt::format("#{}", number(record["original"])));
    }

    add("Cancion", entry.song);
    addCount("Song ID", record["song"]["online_id"]);
    addCount("Track oficial", record["official_song"]);

    add("Tiempo de edicion", formatDuration(static_cast<int>(
        number(record["seconds_spent_editing"]))));
    add("Tiempo de edicion (copia)", formatDuration(static_cast<int>(
        number(record["seconds_spent_editing_copies"]))));

    auto const& stringInfo = record["level_string_info"];
    if (auto fileSize = number(stringInfo["file_size"]); fileSize > 0) {
        add("Tamano del archivo", fmt::format("{} KB", formatThousands(fileSize / 1024)));
    }
    if (auto rawSize = number(stringInfo["decompressed_file_size"]); rawSize > 0) {
        add("Tamano descomprimido", fmt::format("{} KB", formatThousands(rawSize / 1024)));
    }
    if (auto hash = text(stringInfo["sha256"]); hash.size() > 16) {
        add("SHA256", hash.substr(0, 16) + "...");
    }

    addCount("Daily ID", record["daily_id"]);
    if (auto password = number(record["password"]); password > 0) {
        add("Contrasena", password == 1 ? "Copia libre" : fmt::format("{}", password % 1000000));
    }

    if (entry.invalid) add("Registro invalido", "Si");
    if (truthy(record["cache_is_dupe"])) add("Duplicado", "Si");
    if (record["cache_is_public"].isBool() && !truthy(record["cache_is_public"])) {
        add("Publico", "No");
    }

    return out;
}

} // namespace paimon::info
