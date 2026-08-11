#pragma once

#include <cstddef>

namespace cocos2d {

struct CCPoint {
    float x = 0.f;
    float y = 0.f;

    constexpr CCPoint() = default;
    constexpr CCPoint(float x, float y) : x(x), y(y) {}

    CCPoint& operator+=(CCPoint const& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    CCPoint& operator-=(CCPoint const& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    CCPoint& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }
};

inline CCPoint operator+(CCPoint lhs, CCPoint const& rhs) {
    return lhs += rhs;
}

inline CCPoint operator-(CCPoint lhs, CCPoint const& rhs) {
    return lhs -= rhs;
}

inline CCPoint operator*(CCPoint point, float scalar) {
    return point *= scalar;
}

inline CCPoint operator/(CCPoint point, float scalar) {
    return point * (1.f / scalar);
}

struct CCSize {
    float width = 0.f;
    float height = 0.f;
};

struct CCRect {
    CCPoint origin;
    CCSize size;

    constexpr CCRect() = default;
    constexpr CCRect(float x, float y, float width, float height)
      : origin(x, y), size{width, height} {}
};

class CCNode {
public:
    float getScaleX() const { return m_scaleX; }
    float getScaleY() const { return m_scaleY; }
    void setScaleX(float scale) { m_scaleX = scale; }
    void setScaleY(float scale) { m_scaleY = scale; }
    CCNode* getParent() const { return m_parent; }
    CCPoint convertToNodeSpace(CCPoint point) const { return point; }
    void setPosition(CCPoint position) { m_position = position; }
    void setRotation(float rotation) { m_rotation = rotation; }

private:
    float m_scaleX = 1.f;
    float m_scaleY = 1.f;
    float m_rotation = 0.f;
    CCPoint m_position;
    CCNode* m_parent = nullptr;
};

}

namespace geode {

template <class T>
class Ref {
public:
    Ref() = default;
    Ref(std::nullptr_t) {}
    Ref(T* value) : m_value(value) {}

    Ref& operator=(T* value) {
        m_value = value;
        return *this;
    }

    T* data() const { return m_value; }
    explicit operator bool() const { return m_value != nullptr; }

private:
    T* m_value = nullptr;
};

}
