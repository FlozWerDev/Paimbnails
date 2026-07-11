#pragma once

#include <vector>
#include <cstdint>

namespace paimon::editor {

// Persisted favorite object IDs for the build tab (right-click to toggle).
class ObjectFavoritesStore {
public:
    static ObjectFavoritesStore& get();

    void load();
    void save() const;

    bool isFavorite(int objectId) const;
    bool toggle(int objectId); // returns new state (true = favorited)
    void add(int objectId);
    void remove(int objectId);
    void clear();

    std::vector<int> const& ids() const { return m_ids; }

private:
    ObjectFavoritesStore() = default;
    std::vector<int> m_ids;
    bool m_loaded = false;
};

} // namespace paimon::editor
