#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>

namespace cocos2d {
class CCNode;
class CCTexture2D;
}

namespace paimon::capture {

struct ScreenSize {
    float width = 0.f;
    float height = 0.f;
};

// enables capture context for sprites with manual draw() (paimonshader*).
class ActiveGuard {
public:
    explicit ActiveGuard(cocos2d::CCSize const& logicalSize);
    ~ActiveGuard();
    ActiveGuard(ActiveGuard const&) = delete;
    ActiveGuard& operator=(ActiveGuard const&) = delete;

private:
    bool m_hadPrev = false;
    bool m_prevActive = false;
    ScreenSize m_prevSize{};
};

} // namespace paimon::capture