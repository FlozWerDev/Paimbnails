#include "PermissionPolicy.hpp"
#include "FeatureRegistry.hpp"

namespace paimon {

AuthResult PermissionPolicy::authorizeFeature(std::string const& featureName) const {
    auto spec = FeatureRegistry::get().getSpec(featureName);
    if (!spec.has_value()) {
        return AuthResult::deny("Feature desconocido: " + featureName);
    }
    if (!FeatureRegistry::get().isEnabled(featureName)) {
        return AuthResult::deny("Feature deshabilitado: " + featureName);
    }
    return authorize(spec->requiredTier);
}

} // namespace paimon
