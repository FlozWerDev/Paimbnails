#pragma once

// Filtros de la cola: modo, caras de dificultad y longitudes que aceptas. Los
// pedidos que no encajan siguen guardados pero fuera de la lista.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace paimon::twitch {

enum class ModeFilter : int { All, Classic, Platformer };
constexpr int kModeFilterCount = 3;

// Un bit por cara / longitud, en el orden en que se dibujan.
// Dificultad: NA, Facil, Normal, Dificil, Muy dificil, Insano, Demon, Auto.
// Longitud: Tiny, Short, Medium, Long, XL (los mismos valores que el juego).
constexpr int kDifficultySlotCount = 8;
constexpr int kLengthSlotCount = 5;
constexpr uint32_t kAllDifficulties = (1u << kDifficultySlotCount) - 1;
constexpr uint32_t kAllLengths = (1u << kLengthSlotCount) - 1;

struct VideoRequirementRule {
    ModeFilter mode = ModeFilter::All;
    uint32_t difficulties = (1u << 6);
};

struct RequestFilters {
    ModeFilter mode = ModeFilter::All;
    uint32_t difficulties = kAllDifficulties;
    uint32_t lengths = kAllLengths;
    bool verifiedOnly = false;
    bool blockDuplicates = true;
    int maxPerUser = 0;
    int cooldownSeconds = 0;
    std::vector<VideoRequirementRule> videoRules;

    // Nada marcado vale lo mismo que todo marcado: la cola no se queda muerta.
    bool allDifficulties() const {
        uint32_t const mask = difficulties & kAllDifficulties;
        return mask == 0 || mask == kAllDifficulties;
    }
    bool allLengths() const {
        uint32_t const mask = lengths & kAllLengths;
        return mask == 0 || mask == kAllLengths;
    }
    bool hasLevelFilters() const {
        return mode != ModeFilter::All || !allDifficulties() || !allLengths() || !videoRules.empty();
    }
};

std::vector<std::string> modeFilterNames();
char const* difficultySlotName(int slot);
char const* lengthSlotName(int slot);
// Valor que espera GJDifficultySprite para la cara de ese slot.
int difficultySlotSprite(int slot);

// Resumen de una regla de video obligatorio.
std::string videoRuleSummary(VideoRequirementRule const& rule);

// Resumen corto para la cabecera de la cola; vacio si no hay filtros.
std::string filterSummary(RequestFilters const& filters);

// difficulty es el valor que usa GJDifficultySprite: -1 auto, 0 sin calificar,
// 1-5 facil..insano, 6 o mas demon.
bool matchesFilters(
    RequestFilters const& filters,
    int difficulty,
    int length,
    bool platformer,
    bool hasVideo = true
);

// nullopt mientras el nivel no esta resuelto: todavia no hay con que juzgarlo.
std::optional<bool> requestPasses(int levelID, bool hasVideo = true);

} // namespace paimon::twitch
