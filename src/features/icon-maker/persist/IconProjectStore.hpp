#pragma once
// main thread only: compiler workers queue results before changing store state.

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

// lightweight summary kept in icons.json so the gallery renders without
// parsing every project.json.
struct IconIndexEntry {
    std::string  id;
    std::string  name;
    IconType     type = IconType::Cube;
    std::int64_t modifiedAt = 0;
    std::int64_t createdAt  = 0;
    bool         hasBuiltOnce = false;
    bool         favorite = false;
};

class IconProjectStore final {
public:
    static IconProjectStore& get();

    // idempotent — safe to call multiple times.
    void loadIndex();

    geode::Result<> saveIndex();

    std::vector<IconIndexEntry> const& list() const { return m_index; }

    // returns the assigned id (derived from name, made unique on collision).
    geode::Result<std::string> createProject(IconProject seed);

    geode::Result<IconProject> loadProject(std::string_view id);

    geode::Result<> saveProject(IconProject const& project);

    geode::Result<std::string> duplicateProject(std::string_view id);

    geode::Result<> deleteProject(std::string_view id);

    // gallery marker; leaves project.json alone.
    geode::Result<> setFavorite(std::string_view id, bool favorite);

    bool exists(std::string_view id) const;

private:
    IconProjectStore() = default;
    ~IconProjectStore() = default;
    IconProjectStore(IconProjectStore const&) = delete;
    IconProjectStore& operator=(IconProjectStore const&) = delete;

    void sortIndex();
    void upsertIndexEntry(IconProject const& project);
    std::string makeUniqueId(std::string desiredId) const;

    bool m_indexLoaded = false;
    std::vector<IconIndexEntry> m_index;
    std::map<std::string, IconProject> m_projects;
};

}  // namespace paimon::icon_maker
