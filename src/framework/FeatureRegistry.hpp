#pragma once

#include "FeatureSpec.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

namespace paimon {

class FeatureRegistry {
public:
    static FeatureRegistry& get() {
        static FeatureRegistry instance;
        return instance;
    }

    // Registra un feature. Devuelve false si ya existe uno con ese name.
    bool registerFeature(FeatureSpec spec) {
        std::lock_guard lock(m_mutex);
        if (m_specs.contains(spec.name)) return false;
        std::string name = spec.name;
        m_enabled[name] = spec.enabledByDefault;
        m_specs.emplace(std::move(name), std::move(spec));
        return true;
    }

    // Habilita o deshabilita un feature en runtime.
    void setEnabled(std::string const& name, bool enabled) {
        std::lock_guard lock(m_mutex);
        auto it = m_enabled.find(name);
        if (it != m_enabled.end()) it->second = enabled;
    }

    bool isEnabled(std::string const& name) const {
        std::lock_guard lock(m_mutex);
        auto it = m_enabled.find(name);
        return it != m_enabled.end() && it->second;
    }

    std::optional<FeatureSpec> getSpec(std::string const& name) const {
        std::lock_guard lock(m_mutex);
        auto it = m_specs.find(name);
        if (it == m_specs.end()) return std::nullopt;
        return it->second;
    }

    std::vector<std::string> allFeatureNames() const {
        std::lock_guard lock(m_mutex);
        std::vector<std::string> names;
        names.reserve(m_specs.size());
        for (auto const& [k, _] : m_specs) names.push_back(k);
        return names;
    }

    size_t featureCount() const {
        std::lock_guard lock(m_mutex);
        return m_specs.size();
    }

private:
    FeatureRegistry() = default;
    ~FeatureRegistry() = default;
    FeatureRegistry(FeatureRegistry const&) = delete;
    FeatureRegistry& operator=(FeatureRegistry const&) = delete;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, FeatureSpec> m_specs;
    std::unordered_map<std::string, bool> m_enabled;
};

} // namespace paimon
