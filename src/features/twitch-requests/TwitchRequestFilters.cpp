#include "TwitchRequestFilters.hpp"

#include "TwitchRequestManager.hpp"
#include "services/TwitchLevelBriefCache.hpp"

#include <algorithm>
#include <bit>

namespace paimon::twitch {

namespace {

char const* modeName(ModeFilter mode) {
    switch (mode) {
        case ModeFilter::Classic: return "Clasico";
        case ModeFilter::Platformer: return "Plataformas";
        default: return "Todos";
    }
}

// Slot al que cae la cara de un nivel.
int difficultySlotOf(int difficulty) {
    if (difficulty < 0) return 7;                 // auto
    if (difficulty >= 6) return 6;                // cualquier demon
    return std::clamp(difficulty, 0, 5);
}

int onlySlot(uint32_t mask) {
    return std::countr_zero(mask);
}

std::string countLabel(uint32_t mask, char const* suffix) {
    return std::to_string(std::popcount(mask)) + suffix;
}

} // namespace

std::vector<std::string> modeFilterNames() {
    return {
        modeName(ModeFilter::All),
        modeName(ModeFilter::Classic),
        modeName(ModeFilter::Platformer),
    };
}

char const* difficultySlotName(int slot) {
    switch (slot) {
        case 1: return "Facil";
        case 2: return "Normal";
        case 3: return "Dificil";
        case 4: return "Muy dificil";
        case 5: return "Insano";
        case 6: return "Demon";
        case 7: return "Auto";
        default: return "Sin calificar";
    }
}

char const* lengthSlotName(int slot) {
    switch (slot) {
        case 1: return "Short";
        case 2: return "Medium";
        case 3: return "Long";
        case 4: return "XL";
        default: return "Tiny";
    }
}

int difficultySlotSprite(int slot) {
    if (slot == 7) return -1;
    return std::clamp(slot, 0, 6);
}

std::string videoRuleSummary(VideoRequirementRule const& rule) {
    std::string text = modeName(rule.mode);
    text += " - ";
    uint32_t const mask = rule.difficulties & kAllDifficulties;
    if (mask == 0 || mask == kAllDifficulties) {
        text += "Todas las dif.";
    } else if (std::popcount(mask) == 1) {
        text += difficultySlotName(onlySlot(mask));
    } else {
        int first = -1;
        int last = -1;
        bool isContiguous = true;
        for (int i = 0; i < kDifficultySlotCount; ++i) {
            if (mask & (1u << i)) {
                if (first == -1) first = i;
                else if (last != i - 1) isContiguous = false;
                last = i;
            }
        }
        if (isContiguous && first != -1 && last > first) {
            text += difficultySlotName(first);
            text += " - ";
            text += difficultySlotName(last);
        } else {
            text += countLabel(mask, " dif");
        }
    }
    return text;
}

std::string filterSummary(RequestFilters const& filters) {
    std::string summary;
    auto add = [&summary](std::string text) {
        if (!summary.empty()) summary += "/";
        summary += text;
    };

    if (filters.mode != ModeFilter::All) add(modeName(filters.mode));

    if (!filters.allDifficulties()) {
        uint32_t const mask = filters.difficulties & kAllDifficulties;
        add(std::popcount(mask) == 1
            ? difficultySlotName(onlySlot(mask))
            : countLabel(mask, " dif"));
    }

    if (!filters.allLengths()) {
        uint32_t const mask = filters.lengths & kAllLengths;
        add(std::popcount(mask) == 1
            ? lengthSlotName(onlySlot(mask))
            : countLabel(mask, " long"));
    }

    if (!filters.videoRules.empty()) {
        add(std::to_string(filters.videoRules.size()) + " vid req");
    }

    if (filters.verifiedOnly) add("Verificados");
    if (filters.maxPerUser > 0) add(std::to_string(filters.maxPerUser) + "/usr");
    if (filters.cooldownSeconds > 0) {
        add(std::to_string(filters.cooldownSeconds) + "s");
    }
    if (!filters.blockDuplicates) add("Repetidos");

    return summary;
}

bool matchesFilters(
    RequestFilters const& filters,
    int difficulty,
    int length,
    bool platformer,
    bool hasVideo
) {
    if (filters.mode == ModeFilter::Classic && platformer) return false;
    if (filters.mode == ModeFilter::Platformer && !platformer) return false;

    if (!filters.allDifficulties()) {
        uint32_t const bit = 1u << difficultySlotOf(difficulty);
        if (!(filters.difficulties & bit)) return false;
    }

    // Los niveles de plataformas no tienen longitud propia en el juego, asi que
    // ahi solo manda el modo.
    if (!platformer && !filters.allLengths()) {
        uint32_t const bit = 1u << std::clamp(length, 0, kLengthSlotCount - 1);
        if (!(filters.lengths & bit)) return false;
    }

    if (!hasVideo && !filters.videoRules.empty()) {
        int const slot = difficultySlotOf(difficulty);
        uint32_t const bit = 1u << slot;
        for (auto const& rule : filters.videoRules) {
            if (rule.mode == ModeFilter::Classic && platformer) continue;
            if (rule.mode == ModeFilter::Platformer && !platformer) continue;
            if (rule.difficulties & bit) return false;
        }
    }

    return true;
}

std::optional<bool> requestPasses(int levelID, bool hasVideo) {
    auto const& filters = TwitchRequestManager::get().filters();
    if (!filters.hasLevelFilters()) return true;

    auto const* brief = TwitchLevelBriefCache::get().peek(levelID);
    if (!brief) return std::nullopt;
    // Una ID muerta no encaja en nada: se queda visible para poder borrarla.
    if (!brief->found) return true;

    return matchesFilters(filters, brief->filterDifficulty, brief->length, brief->platformer, hasVideo);
}

} // namespace paimon::twitch
