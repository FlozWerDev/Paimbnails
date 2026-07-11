# Editor button textures (Paimon)

Drop PNGs with these **exact filenames** into `resources/` (same folder as
`paim_Paimon.png`). They are picked up by `mod.json` → `resources/*.png` and
loaded via `EditorAssets` with automatic vanilla fallbacks until present.

| File | Button / control |
|------|------------------|
| `paim_hide-ui-on.png` | Hide UI toggle (UI visible) |
| `paim_hide-ui-off.png` | Hide UI toggle (UI hidden) |
| `paim_auto-build-helper.png` | Auto Build Helper toggle |
| `paim_edit-extras.png` | Quick Extras |
| `paim_back-to-content.png` | Back to content |
| `paim_quick-save.png` | Quick save backup |
| `paim_ref-image.png` | Import reference image |
| `paim_ref-clear.png` | Clear reference image |
| `paim_cam-to-object.png` | Camera → selection |
| `paim_object-to-cam.png` | Selection → camera |
| `paim_tool-bh.png` | Relocated Build Helper |
| `paim_tool-align-x.png` | Align X |
| `paim_tool-align-y.png` | Align Y |
| `paim_tool-loop.png` | Create Loop |
| `paim_startpos-prev.png` | Start pos previous |
| `paim_startpos-next.png` | Start pos next (right-facing) |
| `paim_startpos-play.png` | Playtest without start pos |
| `paim_object-search.png` | Object search / Search tab |
| `paim_favorites.png` | Favorites / Favs tab |
| `paim_view-tab.png` | View tab / View panel open |
| `paim_grid-icon.png` | Grid size pill icon |
| `paim_group-summary.png` | Pause → Groups |
| `paim_object-summary.png` | Pause → Objects |
| `paim_backups.png` | Pause → Backups |
| `paim_collab.png` | Edit Level → Collab |
| `paim_editor-history.png` | Editor history undo panel |

## Guidelines

- Prefer **square** icons, ~64–128 px, transparent PNG.
- Icons used inside **circle buttons** should leave a little padding at the edges.
- Until a file exists, the editor keeps working with Geometry Dash vanilla frames
  or short text labels (Groups / Objects / Backups / Favs / View).
