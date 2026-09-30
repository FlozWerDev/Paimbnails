#pragma once

#include "../PhysicsTypes.hpp"

class GameObject;

namespace paimon::editorphysics {

// convex outline of an object's art, normalised so bounds span [-0.5, 0.5] and
// wound counter-clockwise. `vertexcount` stays 0 when art fills its own bounds.
struct Silhouette {
    int vertexCount = 0;
    Vec2 vertices[kMaxVertices]{};
};

// traced once per object id off the sprite the game itself draws, so a spike
// collides as a triangle and a saw as a disc without a hand written table.
Silhouette const& silhouetteOf(GameObject* object);

} // namespace paimon::editorphysics
