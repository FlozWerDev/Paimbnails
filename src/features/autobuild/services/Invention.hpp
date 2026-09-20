#pragma once

// Fase 0: el gameplay nunca se fragmenta, solo la decoracion.

#include "../AutobuildTypes.hpp"
#include "LevelParse.hpp"
#include "ObjectTaxonomy.hpp"

namespace paimon::autobuild {

constexpr int kInventionFaithful = 0;  // Fiel: solo recombina lo observado
constexpr int kInventionBlend = 1;     // Mezcla: + motivos/sub-fragmentos frecuentes
constexpr int kInventionBold = 2;      // Atrevido: + motivos raros y jitter acotado

char const* inventionName(int invention);

// Gameplay (solido/peligro/portal/pad/orbe/trigger) no se fragmenta.
bool isGameplayLocked(CapturedObject const& object);

// Fuera de rango se trata como Fiel.
bool inventionAllows(int level, int required);

// Rama de rng propia por tecnica, sin robar numeros.
unsigned deriveSeed(unsigned base, unsigned tag);

} // namespace paimon::autobuild
