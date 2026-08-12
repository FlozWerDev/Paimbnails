#pragma once

#include "../PhysicsConfig.hpp"

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

struct ResolvedBody {
    BodySpec spec;
    int preferredGroup = 0;
    std::vector<GameObject*> objects;
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
