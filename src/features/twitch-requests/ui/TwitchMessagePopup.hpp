#pragma once
#include "../../../ui/PaimonPopup.hpp"

// popup behind a row's "i" button: the note sent with the level from your web page.

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::twitch {

struct LevelRequest;

class TwitchMessagePopup : public PaimonPopup {
public:
    // `levelname` and `author` are what the level is known by; empty while
    // still loading.
    static TwitchMessagePopup* create(
        LevelRequest const& request, std::string levelName, std::string author);

protected:
    bool init(LevelRequest const& request, std::string levelName, std::string author);
};

} // namespace paimon::twitch
