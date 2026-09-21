#pragma once

// Own JSON file, not a saved value: a partial write of a saved value
// leaves half a slot. Loading is lazy, writes happen on change only.

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

    // Adds the slot, assigning it an id. Empty on failure.
    std::string add(Slot slot);

    // False when already gone: the panel may stay open while the list changes.
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

    // The original is left alone so the user can move or delete it freely.
    std::optional<std::string> importGmd(std::filesystem::path const& source);

    // Drops the .gmd of a slot that no longer needs it. Safe when absent.
    void discardGmd(std::string const& fileName);

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
