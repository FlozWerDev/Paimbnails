#pragma once

#include "../PhysicsConfig.hpp"
#include "PhysicsObjectShapes.hpp"

#include <Geode/Geode.hpp>

#include <optional>
#include <vector>

class EditorUI;
class GameObject;

namespace paimon::editorphysics {

enum class CaptureRole {
    ReplaceA,
    ReplaceB,
    AddDynamic,
    AddStatic,
};

struct CapturedBody {
    Motion motion = Motion::Static;
    float gravityScale = 0.f;
    int exactGroup = 0;
    std::vector<geode::WeakRef<GameObject>> objects;
};

// Enough to rebuild the object in the preview with its own art instead of a
// stretched copy of its main frame.
struct BodyVisual {
    GameObject* object = nullptr;
    int objectID = 0;
    Vec2 offset;
    Vec2 size;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool flipX = false;
    bool flipY = false;
    int zOrder = 0;
    cocos2d::ccColor3B baseColor{255, 255, 255};
    cocos2d::ccColor3B detailColor{255, 255, 255};
    unsigned char baseOpacity = 255;
    unsigned char detailOpacity = 255;
    ShapeKind kind = ShapeKind::Box;
};

struct ShapeCounts {
    std::size_t boxes = 0;
    std::size_t ramps = 0;
    std::size_t rounds = 0;
};

struct ResolvedBody {
    BodySpec spec;
    int preferredGroup = 0;
    std::vector<GameObject*> objects;
    std::vector<BodyVisual> visuals;
    ShapeCounts shapes;
};

struct CaptureReport {
    std::size_t objects = 0;
    int group = 0;
};

class PhysicsWorkspace {
public:
    static PhysicsWorkspace& get();

    void bind(EditorUI* ui);
    geode::Result<CaptureReport> capture(EditorUI* ui, CaptureRole role);
    geode::Result<CaptureReport> consumePending(EditorUI* ui);
    geode::Result<std::vector<ResolvedBody>> resolve(
        EditorUI* ui,
        LabConfig const& config
    ) const;
    geode::Result<Motion> toggleMotion(std::size_t index);

    void beginCapture(CaptureRole role);
    void clear();

    bool hasPendingCapture() const;
    bool empty() const;
    std::vector<CapturedBody> const& bodies() const;

private:
    geode::WeakRef<EditorUI> m_ui;
    std::vector<CapturedBody> m_bodies;
    std::optional<CaptureRole> m_pending;
};

} // namespace paimon::editorphysics
