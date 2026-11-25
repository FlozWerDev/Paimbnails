#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace paimon::profiles {

// what the profile name popup shows for one mod identity (level, versus, badge).
// callbacks capture their data by value so they stay valid after the page closes.
struct IdentityEntry {
    std::string id;
    int order = 0;
    std::function<cocos2d::CCNode*(float height)> buildChip;
    std::function<void()> onOpenRanking;
    std::string emoteName;
};

// lives on the profile main layer so every ProfilePage hook can register into it.
class IdentityStore : public cocos2d::CCObject {
public:
    static IdentityStore* get(cocos2d::CCNode* host, bool create = false) {
        using namespace geode::prelude;
        if (!host) return nullptr;
        auto key = "paimon-identity-store"_spr;
        if (auto* existing = typeinfo_cast<IdentityStore*>(host->getUserObject(key))) {
            return existing;
        }
        if (!create) return nullptr;
        auto* store = new IdentityStore();
        store->autorelease();
        host->setUserObject(key, store);
        return store;
    }

    void set(IdentityEntry entry) {
        remove(entry.id);
        m_entries.push_back(std::move(entry));
        std::stable_sort(m_entries.begin(), m_entries.end(),
            [](IdentityEntry const& a, IdentityEntry const& b) { return a.order < b.order; });
    }

    void remove(std::string const& id) {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
            [&](IdentityEntry const& e) { return e.id == id; }), m_entries.end());
    }

    std::vector<IdentityEntry> const& entries() const { return m_entries; }

private:
    std::vector<IdentityEntry> m_entries;
};

} // namespace paimon::profiles
