#pragma once

#include "../PhysicsTypes.hpp"

class GameObject;
class LevelEditorLayer;

namespace paimon::editorphysics {

enum class ShapeKind {
    Box,
    Ramp,
    Round,
    Hull,
};

// The collision footprint of a single editor object, in world coordinates.
struct ObjectShape {
    ShapeKind kind = ShapeKind::Box;
    Vec2 center;
    Vec2 halfSize;
    float radius = 0.f;
    int vertexCount = 0;
    Vec2 vertices[kMaxVertices]{};
};

// The collision shape GD itself uses for the object.
ObjectShape shapeOf(LevelEditorLayer* editor, GameObject* object);

// The footprint the shape really covers, so a slope weighs the half block it
// fills instead of the whole rect it spans.
float shapeArea(ObjectShape const& shape);

// Where that footprint's mass sits: the middle of the rect for a box or a
// circle, and the real centroid for a slope or a traced silhouette.
Vec2 shapeCentroid(ObjectShape const& shape);

Fixture fixtureFrom(ObjectShape const& shape, Vec2 bodyCenter);

} // namespace paimon::editorphysics
