#pragma once

#include "../GifImportTypes.hpp"

#include <Geode/Geode.hpp>

#include <memory>

class EditorUI;

namespace paimon::gifimport {

struct EmitReport {
    std::size_t objects = 0;
    int colors = 0;
    int groups = 0;
};

geode::Result<EmitReport> emitToEditor(
    EditorUI* ui,
    ImportPlan const& plan,
    Options const& options,
    cocos2d::CCPoint origin
);

geode::Result<> startBackgroundImport(
    EditorUI* ui,
    std::shared_ptr<SourceAnimation> source,
    Options const& options,
    cocos2d::CCPoint center
);

} // namespace paimon::gifimport
