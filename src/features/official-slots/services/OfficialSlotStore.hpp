#pragma once

// Persistence and mutation for the official slots.
//
// Kept in its own JSON file inside the mod save dir rather than in a saved
// value: the list is a whole document, and a partial write of a saved value is
// how you end up with half a slot. Loading is lazy and the file is only
// rewritten on an actual change.

#include "../OfficialSlots.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace paimon::officialslots {

class SlotStore {
public:
    static SlotStore& get();

    // Every slot in display order.
    std::vector<Slot> const& slots();

    std::optional<Slot> find(std::string const& slotId);

    // Adds the slot, assigning it an id, and returns that id. Empty on failure.
    std::string add(Slot slot);

    // Replaces the slot with the same id. False when it is already gone, which
    // happens if the panel is left open while the list changes elsewhere.
    bool update(Slot const& slot);

    bool remove(std::string const& slotId);

    void move(std::string const& slotId, int delta);

    // Officials the user chose to hide. Ids are always in [1, 22].
    std::vector<int> const& hiddenOfficials();
    bool isOfficialHidden(int levelId);
    void setOfficialHidden(int levelId, bool hidden);

    // The slot drawn in place of an official page, if any.
    std::optional<Slot> slotForOfficial(int levelId);

    // Folder holding imported .gmd files. Created on first use.
    std::filesystem::path gmdDir() const;

    // Copies the picked .gmd into our folder and returns the stored filename.
    // The original is left alone so the user can move or delete it freely.
    std::optional<std::string> importGmd(std::filesystem::path const& source);

    // Drops the .gmd of a slot that no longer needs it. Safe when absent.
    void discardGmd(std::string const& fileName);

    void reload();

private:
    SlotStore() = default;

    void ensureLoaded();
    void save();

    std::filesystem::path storePath() const;

    std::vector<Slot> m_slots;
    std::vector<int> m_hidden;
    bool m_loaded = false;
};

} // namespace paimon::officialslots
