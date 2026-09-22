#pragma once

// Own JSON file, not a saved value: a partial write of a saved value
// leaves half a slot. Loading is lazy, writes happen on change only.

#include "../OfficialSlots.hpp"

#include <matjson.hpp>
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

    // Adds the slot, assigning it an id. Empty on failure. Appended slots
    // land at orderIndex inside the page order, or at the end when absent.
    std::string add(Slot slot, std::optional<std::size_t> orderIndex = std::nullopt);

    // False when already gone: the panel may stay open while the list changes.
    bool update(Slot const& slot);

    bool remove(std::string const& slotId);

    void move(std::string const& slotId, int delta);

    // Page order across officials and appended slots. Keys are "o:<id>" for
    // officials and "s:<slotId>" for appended slots; replacements ride the
    // official page, so they hold no key.
    static std::string officialKey(int levelId);
    static std::string slotKey(std::string const& slotId);
    // True for official keys, with the id in levelId; false for slot keys,
    // whose id follows the "s:" prefix.
    static bool officialKeyId(std::string const& key, int& levelId);

    std::vector<std::string> const& pageOrder();
    void movePage(std::string const& key, int delta);
    void movePageTo(std::string const& key, std::size_t index);

    std::vector<std::string> visiblePages();
    bool pageVisible(std::string const& key);
    // 1-based position inside visiblePages; 0 when the key is not visible.
    std::size_t visiblePosition(std::string const& key);
    // 1-based visible position to order index, for placing a page the user
    // put at an exact spot. Past the end clamps to the end.
    std::size_t orderIndexForVisiblePos(std::size_t pos);
    void movePageToVisible(std::string const& key, std::size_t pos);

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

    void loadOrder(matjson::Value const& root);

    std::filesystem::path storePath() const;

    std::vector<Slot> m_slots;
    std::vector<int> m_hidden;
    std::vector<std::string> m_order;
    bool m_loaded = false;
};

} // namespace paimon::officialslots
