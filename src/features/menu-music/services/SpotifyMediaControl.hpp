#pragma once

#include <string>

namespace paimon::menumusic {

// target the Spotify desktop session; never broadcast system media keys.
void sendSpotifyMediaCommand(std::string const& command);

}
