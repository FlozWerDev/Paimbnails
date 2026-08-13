#include "PhysicsObjectShapes.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/OBB2D.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::editorphysics {

namespace {

// Every orb, ring and pickup GD collides against with a radius instead of a box.
bool isRoundType(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
        case GameObjectType::SecretCoin:
        case GameObjectType::UserCoin:
        case GameObjectType::Collectible:
            return true;
        default:
            return false;
    }
}

float signedArea(Vec2 const* vertices, int count) {
    float area = 0.f;
    for (int i = 0; i < count; ++i) {
        Vec2 const& current = vertices[i];
        Vec2 const& next = vertices[(i + 1) % count];
        area += current.x * next.y - next.x * current.y;
    }
    return area * 0.5f;
}

// The solver reads edge normals as outward facing, which only holds for
// counter-clockwise winding.
void makeCounterClockwise(ObjectShape& shape) {
    if (shape.vertexCount < 3 || signedArea(shape.vertices, shape.vertexCount) >= 0.f) return;
    std::reverse(shape.vertices, shape.vertices + shape.vertexCount);
}

void fitBoundsToVertices(ObjectShape& shape) {
    float minX = shape.vertices[0].x;
    float minY = shape.vertices[0].y;
    float maxX = minX;
    float maxY = minY;
    for (int i = 1; i < shape.vertexCount; ++i) {
        minX = std::min(minX, shape.vertices[i].x);
        minY = std::min(minY, shape.vertices[i].y);
        maxX = std::max(maxX, shape.vertices[i].x);
        maxY = std::max(maxY, shape.vertices[i].y);
    }
    Vec2 const shift{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f};
    for (int i = 0; i < shape.vertexCount; ++i) {
        shape.vertices[i].x -= shift.x;
        shape.vertices[i].y -= shift.y;
    }
    shape.center.x += shift.x;
    shape.center.y += shift.y;
    shape.halfSize = {
        std::max((maxX - minX) * 0.5f, 0.5f),
        std::max((maxY - minY) * 0.5f, 0.5f),
    };
}

bool orientedCorners(GameObject* object, ObjectShape& shape) {
    auto* box = object->getOrientedBox();
    if (!box) return false;
    for (int i = 0; i < 4; ++i) {
        auto const corner = box->m_corners[i];
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) return false;
        shape.vertices[i] = {corner.x - shape.center.x, corner.y - shape.center.y};
    }
    shape.vertexCount = 4;
    if (std::abs(signedArea(shape.vertices, 4)) < 1.f) {
        shape.vertexCount = 0;
        return false;
    }
    makeCounterClockwise(shape);
    fitBoundsToVertices(shape);
    return true;
}

// A GD slope is the right triangle inside its rect: the hypotenuse follows the
// walkable surface and the solid mass sits on the side the floor faces.
void buildRamp(GameObject* object, ObjectShape& shape) {
    float const hx = shape.halfSize.x;
    float const hy = shape.halfSize.y;
    bool const massBelow = object->slopeFloorTop();
    bool const uphill = object->m_slopeUphill;

    shape.vertexCount = 3;
    if (uphill) {
        shape.vertices[0] = {-hx, -hy};
        shape.vertices[1] = {hx, hy};
        shape.vertices[2] = massBelow ? Vec2{hx, -hy} : Vec2{-hx, hy};
    } else {
        shape.vertices[0] = {-hx, hy};
        shape.vertices[1] = {hx, -hy};
        shape.vertices[2] = massBelow ? Vec2{-hx, -hy} : Vec2{hx, hy};
    }
    shape.kind = ShapeKind::Ramp;
    makeCounterClockwise(shape);
}

} // namespace

ObjectShape shapeOf(LevelEditorLayer* editor, GameObject* object) {
    ObjectShape shape;
    if (!editor || !object) return shape;

    auto const& rect = object->getObjectRect();
    shape.center = {rect.getMidX(), rect.getMidY()};
    shape.halfSize = {
        std::max(rect.size.width, 1.f) * 0.5f,
        std::max(rect.size.height, 1.f) * 0.5f,
    };

    if (isRoundType(object->m_objectType)) {
        float const fallback = std::min(shape.halfSize.x, shape.halfSize.y);
        float const declared = object->getObjectRadius();
        shape.radius = std::isfinite(declared) && declared > 1.f ? declared : fallback;
        shape.halfSize = {shape.radius, shape.radius};
        shape.kind = ShapeKind::Round;
        return shape;
    }

    if (object->m_objectType == GameObjectType::Slope) {
        buildRamp(object, shape);
        return shape;
    }

    // Off-axis rotations make the rect far larger than the object, so borrow the
    // corners GD already keeps for its own collision.
    float const rotation = object->getRotation();
    if (std::abs(std::remainder(rotation, 90.f)) > 0.5f) orientedCorners(object, shape);
    return shape;
}

Fixture fixtureFrom(ObjectShape const& shape, Vec2 bodyCenter) {
    Fixture fixture;
    fixture.offset = {shape.center.x - bodyCenter.x, shape.center.y - bodyCenter.y};
    fixture.halfSize = shape.halfSize;
    fixture.radius = shape.radius;
    fixture.vertexCount = shape.vertexCount;
    for (int i = 0; i < shape.vertexCount; ++i) fixture.vertices[i] = shape.vertices[i];
    return fixture;
}

} // namespace paimon::editorphysics
