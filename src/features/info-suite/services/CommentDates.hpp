#pragma once

// comment ids are sequential: known ages become anchors, ids in between interpolate to dates.
// anchors come from scrolled-past comments, so no external service is needed.

#include <cstdint>
#include <string>

namespace paimon::info {

// parses gd's relative age ("3 months", "1 day") into seconds, or 0 if it does
// not look like one.
int64_t parseRelativeAge(std::string const& text);

// feeds a comment into the interpolation table.
void noteComment(int64_t commentID, std::string const& relativeAge);

// best guess absolute date for a comment, formatted, or empty when there is not
// enough data around that id yet.
std::string estimateCommentDate(int64_t commentID);

} // namespace paimon::info
