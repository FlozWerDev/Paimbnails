#pragma once
// host-harness stand-in for <Geode/utils/function.hpp>: the guide service
// header uses CopyableFunction only as a callback alias in declarations,
// which std::function covers here. the geode sdk provides the real one.
#include <functional>

namespace geode {
template <class Sig>
using CopyableFunction = std::function<Sig>;
}
