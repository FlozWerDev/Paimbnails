#pragma once

// one json per level under saveDir/animate; the frames themselves live in the
// level as plain groups, so losing this file only loses the timeline layout.

#include "../AnimateTypes.hpp"

#include <Geode/Geode.hpp>

#include <string>

class GJGameLevel;

namespace paimon::animate {

std::string levelKey(GJGameLevel* level);

matjson::Value encodeDocument(Document const& doc);
Document decodeDocument(matjson::Value const& json);

Document loadDocument(std::string const& key);
bool saveDocument(std::string const& key, Document const& doc);

ViewPrefs loadPrefs();
void savePrefs(ViewPrefs const& prefs);

} // namespace paimon::animate
