#pragma once

#include <string>
#include <vector>

namespace paimon {

enum class PermissionTier : int {
    Viewer      = 0,   // solo lectura (ver thumbs, emotes)
    User        = 1,   // interaccion basica (descargar, buscar)
    Contributor = 2,   // upload de contenido
    Moderator   = 3,   // moderacion (aprobar/rechazar, ban)
    Admin       = 4    // acceso total (config server, forzar acciones)
};

struct FeatureSpec {
    std::string name;                       // ej: "thumbnails", "emotes"
    std::string version;                    // ej: "2.3.5"
    std::vector<std::string> dependencies;  // features requeridos
    PermissionTier requiredTier = PermissionTier::Viewer;
    bool enabledByDefault = true;
};

} // namespace paimon
