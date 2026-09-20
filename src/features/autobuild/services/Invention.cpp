#include "Invention.hpp"

#include <algorithm>

namespace paimon::autobuild {

char const* inventionName(int invention) {
    switch (invention) {
        case kInventionBlend: return "Mezcla";
        case kInventionBold:  return "Atrevido";
        default:              return "Fiel";
    }
}

bool isGameplayLocked(CapturedObject const& object) {
    switch (kindOf(object.objectId)) {
        case ObjectKind::Solid:
        case ObjectKind::Hazard:
        case ObjectKind::Portal:
        case ObjectKind::Pad:
        case ObjectKind::Orb:
        case ObjectKind::Trigger:
            return true;
        default:
            break;
    }
    // Sin catalogar: solo triggers dejan huella en save.
    LevelObject probe;
    probe.id = object.objectId;
    probe.save = object.save;
    return looksLikeTrigger(probe);
}

bool inventionAllows(int level, int required) {
    int const clamped = std::clamp(level, kInventionFaithful, kInventionBold);
    return clamped >= required;
}

unsigned deriveSeed(unsigned base, unsigned tag) {
    return base ^ 0x9e3779b9u ^ (tag * 0x85ebca6bu);
}

} // namespace paimon::autobuild
