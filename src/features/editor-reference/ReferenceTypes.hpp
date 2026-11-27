#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::editorref {

inline constexpr int kMaxReferencesPerLevel = 32;
inline constexpr int kMaxHistoryEntries = 48;
inline constexpr char const* kLevelIdentityKey = "paimon-reference-level-id";
inline constexpr char const* kAnchorTagKey = "paimon-reference-anchor-id";
// Decoration white square block (30x30 world units, non-solid).
inline constexpr int kAnchorObjectID = 211;

enum class ZPlacement : int {
    Behind = 0,
    Front = 1,
};

struct ReferenceEntry {
    int id = 0;
    std::string name;
    // Filename inside <saveDir>/reference-images/assets/ (or absolute fallback path).
    std::string assetFile;
    float x = 0.f;
    float y = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float rotationX = 0.f;
    float rotationY = 0.f;
    float skewX = 0.f;
    float skewY = 0.f;
    bool flipX = false;
    bool flipY = false;
    int opacity = 145; // 0..255 (~57% default for tracing)
    ZPlacement zPlacement = ZPlacement::Behind;
    bool locked = false;
    bool visible = true;
    bool showInPlaytest = false;
};

struct ReferenceDocument {
    int nextId = 1;
    std::vector<ReferenceEntry> entries;
};

struct HistoryEntry {
    std::string id;
    std::string name;
    // Filename inside <saveDir>/reference-images/history/.
    std::string assetFile;
    std::uint64_t contentHash = 0;
    std::uint64_t fileSize = 0;
    int width = 0;
    int height = 0;
    std::int64_t usedAtMs = 0;
};

} // namespace paimon::editorref
