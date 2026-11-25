#pragma once

#include "GlassQuality.hpp"
#include "GlassStyle.hpp"

#include <Geode/Geode.hpp>
#include <cstdint>

namespace paimon::glass {

// panel: popup windows and UI containers. Button: ButtonSprites, BasedButtonSprites
// and GD UI sprite buttons. Well: recessed frames inside a glass popup or control
// (lists, nested insets, text inputs, slider grooves); draws without a framebuffer copy.
enum class GlassKind : std::uint8_t { Panel, Button, Well };
enum class GlassScope : std::uint8_t { Popups, Buttons, Always };

// current Liquid Glass settings for every plate, hook and the config popup.
// loaded once, refreshed by reload() when the popup saves.
class GlassRuntime {
public:
    static GlassRuntime& get();

    void reload();
    GlassSettings const& settings();
    GlassTier tier();
    GlassProfile const& profile();
    bool active() { return tier() != GlassTier::Off; }
    bool allows(GlassScope scope);
    // cached once per second: plates must not query saved values per frame.
    bool reducedMotion() const { return m_reducedMotion; }
    void refreshMotion();
    // bumped by reload(); plates recompute cached style when it changes.
    std::uint64_t revision() const { return m_revision; }

private:
    GlassRuntime() = default;
    void ensureLoaded();

    bool m_loaded = false;
    bool m_reducedMotion = false;
    GlassSettings m_settings;
    GlassTier m_tier = GlassTier::Off;
    GlassProfile m_profile;
    std::uint64_t m_revision = 1;
    geode::ListenerHandle m_resizeListener;
};

} // namespace paimon::glass
