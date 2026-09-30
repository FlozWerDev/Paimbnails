#pragma once

// shared json holds settings only; sheets re-detected locally on import.

#include "TextureProject.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>

namespace paimon::texture_studio {

class ProjectShare final {
public:
    // writes project json; sheets re-resolved on import.
    static geode::Result<> exportTo(std::filesystem::path const& dst,
                                    TextureProject const& project);

    // imports json as new slot; slotstore resolves id collisions.
    static geode::Result<std::string> importFrom(std::filesystem::path const& src);

private:
    ProjectShare() = delete;
};

}  // namespace paimon::texture_studio
