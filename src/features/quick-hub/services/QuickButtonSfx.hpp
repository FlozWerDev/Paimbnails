#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::quickhub {

struct CustomQuickButton;

std::filesystem::path quickHubImagesDir();
std::filesystem::path quickHubSfxDir();

bool isQuickHubAudioFile(std::filesystem::path const& path);

// online kind not downloaded: fires downloadsfx + notify, returns "".
std::string resolveQuickButtonSfxPath(CustomQuickButton const& b);

// duration in ms via fmod openonly; false if fmod can't open it.
bool probeQuickButtonSfxDuration(std::string const& absPath, unsigned int* outMs);

// plays the custom sfx (volume/pitch/start/end/fades); false if none.
bool playQuickButtonSfx(CustomQuickButton const& b);

void stopQuickButtonSfx();

// original-sound suppression window (sync activate only).
void beginQuickButtonSfxSuppress();
bool consumeQuickButtonSfxSuppress();
void clearQuickButtonSfxSuppress();

// no custom sfx: same as item->activate().
void activateItemWithQuickButtonSfx(cocos2d::CCMenuItem* item, CustomQuickButton const& def);

} // namespace paimon::quickhub
