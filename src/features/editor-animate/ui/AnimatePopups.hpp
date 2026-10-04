#pragma once

#include <functional>
#include <string>

namespace paimon::animate {

enum class CenterTab { Playback = 0, Start, Bake, View };

void openCenter(CenterTab tab = CenterTab::Playback);
void openClips();
void openFrame();

// the callback never sees an empty (trimmed) value and runs after the popup closed.
void askText(
    std::string title,
    std::string hint,
    std::string initial,
    std::string filter,
    int maxChars,
    std::function<void(std::string const&)> onConfirm
);
void confirm(std::string title, std::string body, std::string action, std::function<void()> onConfirm);

void bake(int clipIndex);
void bakeAll();
void createWithPrompt(bool fromSelection);
void importWithPrompt();

} // namespace paimon::animate
