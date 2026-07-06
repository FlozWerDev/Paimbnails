#pragma once

#include <Geode/binding/GJGameLevel.hpp>
#include <string>

namespace paimon {

/**
 * Serializes (almost) every field of a GJGameLevel into a compact JSON string
 * using the Geode bindings. Intended to be attached to a thumbnail upload so the
 * server stores the full level context alongside the image.
 *
 * The heavy m_levelString (the actual level geometry, which can be megabytes) is
 * deliberately NOT included as-is — only its length is reported as
 * "levelStringLength" — to keep the upload payload small. Everything else
 * (identity, stats, difficulty, song, progress, flags, dates...) is included.
 *
 * Must be called on the main thread (reads GJGameLevel bindings).
 * Returns an empty string if `level` is null.
 */
std::string collectLevelMetadata(GJGameLevel* level);

} // namespace paimon
