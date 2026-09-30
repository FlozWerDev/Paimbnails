#pragma once
// call on the main thread; compilation runs on a tracked worker and completion returns on main.

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::icon_maker {

class IconBuildService final {
public:
    // on success the message is user-ready.
    using DoneCallback = std::function<void(geode::Result<std::string>)>;

    // compiles the sheets, hands the icon to more icons when it is installed
    // and to the mod's own applier otherwise, and records the build on disk.
    static void buildAndApply(IconProject project, DoneCallback onDone);

    // only compiles; leaves the icon un-applied.
    static void build(IconProject project, DoneCallback onDone);

private:
    IconBuildService() = delete;
};

}  // namespace paimon::icon_maker
