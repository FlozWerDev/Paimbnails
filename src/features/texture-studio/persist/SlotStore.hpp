#pragma once
//
// Threading: all SlotStore methods MUST be called from the main thread.
// The exporter runs off-thread but sends back through `Loader::queueInMainThread`
// before mutating slot state.
//

#include "TextureProject.hpp"

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::texture_studio {

// Lightweight summary entry kept in slots.json so the UI can render the
// slot grid without parsing every project.json.
struct SlotIndexEntry {
    std::string  id;
    std::string  name;
    std::int64_t modifiedAt = 0;
    std::int64_t createdAt  = 0;
    bool         hasBuiltOnce = false;
};

class SlotStore final {
public:
    static SlotStore& get();

    // Idempotent — safe to call multiple times.
    void loadIndex();

    geode::Result<> saveIndex();

    std::vector<SlotIndexEntry> const& list() const { return m_index; }

    std::string const& activeSlotId() const { return m_activeSlotId; }
    void setActiveSlot(std::string id);

    // Returns the assigned id (derived from name, made unique on collision).
    geode::Result<std::string> createSlot(TextureProject seed);

    geode::Result<TextureProject> loadSlot(std::string_view id);

    geode::Result<> saveSlot(TextureProject const& project);

    geode::Result<> deleteSlot(std::string_view id);

    bool exists(std::string_view id) const;

private:
    SlotStore() = default;
    ~SlotStore() = default;
    SlotStore(SlotStore const&) = delete;
    SlotStore& operator=(SlotStore const&) = delete;

    void rebuildIndexCache();

    std::string makeUniqueId(std::string desiredId) const;

    bool m_indexLoaded = false;
    std::vector<SlotIndexEntry> m_index;
    std::map<std::string, TextureProject> m_projects;
    std::string m_activeSlotId;
};

}  // namespace paimon::texture_studio
