#pragma once

// Pulling a level by id and handing the analyzer its objects.

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>

#include <memory>
#include <string>

#include "LevelAnalysis.hpp"

class GJGameLevel;

namespace paimon::autobuild {

struct AnalysisResult {
    std::shared_ptr<LevelData> data;
    LevelReport report;
};

// nullptr-ish result (empty data) means the level could not be read. Always
// called on the main thread.
using AnalysisCallback = geode::CopyableFunction<void(geode::Result<AnalysisResult>)>;

void analyzeLevelId(int levelId, AnalysisCallback callback);
void analyzeOpenLevel(AnalysisCallback callback);

// config/autobuild/objects.txt, read once per session.
void loadTaxonomyFile();

} // namespace paimon::autobuild
