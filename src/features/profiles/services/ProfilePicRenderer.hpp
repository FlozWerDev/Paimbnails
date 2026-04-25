#pragma once
#include <Geode/Geode.hpp>
#include <string>

struct ProfilePicConfig;

namespace paimon::profile_pic {

// Crea un nodo de foto de perfil con forma, borde y decoraciones.
// Retorna un nodo listo para agregar a la escena.
cocos2d::CCNode* composeProfilePicture(
    cocos2d::CCNode* imageNode,
    float targetSize,
    ProfilePicConfig const& config
);

} // namespace paimon::profile_pic
