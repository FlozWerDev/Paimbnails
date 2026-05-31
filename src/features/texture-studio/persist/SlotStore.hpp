#pragma once
//
// SlotStore.hpp - Singleton that owns the in-memory list of TextureProjects
// and persists the master index. Mirrors the pattern of IconConfigStore
// (singleton + listeners) but with a richer object lifecycle: slots are
// created, renamed, deleted, edited, and exported.
//
// Threading: all SlotStore methods MUST be called from the main thread.
// The exporter runs off-thread but sends back through `Loader::queueInMainThread`
// before mutating slot state.
//
// On disk:
//   - slots.json is the master index (small, fast to read).
//   - Each slot's project.json holds the full TextureProject (read on
//     demand, lazily). Cached in m_projects.
//

#include "TextureProject.hpp"

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::texture_studio {

// Lightweight summary entry kept in slots.json. Lets the UI render the
// slot grid without paying the cost of parsing every project.json.
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

    // ── Lifecycle ──────────────────────────────────────────────────────

    // Read slots.json into memory. Idempotent — safe to call multiple times.
    // Logs warnings on per-slot failures but never throws.
    void loadIndex();

    // Persist the index back to disk. Called automatically after every
    // mutation; expose for explicit "save now" UI affordances.
    geode::Result<> saveIndex();

    // ── Slot list ──────────────────────────────────────────────────────

    // The current slot index, sorted by modifiedAt descending.
    std::vector<SlotIndexEntry> const& list() const { return m_index; }

    // ── Active slot ────────────────────────────────────────────────────

    std::string const& activeSlotId() const { return m_activeSlotId; }
    void setActiveSlot(std::string id);

    // ── CRUD on full TextureProject ────────────────────────────────────

    // Create a new slot, persisting it immediately. Returns the assigned
    // id (derived from name, made unique by appending a hash if collide).
    geode::Result<std::string> createSlot(TextureProject seed);

    // Load a slot's full project. Cached; subsequent loads are O(1).
    geode::Result<TextureProject> loadSlot(std::string_view id);

    // Save changes to a slot. Updates modifiedAt and the index entry.
    geode::Result<> saveSlot(TextureProject const& project);

    // Delete a slot and all its on-disk data. Idempotent.
    geode::Result<> deleteSlot(std::string_view id);

    // ── Helpers ────────────────────────────────────────────────────────

    // Check if a given id already exists in the index.
    bool exists(std::string_view id) const;

private:
    SlotStore() = default;
    ~SlotStore() = default;
    SlotStore(SlotStore const&) = delete;
    SlotStore& operator=(SlotStore const&) = delete;

    // Refresh m_index from m_projects. Sorts by modifiedAt desc.
    void rebuildIndexCache();

    // Pick a unique id derived from `desiredId` by appending a numeric
    // suffix if needed. Idempotent for already-unique ids.
    std::string makeUniqueId(std::string desiredId) const;

    bool m_indexLoaded = false;
    std::vector<SlotIndexEntry> m_index;
    std::map<std::string, TextureProject> m_projects;  // id → project
    std::string m_activeSlotId;
};

}  // namespace paimon::texture_studio
