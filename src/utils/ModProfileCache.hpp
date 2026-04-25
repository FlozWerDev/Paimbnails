#pragma once
#include <string>
#include <unordered_map>

struct CachedModProfile {
    std::string username;
    std::string role;
    int modBadge = 0; // 2 = admin, 1 = mod
};

class ModProfileCache {
public:
    static ModProfileCache& get() {
        static ModProfileCache instance;
        return instance;
    }

    void store(std::string const& username, std::string const& role) {
        m_profiles[username] = {username, role, (role == "admin") ? 2 : 1};
    }

    CachedModProfile* find(std::string const& username) {
        auto it = m_profiles.find(username);
        return it != m_profiles.end() ? &it->second : nullptr;
    }

    bool isModerator(std::string const& username) const {
        return m_profiles.contains(username);
    }

    auto const& all() const { return m_profiles; }
    void clear() { m_profiles.clear(); }

private:
    std::unordered_map<std::string, CachedModProfile> m_profiles;
};
