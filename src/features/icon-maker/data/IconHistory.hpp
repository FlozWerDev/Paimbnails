#pragma once
// Undo/redo for the editor. Whole-project snapshots: a project is a handful of
// small structs (paths, not pixels), so copying one is cheaper than tracking
// per-field deltas and it can never desync from the model.

#include "IconProject.hpp"

#include <string>
#include <vector>

namespace paimon::icon_maker {

class IconHistory {
public:
    static constexpr std::size_t kMaxEntries = 40;

    void reset(IconProject const& current) {
        m_entries.clear();
        m_entries.push_back(current);
        m_cursor = 0;
        m_coalesceKey.clear();
    }

    // Records the state *before* an edit. `coalesceKey` groups a burst of
    // related edits (dragging one slider) into a single undo step; pass an
    // empty key for anything that should always stand alone.
    void push(IconProject const& before, std::string coalesceKey = {}) {
        if (!coalesceKey.empty() && coalesceKey == m_coalesceKey) return;
        m_coalesceKey = std::move(coalesceKey);

        // A new edit after undoing discards the redo tail.
        if (m_cursor + 1 < m_entries.size()) {
            m_entries.resize(m_cursor + 1);
        }
        m_entries.back() = before;
        m_entries.push_back(before);
        m_cursor = m_entries.size() - 1;

        if (m_entries.size() > kMaxEntries) {
            m_entries.erase(m_entries.begin());
            --m_cursor;
        }
    }

    // Call whenever the current project state should become the tip, e.g.
    // after the edit that followed push() finished mutating it.
    void commit(IconProject const& current) {
        if (m_entries.empty()) {
            m_entries.push_back(current);
            m_cursor = 0;
            return;
        }
        m_entries[m_cursor] = current;
    }

    void breakCoalescing() { m_coalesceKey.clear(); }

    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor + 1 < m_entries.size(); }

    // Returns the state to restore, or nullptr when there is nothing to do.
    IconProject const* undo() {
        if (!canUndo()) return nullptr;
        m_coalesceKey.clear();
        --m_cursor;
        return &m_entries[m_cursor];
    }

    IconProject const* redo() {
        if (!canRedo()) return nullptr;
        m_coalesceKey.clear();
        ++m_cursor;
        return &m_entries[m_cursor];
    }

private:
    std::vector<IconProject> m_entries;
    std::size_t m_cursor = 0;
    std::string m_coalesceKey;
};

}  // namespace paimon::icon_maker
