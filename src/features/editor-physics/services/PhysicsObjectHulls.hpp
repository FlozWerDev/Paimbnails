#pragma once

#include "../PhysicsTypes.hpp"

class GameObject;

namespace paimon::editorphysics {

// Convex outline of an object's art, normalised so bounds span [-0.5, 0.5] and
// wound counter-clockwise. `vertexCount` stays 0 when art fills its own bounds.
struct Silhouette {
    int vertexCount = 0;
    Vec2 vertices[kMaxVertices]{};
};

// Traced once per object ID off the sprite the game itself draws, so a spike
// collides as a triangle and a saw as a disc without a hand written table.
Silhouette const& silhouetteOf(GameObject* object);

} // namespace paimon::editorphysics
