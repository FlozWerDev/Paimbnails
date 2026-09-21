#pragma once

// Edits live here so piece removal renumbers links and grids in one place.

#include <vector>

#include "../AutobuildTypes.hpp"
#include "ObjectTaxonomy.hpp"

namespace paimon::autobuild::edit {

struct KindCount {
    ObjectKind kind = ObjectKind::Unknown;
    int objects = 0;
};

std::vector<KindCount> countKinds(Template const& tpl);

int removeKind(Template& tpl, ObjectKind kind);
int removeTriggers(Template& tpl);
int keepOnlyKinds(Template& tpl, std::vector<ObjectKind> const& kinds);

bool removePiece(Template& tpl, int index);
bool duplicatePiece(Template& tpl, int index);
void setWeight(Template& tpl, int index, int weight);

int remapChannel(Template& tpl, int from, int to);
int shiftChannels(Template& tpl, int delta);

// Recompute neighbour rules from the stored sample grids. Needed after any edit
// that changed which pieces exist.
void rebuildLinks(Template& tpl);

// Drop pieces with no objects left and renumber everything that points at them.
int dropEmptyPieces(Template& tpl);

} // namespace paimon::autobuild::edit
