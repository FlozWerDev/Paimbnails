#pragma once

// Globed is optional and only the soft-link API is used, so without the headers
// or the mod every call is a no-op and progress falls back to the server.

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::versus::gl {

// Headers were available at build time.
bool compiled();
// Globed is installed, enabled, and its API table answered.
bool present();
bool connected();
// In a level with an active session, which is what the fast channel needs.
bool inSession();

uint32_t pingMs();
std::vector<int> sessionPlayers();
bool rivalInSession(int accountId);
std::string rivalName(int accountId);

// Hide everyone in the level except the rival, so a duel in the global room
// still looks like a duel. Undone on level exit.
void isolateRival(int accountId);
void restoreVisibility();
// The Wraith card: the caster asks us to stop drawing them for a few seconds.
void setRivalHidden(bool hidden);

// The Shield card. Survives the next death without desyncing the session.
void grantShield();
bool shieldActive();
void clearShield();

void respawn(bool fullReset);

// Room state, read only: the soft-link API cannot create or join one.
bool inRoom();
uint32_t roomId();
int pinnedLevel();

} // namespace paimon::versus::gl
