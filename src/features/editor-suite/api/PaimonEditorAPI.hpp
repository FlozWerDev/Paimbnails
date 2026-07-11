#pragma once

// =============================================================================
// Paimon Editor API — public surface for the Editor Suite.
//
// Design goals (vs Alphalaneous/EditorTab-API, Tinker, BetterEdit):
//   1. Compatible mental model: same mode IDs as EditorTab-API, similar
//      addTab / createEditButtonBar / switch callbacks.
//   2. Better than foreign APIs for our stack:
//        - enable predicates (tabs auto-hide when module off)
//        - dual backend (inject | foreign-safe fallback)
//        - suite events (UI show/hide, exit, UI scale, group-view refresh)
//        - FakePause for pause-menu actions without opening the pause UI
//        - trigger / selection helpers shared by modules
//   3. Zero hard dependency on alphalaneous.editortab_api or BetterEdit.
//
// Modules should include this umbrella header (or the specific sub-header).
// =============================================================================

#include "Compat.hpp"
#include "Events.hpp"
#include "FakePause.hpp"
#include "EditCommands.hpp"
#include "GroupViewAPI.hpp"
#include "UIScaleAPI.hpp"
#include "TriggerUtil.hpp"
#include "Selection.hpp"

#include "../tabs/EditorTabsAPI.hpp"
#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"
#include "../EditorUIKit.hpp"
