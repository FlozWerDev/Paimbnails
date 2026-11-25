#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../services/ProfileIdentityStore.hpp"
#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// opened from the profile name: level / versus ranking, plus the custom badge.
class ProfileIdentityPopup : public PaimonPopup {
public:
    static ProfileIdentityPopup* create(std::string const& username,
        std::vector<paimon::profiles::IdentityEntry> entries);

protected:
    bool init(std::string const& username, std::vector<paimon::profiles::IdentityEntry> entries);
};
