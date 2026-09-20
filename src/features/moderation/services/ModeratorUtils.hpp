#pragma once

#include "../../../core/ModAuthFlow.hpp"

namespace PaimonUtils {

inline bool isUserModerator() {
    return paimon::modauth::isVerified();
}

}
