#pragma once

#include <Geode/Geode.hpp>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace paimon {

namespace detail {

class NodeLivenessSentinel final : public cocos2d::CCObject {
public:
    std::shared_ptr<bool> alive;

    NodeLivenessSentinel() : alive(std::make_shared<bool>(true)) {}

    ~NodeLivenessSentinel() override {
        if (alive) {
            *alive = false;
        }
    }
};

inline std::string const& nodeSentinelKey() {
    static std::string const key = "paimon-node-sentinel"_spr;
    return key;
}

} // namespace detail

// Weak handle backed by a node user-object sentinel instead of Geode's WeakRefPool.
// Avoids Geode v6 WeakRefPool::forget() re-entrancy/out_of_range crashes when multiple
// weak references track the same CCNode across features.
template <class T>
class SafeNodeHandle final {
    static_assert(std::is_base_of_v<cocos2d::CCNode, T>,
        "SafeNodeHandle requires a CCNode-derived type");

    T* m_ptr = nullptr;
    std::shared_ptr<bool> m_alive;

public:
    SafeNodeHandle() = default;
    SafeNodeHandle(std::nullptr_t) {}

    explicit SafeNodeHandle(T* node) {
        reset(node);
    }

    void reset(T* node = nullptr) {
        m_ptr = nullptr;
        m_alive.reset();
        if (!node) return;

        auto* existing = geode::cast::typeinfo_cast<detail::NodeLivenessSentinel*>(
            node->getUserObject(detail::nodeSentinelKey()));
        if (existing && existing->alive && *existing->alive) {
            m_ptr = node;
            m_alive = existing->alive;
            return;
        }

        auto* sentinel = new detail::NodeLivenessSentinel();
        node->setUserObject(detail::nodeSentinelKey(), sentinel);
        sentinel->release();
        m_ptr = node;
        m_alive = sentinel->alive;
    }

    SafeNodeHandle& operator=(std::nullptr_t) {
        reset(nullptr);
        return *this;
    }

    SafeNodeHandle& operator=(T* node) {
        reset(node);
        return *this;
    }

    T* get() const {
        return (m_ptr && m_alive && *m_alive) ? m_ptr : nullptr;
    }

    explicit operator bool() const {
        return get() != nullptr;
    }

    bool operator==(T const* other) const {
        return get() == other;
    }

    bool operator!=(T const* other) const {
        return get() != other;
    }
};

} // namespace paimon
