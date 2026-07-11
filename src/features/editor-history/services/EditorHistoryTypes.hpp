#pragma once

#include <Geode/Geode.hpp>
#include <Geode/Enums.hpp>
#include <cocos2d.h>
#include <cstdint>
#include <string>

namespace paimon::editorhistory {

enum class HistoryTab { Undo = 0, Redo = 1, Collab = 2 };

enum class HistoryFilter {
    All = 0,
    CreateDelete = 1,
    Transform = 2,
    Edit = 3,     // color + property + shape (scale as form)
    Select = 4,
    Mine = 5,
    Peers = 6,
};

enum class ActionKind {
    Create, Paste, Delete,
    Move, Scale, Rotate, Flip, Transform,
    Color, Property,
    Select, Unknown,
};

struct UndoMeta {
    uint64_t seq = 0;
    int64_t timeMs = 0;
    int clientId = 0;
    std::string author;
    bool collabSession = false;
    ActionKind kind = ActionKind::Unknown;
    std::string label;
};

// Snapshot of displayed / channel color.
// channelId > 0  → level ColorAction id
// channelId == -1 → object main/base sprite color
// channelId == -2 → object detail/color sprite
struct ChannelSnap {
    int channelId = 0;
    cocos2d::ccColor3B color{255, 255, 255};
    float opacity = 1.f;
    bool blending = false;
    int playerColor = 0;
    int copyID = 0;
    cocos2d::ccHSVValue copyHSV{};
    bool copyOpacity = false;
};

// Synthetic local edit (color/shape/props) — shown in Undo tab even when GD
// never pushed a native UndoObject (common for channel colors / live HSV).
struct LocalEditEntry {
    uint64_t id = 0;
    int64_t timeMs = 0;
    ActionKind kind = ActionKind::Property;
    std::string label;
    int objectCount = 0;
    int objectId = 0;
    int uniqueId = 0;
    cocos2d::CCPoint worldPos{0.f, 0.f};
    std::string beforeSave;
    std::string afterSave;
    std::vector<ChannelSnap> beforeChannels;
    std::vector<ChannelSnap> afterChannels;
    // Quick RGB for UI swatches (derived from channels / sprites).
    cocos2d::ccColor3B previewBefore{255, 255, 255};
    cocos2d::ccColor3B previewAfter{255, 255, 255};
    bool hasColorPreview = false;
    bool canRestore = false;
    // Toggle: false = next Restore applies before*; true = applies after*.
    bool restored = false;
};

// Pick the best RGB to show in a swatch from a channel list.
inline cocos2d::ccColor3B previewFromChannels(std::vector<ChannelSnap> const& ch) {
    // Prefer sprite snap, then any channel.
    for (auto const& c : ch) {
        if (c.channelId == -1) return c.color;
    }
    for (auto const& c : ch) {
        if (c.channelId == -2) return c.color;
    }
    for (auto const& c : ch) {
        if (c.channelId > 0) return c.color;
    }
    if (!ch.empty()) return ch.front().color;
    return {255, 255, 255};
}

struct CollabFeedEntry {
    uint64_t id = 0;
    int64_t timeMs = 0;
    int clientId = 0;
    std::string author;
    std::string kind;
    int objectCount = 0;
    int objectId = 0;
    cocos2d::CCPoint worldPos{0.f, 0.f};
    std::string savePreview;
    bool isLocal = false;
    bool isRemote = false;
};

inline char const* actionKindName(ActionKind k) {
    switch (k) {
        case ActionKind::Create: return "create";
        case ActionKind::Paste: return "paste";
        case ActionKind::Delete: return "delete";
        case ActionKind::Move: return "move";
        case ActionKind::Scale: return "scale";
        case ActionKind::Rotate: return "rotate";
        case ActionKind::Flip: return "flip";
        case ActionKind::Color: return "color";
        case ActionKind::Property: return "property";
        case ActionKind::Transform: return "transform";
        case ActionKind::Select: return "select";
        default: return "action";
    }
}

inline std::string actionLabel(ActionKind kind, int count) {
    bool p = count != 1;
    switch (kind) {
        case ActionKind::Create: return p ? fmt::format("Created {} objects", count) : "Created 1 object";
        case ActionKind::Paste: return p ? fmt::format("Pasted {} objects", count) : "Pasted 1 object";
        case ActionKind::Delete: return p ? fmt::format("Deleted {} objects", count) : "Deleted 1 object";
        case ActionKind::Move: return p ? fmt::format("Moved {} objects", count) : "Moved 1 object";
        case ActionKind::Scale: return p ? fmt::format("Scaled {} objects", count) : "Scaled 1 object";
        case ActionKind::Rotate: return p ? fmt::format("Rotated {} objects", count) : "Rotated 1 object";
        case ActionKind::Flip: return p ? fmt::format("Flipped {} objects", count) : "Flipped 1 object";
        case ActionKind::Color: return p ? fmt::format("Recolored {} objects", count) : "Recolored 1 object";
        case ActionKind::Property: return p ? fmt::format("Edited {} objects", count) : "Edited properties";
        case ActionKind::Transform: return p ? fmt::format("Transformed {} objects", count) : "Transformed 1 object";
        case ActionKind::Select:
            if (count <= 0) return "Selection changed";
            return p ? fmt::format("De-selected {} objects", count) : "De-selected 1 object";
        default:
            return p ? fmt::format("Edited {} objects", count) : "Edited 1 object";
    }
}

inline std::string commandLabel(UndoCommand cmd, int count) {
    switch (cmd) {
        case UndoCommand::New: return actionLabel(ActionKind::Create, count);
        case UndoCommand::Paste: return actionLabel(ActionKind::Paste, count);
        case UndoCommand::Delete:
        case UndoCommand::DeleteMulti: return actionLabel(ActionKind::Delete, count);
        case UndoCommand::Transform: return actionLabel(ActionKind::Transform, count);
        case UndoCommand::Select: return actionLabel(ActionKind::Select, count);
        default: return fmt::format("Action {} ({})", static_cast<int>(cmd), count);
    }
}

inline std::string kindLabel(std::string const& kind, int count) {
    if (kind == "add" || kind == "new" || kind == "create") return actionLabel(ActionKind::Create, count);
    if (kind == "paste") return actionLabel(ActionKind::Paste, count);
    if (kind == "color") return actionLabel(ActionKind::Color, count);
    if (kind == "property") return actionLabel(ActionKind::Property, count);
    if (kind == "move") return actionLabel(ActionKind::Move, count);
    if (kind == "scale") return actionLabel(ActionKind::Scale, count);
    if (kind == "rotate") return actionLabel(ActionKind::Rotate, count);
    if (kind == "flip") return actionLabel(ActionKind::Flip, count);
    if (kind == "update" || kind == "transform") return actionLabel(ActionKind::Transform, count);
    if (kind == "delete" || kind == "delete_multi") return actionLabel(ActionKind::Delete, count);
    if (kind == "select") return actionLabel(ActionKind::Select, count);
    return kind.empty() ? "Action" : kind;
}

inline HistoryFilter filterBucket(ActionKind kind) {
    switch (kind) {
        case ActionKind::Create:
        case ActionKind::Paste:
        case ActionKind::Delete:
            return HistoryFilter::CreateDelete;
        case ActionKind::Move:
        case ActionKind::Scale:
        case ActionKind::Rotate:
        case ActionKind::Flip:
        case ActionKind::Transform:
            return HistoryFilter::Transform;
        case ActionKind::Color:
        case ActionKind::Property:
            return HistoryFilter::Edit;
        case ActionKind::Select:
            return HistoryFilter::Select;
        default:
            return HistoryFilter::All;
    }
}

inline HistoryFilter filterBucketKind(std::string const& kind) {
    if (kind == "add" || kind == "new" || kind == "create" || kind == "paste" ||
        kind == "delete" || kind == "delete_multi")
        return HistoryFilter::CreateDelete;
    if (kind == "move" || kind == "scale" || kind == "rotate" || kind == "flip" || kind == "transform")
        return HistoryFilter::Transform;
    if (kind == "color" || kind == "property" || kind == "update")
        return HistoryFilter::Edit;
    if (kind == "select") return HistoryFilter::Select;
    return HistoryFilter::All;
}

inline cocos2d::ccColor3B actionColor(ActionKind kind) {
    switch (kind) {
        case ActionKind::Create:
        case ActionKind::Paste: return {90, 220, 140};
        case ActionKind::Delete: return {255, 100, 100};
        case ActionKind::Move:
        case ActionKind::Scale:
        case ActionKind::Rotate:
        case ActionKind::Flip:
        case ActionKind::Transform: return {120, 180, 255};
        case ActionKind::Color: return {220, 140, 255};
        case ActionKind::Property: return {100, 220, 220};
        case ActionKind::Select: return {255, 210, 100};
        default: return {200, 200, 200};
    }
}

inline cocos2d::ccColor3B kindColor(std::string const& kind) {
    if (kind == "add" || kind == "new" || kind == "create" || kind == "paste") return {90, 220, 140};
    if (kind == "delete" || kind == "delete_multi") return {255, 100, 100};
    if (kind == "color") return {220, 140, 255};
    if (kind == "property") return {100, 220, 220};
    if (kind == "move" || kind == "scale" || kind == "rotate" || kind == "flip" ||
        kind == "update" || kind == "transform")
        return {120, 180, 255};
    if (kind == "select") return {255, 210, 100};
    return {200, 200, 200};
}

inline std::string relativeTime(int64_t timeMs, int64_t nowMs) {
    if (timeMs <= 0) return "";
    int64_t diff = (nowMs - timeMs) / 1000;
    if (diff < 0) diff = 0;
    if (diff < 3) return "now";
    if (diff < 60) return fmt::format("{}s", diff);
    if (diff < 3600) return fmt::format("{}m", diff / 60);
    if (diff < 86400) return fmt::format("{}h", diff / 3600);
    return fmt::format("{}d", diff / 86400);
}

} // namespace paimon::editorhistory
