#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::quickhub {

struct CustomQuickButton;

std::filesystem::path quickHubImagesDir();
std::filesystem::path quickHubSfxDir();

bool isQuickHubAudioFile(std::filesystem::path const& path);

// Online kind not downloaded: fires downloadSFX + notify, returns "".
std::string resolveQuickButtonSfxPath(CustomQuickButton const& b);

// Duration in ms via FMOD OPENONLY; false if FMOD can't open it.
bool probeQuickButtonSfxDuration(std::string const& absPath, unsigned int* outMs);

// Plays the custom SFX (volume/pitch/start/end/fades); false if none.
bool playQuickButtonSfx(CustomQuickButton const& b);

void stopQuickButtonSfx();

// Original-sound suppression window (sync activate only).
void beginQuickButtonSfxSuppress();
bool consumeQuickButtonSfxSuppress();
void clearQuickButtonSfxSuppress();

// No custom SFX: same as item->activate().
void activateItemWithQuickButtonSfx(cocos2d::CCMenuItem* item, CustomQuickButton const& def);

} // namespace paimon::quickhub
