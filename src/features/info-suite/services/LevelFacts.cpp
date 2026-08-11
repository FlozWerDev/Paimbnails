#include "LevelFacts.hpp"
#include "ProgressTracker.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::info {

namespace {

std::string s(gd::string const& v) { return std::string(v); }

void add(std::vector<Fact>& out, FactTab tab, std::string label, std::string value,
         bool accent = false) {
    if (value.empty()) return;
    out.push_back({std::move(label), std::move(value), "", tab, FactAction::None, 0, accent});
}

void addAction(std::vector<Fact>& out, FactTab tab, std::string label, std::string value,
               FactAction action, int arg) {
    if (value.empty()) return;
    out.push_back({std::move(label), std::move(value), "", tab, action, arg, true});
}

// Only emit numeric rows that carry information. Zero downloads on a local
// level is noise; zero likes on an online level is a fact.
void addNumber(std::vector<Fact>& out, FactTab tab, std::string label, int64_t value,
               bool keepZero = false, bool accent = false) {
    if (value == 0 && !keepZero) return;
    add(out, tab, std::move(label), formatThousands(value), accent);
}

void addFlag(std::vector<Fact>& out, FactTab tab, std::string label, bool value) {
    if (!value) return;
    add(out, tab, std::move(label), "Si");
}

char const* coinStateName(int verified) {
    switch (verified) {
        case 0:  return "Sin verificar";
        case 1:  return "Verificadas";
        default: return "Desconocido";
    }
}

} // namespace

char const* tabName(FactTab tab) {
    switch (tab) {
        case FactTab::Level:    return "Nivel";
        case FactTab::Song:     return "Audio";
        case FactTab::Stats:    return "Stats";
        case FactTab::Raw:      return "Raw";
        default:                return "Nivel";
    }
}

std::string formatThousands(int64_t value) {
    bool negative = value < 0;
    auto digits = std::to_string(negative ? -value : value);

    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && count % 3 == 0) out.push_back(',');
        out.push_back(*it);
        count++;
    }
    if (negative) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

std::string formatDuration(int seconds) {
    if (seconds <= 0) return "";
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    if (hours > 0) return fmt::format("{}h {}m {}s", hours, minutes, secs);
    if (minutes > 0) return fmt::format("{}m {}s", minutes, secs);
    return fmt::format("{}s", secs);
}

int difficultyValue(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_autoLevel) return -1;
    if (level->m_demon.value() > 0) {
        switch (level->m_demonDifficulty) {
            case 3:  return 7;
            case 4:  return 8;
            case 5:  return 9;
            case 6:  return 10;
            default: return 6;
        }
    }
    // Online levels ship m_difficulty unset, the rating is what the browser
    // reads. Main levels are the other way around.
    if (level->m_levelType == GJLevelType::Main) return static_cast<int>(level->m_difficulty);
    return level->getAverageDifficulty();
}

// Stars the creator asked for, mapped onto the same face values. 0 when the
// level carries no request at all.
int requestedDifficultyValue(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_autoLevel) return -1;

    int const stars = level->m_starsRequested;
    if (stars <= 0) return 0;
    if (stars == 1) return -1;  // one star is how the game asks for auto
    if (stars == 2) return 1;
    if (stars == 3) return 2;
    if (stars <= 5) return 3;
    if (stars <= 7) return 4;
    if (stars <= 9) return 5;
    return 6;
}

std::string difficultyFaceName(int face) {
    switch (face) {
        case -1: return "Auto";
        case 1:  return "Facil";
        case 2:  return "Normal";
        case 3:  return "Dificil";
        case 4:  return "Muy dificil";
        case 5:  return "Insano";
        case 6:  return "Demon";
        case 7:  return "Demon Facil";
        case 8:  return "Demon Medio";
        case 9:  return "Demon Insano";
        case 10: return "Demon Extremo";
        default: return "Sin calificar";
    }
}

std::string difficultyName(GJGameLevel* level) {
    if (!level) return "";

    // Un demon sin subtipo es un demon duro; la cara generica solo aparece
    // cuando la dificultad se dedujo de las estrellas.
    if (level->m_demon.value() > 0 && difficultyValue(level) == 6) return "Demon Duro";
    return difficultyFaceName(difficultyValue(level));
}

std::string lengthName(int length) {
    switch (length) {
        case 0:  return "Tiny";
        case 1:  return "Short";
        case 2:  return "Medium";
        case 3:  return "Long";
        case 4:  return "XL";
        case 5:  return "Plataformas";
        default: return "";
    }
}

std::string featureName(int featured, int isEpic) {
    switch (isEpic) {
        case 1:  return "Epic";
        case 2:  return "Legendary";
        case 3:  return "Mythic";
        default: break;
    }
    if (featured > 0) return fmt::format("Featured ({})", featured);
    return "";
}

std::vector<Fact> collectFacts(GJGameLevel* level) {
    std::vector<Fact> out;
    if (!level) return out;

    out.reserve(72);

    int levelID = level->m_levelID.value();
    addNumber(out, FactTab::Level, "ID del nivel", levelID, true, true);
    add(out, FactTab::Level, "Nombre", s(level->m_levelName));

    int original = level->m_originalLevel.value();
    if (original > 0) {
        addAction(out, FactTab::Level, "Copia de", fmt::format("#{}", original),
                  FactAction::OpenOriginal, original);
    }

    auto creator = s(level->m_creatorName);
    int accountID = level->m_accountID.value();
    if (!creator.empty()) {
        if (accountID > 0) {
            addAction(out, FactTab::Level, "Creador", creator, FactAction::OpenCreator, accountID);
        } else {
            add(out, FactTab::Level, "Creador", creator);
        }
    }
    addNumber(out, FactTab::Level, "User ID", level->m_userID.value());
    addNumber(out, FactTab::Level, "Account ID", accountID);

    auto desc = s(level->getUnpackedLevelDescription());
    if (desc.empty()) desc = s(level->m_levelDesc);
    add(out, FactTab::Level, "Descripcion", desc);

    addNumber(out, FactTab::Level, "Objetos", level->m_objectCount.value(), true);
    add(out, FactTab::Level, "Duracion", lengthName(level->m_levelLength));
    addNumber(out, FactTab::Level, "Version del nivel", level->m_levelVersion, true);

    if (level->m_gameVersion > 0) {
        // RobTop stores 2.0 as 20, 2.1 as 21… anything below 10 is a 1.x build.
        int gv = level->m_gameVersion;
        std::string pretty = gv < 10
            ? fmt::format("1.{}", gv)
            : fmt::format("{}.{}", gv / 10, gv % 10);
        add(out, FactTab::Level, "Version del juego", fmt::format("{} ({})", pretty, gv));
    }

    add(out, FactTab::Level, "Tiempo de edicion", formatDuration(level->m_workingTime));
    add(out, FactTab::Level, "Tiempo de edicion (copia)", formatDuration(level->m_workingTime2));

    add(out, FactTab::Level, "Subido", s(level->m_uploadDate));
    add(out, FactTab::Level, "Actualizado", s(level->m_updateDate));

    addFlag(out, FactTab::Level, "Dos jugadores", level->m_twoPlayerMode);
    addFlag(out, FactTab::Level, "Low detail", level->m_lowDetailMode);
    addFlag(out, FactTab::Level, "No listado", level->m_unlisted);
    addFlag(out, FactTab::Level, "Solo amigos", level->m_friendsOnly);
    addFlag(out, FactTab::Level, "Favorito", level->m_levelFavorited);
    addFlag(out, FactTab::Level, "Verificado", level->m_isVerifiedRaw);
    addFlag(out, FactTab::Level, "Subido por ti", level->m_isUploaded);
    addFlag(out, FactTab::Level, "Editable", level->m_isEditable);
    addFlag(out, FactTab::Level, "Objetos altos", level->m_highObjectsEnabled);
    addFlag(out, FactTab::Level, "Objetos ilimitados", level->m_unlimitedObjectsEnabled);

    int password = level->m_password.value();
    if (password > 0) {
        // 1 means "free copy"; anything else is the 6 digit code with a leading 1.
        add(out, FactTab::Level, "Contrasena",
            password == 1 ? "Copia libre" : fmt::format("{}", password % 1000000), true);
    }

    addNumber(out, FactTab::Level, "Carpeta", level->m_levelFolder);
    addNumber(out, FactTab::Level, "Daily ID", level->m_dailyID.value());
    addFlag(out, FactTab::Level, "En gauntlet", level->m_gauntletLevel || level->m_gauntletLevel2);
    addNumber(out, FactTab::Level, "Posicion en lista", level->m_listPosition);

    if (level->m_songID > 0) {
        addAction(out, FactTab::Song, "Song ID", fmt::format("{}", level->m_songID),
                  FactAction::OpenSong, level->m_songID);
    } else {
        addNumber(out, FactTab::Song, "Track oficial", level->m_audioTrack, true);
    }
    add(out, FactTab::Song, "Cancion", s(level->getSongName()));
    add(out, FactTab::Song, "Archivo", s(level->getAudioFileName()));
    addNumber(out, FactTab::Song, "Tamano (KB)", level->m_songSize);
    add(out, FactTab::Song, "Canciones extra", s(level->m_songIDs));
    add(out, FactTab::Song, "SFX", s(level->m_sfxIDs));

    add(out, FactTab::Stats, "Dificultad", difficultyName(level), true);
    addNumber(out, FactTab::Stats, "Estrellas", level->m_stars.value(), false, true);
    addNumber(out, FactTab::Stats, "Estrellas pedidas", level->m_starsRequested);
    add(out, FactTab::Stats, "Estado", featureName(level->m_featured, level->m_isEpic), true);

    addNumber(out, FactTab::Stats, "Descargas", level->m_downloads, true);
    addNumber(out, FactTab::Stats, "Likes", level->m_likes, true);
    addNumber(out, FactTab::Stats, "Dislikes", level->m_dislikes);

    int votes = level->m_likes + level->m_dislikes;
    if (votes > 0) {
        double ratio = 100.0 * static_cast<double>(level->m_likes) / static_cast<double>(votes);
        add(out, FactTab::Stats, "Aprobacion", fmt::format("{:.1f}%", ratio));
    }

    if (level->m_coins > 0) {
        add(out, FactTab::Stats, "Monedas",
            fmt::format("{} ({})", level->m_coins, coinStateName(level->m_coinsVerified.value())));
    }
    addNumber(out, FactTab::Stats, "Monedas requeridas", level->m_requiredCoins);

    addNumber(out, FactTab::Stats, "Votos de dificultad", level->m_ratings);
    addNumber(out, FactTab::Stats, "Suma de votos", level->m_ratingsSum);
    addNumber(out, FactTab::Stats, "Votos de estrellas", level->m_starRatings);
    addNumber(out, FactTab::Stats, "Suma de estrellas", level->m_starRatingsSum);
    addNumber(out, FactTab::Stats, "Estrellas minimas", level->m_minStarRatings);
    addNumber(out, FactTab::Stats, "Estrellas maximas", level->m_maxStarRatings);
    addNumber(out, FactTab::Stats, "Votos de demon", level->m_demonVotes);

    if (level->m_ratings > 0) {
        double avgVote = static_cast<double>(level->m_ratingsSum) / level->m_ratings;
        add(out, FactTab::Stats, "Voto medio de dificultad", fmt::format("{:.2f}", avgVote));
    }

    addNumber(out, FactTab::Stats, "Rate sugerido", level->m_rateStars);
    addFlag(out, FactTab::Stats, "Feature sugerido", level->m_rateFeature);
    add(out, FactTab::Stats, "Sugerido por", s(level->m_rateUser));

    // Personal records the game keeps per level. Attempts, jumps and the two
    // best percentages are not here: LevelStatsPopup owns those, with charts.
    addNumber(out, FactTab::Stats, "Clics", level->m_clicks.value());
    add(out, FactTab::Stats, "Tiempo jugado", formatDuration(level->m_attemptTime.value()));
    addNumber(out, FactTab::Stats, "Orbes obtenidos", level->m_orbCompletion.value());

    if (level->isPlatformer()) {
        if (level->m_bestTime > 0) {
            add(out, FactTab::Stats, "Mejor tiempo",
                fmt::format("{:.3f}s", static_cast<double>(level->m_bestTime) / 1000.0), true);
        }
        addNumber(out, FactTab::Stats, "Mejores puntos", level->m_bestPoints);
    }
    addFlag(out, FactTab::Stats, "Completado legitimo", level->m_isCompletionLegitimate);

    if (auto const* tracked = ProgressTracker::get().find(levelID)) {
        addNumber(out, FactTab::Stats, "Veces completado", tracked->completions);
        add(out, FactTab::Stats, "Tiempo registrado",
            formatDuration(static_cast<int>(tracked->playSeconds)));
    }

    addNumber(out, FactTab::Raw, "Level string (bytes)",
              static_cast<int64_t>(level->m_levelString.size()));
    addNumber(out, FactTab::Raw, "Level rev", level->m_levelRev);
    addNumber(out, FactTab::Raw, "Level index", level->m_levelIndex);
    addNumber(out, FactTab::Raw, "Timestamp", level->m_timestamp);
    addNumber(out, FactTab::Raw, "Tipo de nivel", static_cast<int>(level->m_levelType), true);
    addNumber(out, FactTab::Raw, "M_ID", level->m_M_ID);
    addNumber(out, FactTab::Raw, "CHK", level->m_chk);
    add(out, FactTab::Raw, "Capacity string", s(level->m_capacityString));
    add(out, FactTab::Raw, "Record string", s(level->m_recordString));
    add(out, FactTab::Raw, "Personal bests", s(level->m_personalBests));

    return out;
}

} // namespace paimon::info
