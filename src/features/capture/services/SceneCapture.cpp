#include "SceneCapture.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/loader/Log.hpp>
#include <algorithm>
#include <cstring>

using namespace geode::prelude;

namespace paimon::capture {
namespace {

struct State {
    bool active = false;
    ScreenSize size{};
};

State& state() {
    static State s;
    return s;
}

} // namespace

ActiveGuard::ActiveGuard(CCSize const& logicalSize) {
    auto& s = state();
    m_hadPrev = true;
    m_prevActive = s.active;
    m_prevSize = s.size;
    s.active = true;
    s.size.width = logicalSize.width;
    s.size.height = logicalSize.height;
}

ActiveGuard::~ActiveGuard() {
    if (!m_hadPrev) return;
    auto& s = state();
    s.active = m_prevActive;
    s.size = m_prevSize;
}

} // namespace paimon::capture