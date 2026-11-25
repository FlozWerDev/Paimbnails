#include "../src/features/progressbar/services/ProgressBarMath.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace paimon::progressbar;
    float const nan = std::numeric_limits<float>::quiet_NaN();
    float const inf = std::numeric_limits<float>::infinity();

    // Non-finite config values fall back instead of reaching cocos.
    assert(sanitizeFloat(nan, 1.f, 0.05f, 5.f) == 1.f);
    assert(sanitizeFloat(inf, 1.f, 0.05f, 5.f) == 1.f);
    assert(sanitizeFloat(-inf, 0.f, -10.f, 10.f) == 0.f);
    // Finite values clamp to range and pass through untouched inside it.
    assert(sanitizeFloat(99.f, 1.f, 0.05f, 5.f) == 5.f);
    assert(sanitizeFloat(-99.f, 1.f, 0.05f, 5.f) == 0.05f);
    assert(sanitizeFloat(2.5f, 1.f, 0.05f, 5.f) == 2.5f);

    assert(wrapRotation(0.f) == 0.f);
    assert(wrapRotation(360.f) == 0.f);
    assert(wrapRotation(370.f) == 10.f);
    assert(wrapRotation(-90.f) == 270.f);
    assert(wrapRotation(nan) == 0.f);
    assert(wrapRotation(inf) == 0.f);
}
