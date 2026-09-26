#pragma once

// Globed is optional and only soft-linked, so without the headers or the mod
// every call is a no-op and progress falls back to the server.

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::versus::gl {

// headers were available at build time.
bool compiled();
// installed, enabled, and its API table answered.
bool present();
bool connected();
// in a level with an active session, which is what the fast channel needs.
bool inSession();

uint32_t pingMs();
std::vector<int> sessionPlayers();
bool rivalInSession(int accountId);
std::string rivalName(int accountId);

// hide everyone except the rival, so a global-room duel still looks like a
// duel. Undone on level exit.
void isolateRival(int accountId);
void restoreVisibility();
// the Wraith card: the caster asks us to stop drawing them for a few seconds.
void setRivalHidden(bool hidden);

// the Shield card: survives the next death without desyncing the session.
void grantShield();
bool shieldActive();
void clearShield();

void respawn(bool fullReset);

// room state, read only: soft-link can't create or join one.
bool inRoom();
uint32_t roomId();
int pinnedLevel();

} // namespace paimon::versus::gl
