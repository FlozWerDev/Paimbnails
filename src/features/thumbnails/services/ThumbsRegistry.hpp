#pragma once
#include <Geode/DefaultInclude.hpp>
#include <optional>
#include <vector>
#include <mutex>

enum class ThumbKind { Level, Profile };

struct ThumbRecord {
    ThumbKind kind;
    int id;      // levelid o accountid
    bool verified;
};

class ThumbsRegistry {
public:
    static ThumbsRegistry& get();

    void mark(ThumbKind kind, int id, bool verified);
    bool isVerified(ThumbKind kind, int id) const;
    std::vector<ThumbRecord> list(ThumbKind kind, bool onlyUnverified) const;

private:
    ThumbsRegistry() = default;
    std::filesystem::path path() const;
    void load() const; // lazy (diferido) — caller must hold m_mutex
    void save() const; // caller must hold m_mutex

    mutable std::mutex m_mutex;
    mutable bool m_loaded = false;
    mutable std::vector<ThumbRecord> m_items;
};

