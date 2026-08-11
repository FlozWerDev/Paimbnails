#pragma once

// GD only tells you a comment is "3 months" old. Comment ids are handed out in
// order, so every comment whose rough age we already know becomes an anchor;
// interpolating between two anchors turns any id in between into a real date.
//
// The anchors come from the game itself, one per comment the player scrolls
// past, so this works with no external service. GDHistory, when the user opts
// in, only sharpens the anchors it already has.

#include <cstdint>
#include <string>

namespace paimon::info {

// Parses GD's relative age ("3 months", "1 day") into seconds, or 0 if it does
// not look like one.
int64_t parseRelativeAge(std::string const& text);

// Feeds a comment into the interpolation table.
void noteComment(int64_t commentID, std::string const& relativeAge);

// Best guess absolute date for a comment, formatted, or empty when there is not
// enough data around that id yet.
std::string estimateCommentDate(int64_t commentID);

} // namespace paimon::info
