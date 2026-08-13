#pragma once

#include "../PhysicsTypes.hpp"

class GameObject;
class LevelEditorLayer;

namespace paimon::editorphysics {

enum class ShapeKind {
    Box,
    Ramp,
    Round,
};

// The collision footprint of a single editor object, in world coordinates.
struct ObjectShape {
    ShapeKind kind = ShapeKind::Box;
    Vec2 center;
    Vec2 halfSize;
    float radius = 0.f;
    int vertexCount = 0;
    Vec2 vertices[4]{};
};

// Reads the shape GD itself uses for the object: a radius for orbs and rings, a
// triangle for slopes, the oriented corners for anything the editor rotated off
// the axes, and the plain rect for everything else.
ObjectShape shapeOf(LevelEditorLayer* editor, GameObject* object);

Fixture fixtureFrom(ObjectShape const& shape, Vec2 bodyCenter);

} // namespace paimon::editorphysics
