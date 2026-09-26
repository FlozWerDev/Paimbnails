#pragma once
// Compile + apply an icon, shared by the editor ("Use") and the gallery.
// Threading: call on the main thread; heavy work moves to a ThreadTracker
// thread and the callback returns on main.

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::icon_maker {

class IconBuildService final {
public:
    // on success the message is user-ready.
    using DoneCallback = std::function<void(geode::Result<std::string>)>;

    // Compiles the sheets, hands the icon to More Icons when it is installed
    // and to the mod's own applier otherwise, and records the build on disk.
    static void buildAndApply(IconProject project, DoneCallback onDone);

    // Only compiles; leaves the icon un-applied.
    static void build(IconProject project, DoneCallback onDone);

private:
    IconBuildService() = delete;
};

}  // namespace paimon::icon_maker
