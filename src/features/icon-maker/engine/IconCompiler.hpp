#pragma once

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
    // pure cpu work — safe to call off the main thread. writes into
    // iconpaths::outputdir(project.id).
    static geode::Result<CompiledIcon> compile(IconProject const& project);

private:
    IconCompiler() = delete;
};

}  // namespace paimon::icon_maker
