#pragma once

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include "../../../utils/StagedDirectory.hpp"

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
    std::shared_ptr<paimon::file::StagedDirectory> staging;
};

class IconCompiler final {
public:
    // CPU work and private staged files; safe off the main thread. The caller
    // publishes only after validating the project revision on the main thread.
    static geode::Result<CompiledIcon> compile(IconProject const& project);

private:
    IconCompiler() = delete;
};

}  // namespace paimon::icon_maker
