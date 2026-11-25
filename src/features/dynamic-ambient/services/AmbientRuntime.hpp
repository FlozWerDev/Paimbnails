#pragma once

#include "AmbientMath.hpp"

#include <Geode/Geode.hpp>
#include <cstdint>

namespace paimon::ambient {

enum class AmbientScope : std::uint8_t {
    Buttons,
    Panels,
    Assets,
    Always
};

class AmbientRuntime {
public:
    static AmbientRuntime& get();

    void reload();
    AmbientSettings const& settings();
    bool active();
    bool allows(AmbientScope scope);
    bool isBlockedByGlass() const;

    // Returns false if blocked by active Liquid Glass.
    bool setEnabled(bool enabled);
    void forceDisable();

    std::uint64_t revision() const { return m_revision; }

private:
    AmbientRuntime() = default;
    void ensureLoaded();

    bool m_loaded = false;
    AmbientSettings m_settings;
    std::uint64_t m_revision = 1;
    geode::ListenerHandle m_resizeListener;
};

} // namespace paimon::ambient
