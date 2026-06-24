#pragma once

#include <cstdint>
#include <string>

namespace paimon::discord {

struct PresencePayload {
    std::string state;
    std::string details;
    std::string largeImage;
    std::string largeImageText;
    std::string smallImage;
    std::string smallImageText;
    int64_t startTimestamp = 0;
    bool operator==(PresencePayload const&) const = default;
};

} // namespace paimon::discord
