#pragma once
// Compila un proyecto a spritesheets listos para GD/MoreIcons:
// output/<id>-uhd.png/.plist, <id>-hd.*, <id>.* (sd), con nombres de frame
// vanilla (player_..., robot_..._0X_...).

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>

namespace paimon::icon_maker {

struct CompiledQuality {
    std::filesystem::path png;
    std::filesystem::path plist;
};

struct CompiledIcon {
    std::string exportName;  // == project id
    CompiledQuality uhd;
    CompiledQuality hd;
    CompiledQuality sd;
};

class IconCompiler final {
public:
    // Pure CPU work — safe to call off the main thread. Writes into
    // IconPaths::outputDir(project.id).
    static geode::Result<CompiledIcon> compile(IconProject const& project);

private:
    IconCompiler() = delete;
};

}  // namespace paimon::icon_maker
