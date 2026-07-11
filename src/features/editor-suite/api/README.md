# Paimon Editor API

Internal/public surface for the Editor Suite. Designed to be **compatible** with
[EditorTab-API](https://github.com/Alphalaneous/EditorTab-API) concepts and
**better** for our stack (predicates, events, safe pause actions, trigger helpers).

## Dependency replacements

The suite does not require Tinker or BetterEdit's helper mods:

| External dependency | Local replacement |
|---|---|
| `alphalaneous.editortab_api` | `tabs/EditorTabsRegistry.cpp` |
| `alphalaneous.alphas-ui-pack` | `EditorAssets` + `EditorUIKit` |
| `alphalaneous.good_grid` | `modules/GridControl.cpp` |
| `alphalaneous.level-storage-api` | local editor backup files |
| `hjfod.gmd-api` | direct level-string backups |
| `cvolton.level-id-api` | local level key fallback |
| `alk.better-touch-prio` | real unattached `EditorPauseLayer` actions |

`geode.node-ids` remains the project's existing shared UI compatibility
dependency; this suite adds no new runtime dependencies.

## Include

```cpp
#include "features/editor-suite/api/PaimonEditorAPI.hpp"
// or specific headers: Events.hpp, FakePause.hpp, TriggerUtil.hpp, ...
```

## Tabs (compatible with Alpha EditorTab-API)

```cpp
using namespace paimon::editor_tabs; // BUILD / EDIT / DELETE string IDs

addTab("my.mod/extra-tab", BUILD,
    [] { return createEditButtonBar(nodes); },
    [] { return CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"); },
    [](bool shown, CCNode* content) { /* enter/exit */ },
    [](int rows, int cols, CCNode* content) { /* reload */ }
);
```

Richer form:

```cpp
paimon::editor::tabs::TabDesc d;
d.id = "flozwer.paimbnails2/view-tab";
d.mode = paimon::editor::tabs::Mode::Build;
d.isEnabled = [] { return moduleEnabled("editor-mod-view-panel"); };
d.createContent = ...;
d.createIcon = ...;
paimon::editor::tabs::registerTab(std::move(d));
```

If `alphalaneous.editortab_api` is loaded, we use a **side fallback** so we do
not crash their tab menu. Without it, we inject into the vanilla build tabs.

## Events (Geode v5)

```cpp
EditorUIShowEvent().listen([](EditorUI* ui, bool shown) -> bool {
    return false; // propagate
});

GroupViewUpdateEvent().listen([]() -> bool {
    // rebuild group UI
    return false;
});

EditorUIScaleEvent().listen([](float scale, bool toolbars) -> bool {
    return false;
});
```

Send with `.send(...)` (not `.post`).

## Pause actions

```cpp
paimon::editor::runBuildHelper(LevelEditorLayer::get());
paimon::editor::runAlignX(lel);
paimon::editor::runSaveLevel(lel);
```

These helpers use a real unattached `EditorPauseLayer`; they do not type-pun an
unconstructed object and do not display the pause menu.

## Group View / UI Scale (Tinker-style)

```cpp
paimon::editor::group_view::updateGroupView();
float s = paimon::editor::ui_scale::currentScale();
```

## Trigger helpers

```cpp
using namespace paimon::editor::triggers;
auto col = colorForObjectID(901);
auto objs = objectsInGroup(lel, groupId);
drawSolidLine(a, b, col);
drawDashedLine(a, b, col); // center groups
```
