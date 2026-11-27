#include "EditorMcpBridge.hpp"
#include "EditorMcpCatalog.hpp"
#include "../../utils/JsonHelper.hpp"

#include <fmt/format.h>

namespace paimon::editormcp {

namespace {

matjson::Value makeToolSchema(
    char const* name,
    char const* description,
    matjson::Value props,
    matjson::Value required = matjson::Value::array()
) {
    return matjson::makeObject({
        {"name", name},
        {"description", description},
        {"inputSchema", matjson::makeObject({
            {"type", "object"},
            {"properties", std::move(props)},
            {"required", std::move(required)},
        })},
    });
}

matjson::Value pStr(char const* desc) { return matjson::makeObject({{"type", "string"}, {"description", desc}}); }
matjson::Value pStrEnum(char const* desc, std::initializer_list<char const*> vals) {
    return matjson::makeObject({{"type", "string"}, {"description", desc}, {"enum", strArray(vals)}});
}
matjson::Value pNum(char const* desc) { return matjson::makeObject({{"type", "number"}, {"description", desc}}); }
matjson::Value pInt(char const* desc) { return matjson::makeObject({{"type", "integer"}, {"description", desc}}); }
matjson::Value pBool(char const* desc) { return matjson::makeObject({{"type", "boolean"}, {"description", desc}}); }
matjson::Value pObj(char const* desc) { return matjson::makeObject({{"type", "object"}, {"description", desc}}); }
matjson::Value pArrInt(char const* desc) {
    return matjson::makeObject({
        {"type", "array"}, {"description", desc},
        {"items", matjson::makeObject({{"type", "integer"}})},
    });
}

} // namespace

matjson::Value buildToolsListResult() {
    auto tools = matjson::Value::array();

    tools.push(makeToolSchema("get_editor_state",
        "Get live Geometry Dash editor status: active level, object/trigger count, camera center/zoom, used groups, free IDs, and all editor options.",
        matjson::makeObject({})));

    tools.push(makeToolSchema("list_local_levels",
        "List all local created levels available in Geometry Dash.",
        matjson::makeObject({})));

    tools.push(makeToolSchema("open_level_in_editor",
        "Open a local level in the editor by index or name, or create a brand-new blank level with create_new=true.",
        matjson::makeObject({
            {"create_new", pBool("Create a new empty level and open it in the editor")},
            {"index", pInt("Index from list_local_levels")},
            {"name", pStr("Level name (for creating or finding)")},
            {"audio_track", pInt("Official song track index (0..21)")},
            {"song_id", pInt("Custom song ID")},
            {"save_current", pBool("Save the currently open level before switching")},
        })));

    tools.push(makeToolSchema("save_level",
        "Save the currently open level in LevelEditorLayer.",
        matjson::makeObject({})));

    tools.push(makeToolSchema("get_objects",
        "Query objects and triggers in the current level with spatial (worldY by default), ID, group, layer, color, or selection filters.",
        matjson::makeObject({
            {"x_min", pNum("Minimum X coordinate")}, {"x_max", pNum("Maximum X coordinate")},
            {"y_min", pNum("Minimum Y coordinate")}, {"y_max", pNum("Maximum Y coordinate")},
            {"bounds", pObj("Optional {x_min, x_max, y_min, y_max}")},
            {"world_coords", pBool("Use world Y (ground = 0, default true); set false to filter in internal Y (+90)")},
            {"object_id", pInt("Filter by GD object ID")},
            {"group_id", pInt("Filter by group ID")},
            {"editor_layer", pInt("Filter by editor layer")},
            {"z_layer", pStr("Filter by Z-layer (B5..T4 or integer)")},
            {"color_id", pInt("Filter by base or detail color channel ID")},
            {"triggers_only", pBool("Return only triggers")},
            {"selected_only", pBool("Return only selected objects")},
            {"compact", pBool("Omit default fields to save tokens when scanning large levels (default false)")},
            {"include_save_string", pBool("Include raw GD save string per object")},
            {"offset", pInt("Pagination offset")},
            {"limit", pInt("Max objects to return (1..500, default 100)")},
        })));

    tools.push(makeToolSchema("get_object_raw",
        "Inspect the complete raw GD save string and decoded property key map of a single object.",
        matjson::makeObject({
            {"index", pInt("Object index in level")},
            {"unique_id", pInt("Object uniqueID")},
        })));

    tools.push(makeToolSchema("place_object",
        "Place a single block, hazard, orb, portal, glow, text, decoration, or trigger object in the editor.",
        matjson::makeObject({
            {"object_id", pInt("GD object ID (e.g. 1=block, 8=spike, 211=white square, 914=text, 503=glow beam)")},
            {"trigger", pStr("Optional trigger name/alias if placing a trigger via place_object")},
            {"x", pNum("World X position (1 block = 30 units)")},
            {"y", pNum("World Y position (ground = 0, first block row center = 15)")},
            {"rotation", pNum("Clockwise rotation in degrees")},
            {"scale", pNum("Uniform scale (default 1.0)")},
            {"scale_x", pNum("Independent X scale")}, {"scale_y", pNum("Independent Y scale")},
            {"flip_x", pBool("Flip horizontally")}, {"flip_y", pBool("Flip vertically")},
            {"z_layer", pStr("Z-layer: B5, B4, B3, B2, B1, Default, T1, T2, T3, T4 (or int)")},
            {"z_order", pInt("Draw order within Z-layer")},
            {"editor_layer", pInt("Primary editor layer")},
            {"editor_layer2", pInt("Secondary editor layer")},
            {"base_color", pInt("Base color channel ID (1..999 or 1000..1012)")},
            {"detail_color", pInt("Detail color channel ID")},
            {"base_hsv", pStr("Base HSV string e.g. '0a1a1a0a0'")},
            {"detail_hsv", pStr("Detail HSV string")},
            {"groups", pArrInt("Group IDs (1..9999)")},
            {"no_touch", pBool("Disable collision hitbox (decoration only)")},
            {"passable", pBool("Passable platform/block")},
            {"dont_fade", pBool("Disable edge fade")}, {"dont_enter", pBool("Disable enter transition")},
            {"no_effects", pBool("Disable portal/object effects")},
            {"high_detail", pBool("Mark as high detail (LDM)")}, {"hide", pBool("Hide in gameplay")},
            {"group_parent", pBool("Set as group rotation/scale parent")},
            {"text", pStr("Text content when object_id is 914")},
            {"params", pObj("Optional trigger-specific friendly params (move_x, color, degrees, etc.)")},
            {"extra_props", pObj("Raw GD property key->value map (e.g. {\"131\": 15})")},
            {"raw", pStr("Optional raw GD object string '1,id,2,x,3,y;'")},
        }), strArray({"x", "y"})));

    tools.push(makeToolSchema("place_objects_batch",
        "Batch-place up to 2500 objects and/or triggers at once (supports trigger + friendly params on each item) or a raw GD level_string.",
        matjson::makeObject({
            {"objects", matjson::makeObject({
                {"type", "array"},
                {"description", "Array of place_object / place_trigger specs"},
                {"items", matjson::makeObject({{"type", "object"}})},
            })},
            {"level_string", pStr("Raw GD semicolon-separated object string")},
            {"no_undo", pBool("Skip undo entry for bulk generation")},
        })));

    tools.push(makeToolSchema("place_trigger",
        "Place any Geometry Dash trigger (Move, Color, Pulse, Alpha, Toggle, Spawn, Rotate, Follow, Camera, Shader, Song, SFX, Sequence, Item, etc.) using friendly names.",
        matjson::makeObject({
            {"trigger", pStr("Trigger name/alias (e.g. 'move', 'color', 'alpha', 'pulse', 'spawn', 'toggle', 'rotate', 'camera_zoom', 'gradient', 'glitch') or ID")},
            {"object_id", pInt("Optional explicit trigger object ID")},
            {"x", pNum("World X position")}, {"y", pNum("World Y position")},
            {"target_group", pInt("Target group ID (key 51)")},
            {"center_group", pInt("Center / secondary group ID (key 71)")},
            {"duration", pNum("Duration in seconds (key 10)")},
            {"easing", pStr("Easing name (None, EaseInOut, SineOut, BackOut, ElasticOut...) or 0..18")},
            {"easing_rate", pNum("Easing rate (default 2.0)")},
            {"spawn_triggered", pBool("Spawn triggered (key 62)")},
            {"touch_triggered", pBool("Touch triggered (key 11)")},
            {"multi_trigger", pBool("Multi-trigger (key 87)")},
            {"groups", pArrInt("Group IDs assigned to this trigger itself")},
            {"params", pObj("Trigger-specific params: move_x, move_y, lock_player_x, lock_player_y, target_color, color (#RRGGBB or {r,g,b}), opacity, blending, fade_in, hold, fade_out, delay, delay_variance, remap, chance, stop_mode, degrees, times_360, zoom, activate_group, item_id, target_count, area_x, area_y, area_length, min_layer, max_layer, etc.")},
            {"extra_props", pObj("Additional raw GD numeric key->value overrides")},
        }), strArray({"x", "y"})));

    tools.push(makeToolSchema("update_objects",
        "Modify properties, positions (worldY by default), transforms, colors, HSV, Z-layers, groups, or trigger settings of matching objects.",
        matjson::makeObject({
            {"indices", pArrInt("Object indices to update")},
            {"unique_ids", pArrInt("Object uniqueIDs to update")},
            {"group_id", pInt("Update all objects in group_id")},
            {"selected_only", pBool("Update currently selected objects")},
            {"bounds", pObj("Optional {x_min, x_max, y_min, y_max} filter")},
            {"world_coords", pBool("Use world Y for bounds filter & y (default true)")},
            {"x", pNum("New X")}, {"y", pNum("New world Y")},
            {"dx", pNum("Offset X")}, {"dy", pNum("Offset Y")},
            {"rotation", pNum("New rotation")}, {"d_rotation", pNum("Delta rotation")},
            {"scale", pNum("New uniform scale")}, {"scale_x", pNum("New X scale")}, {"scale_y", pNum("New Y scale")},
            {"flip_x", pBool("Set flipX")}, {"flip_y", pBool("Set flipY")},
            {"z_layer", pStr("New Z-layer")}, {"z_order", pInt("New Z-order")},
            {"editor_layer", pInt("New editorLayer1")}, {"editor_layer2", pInt("New editorLayer2")},
            {"base_color", pInt("New base color channel")}, {"detail_color", pInt("New detail color channel")},
            {"base_hsv", pStr("New base HSV string e.g. '0a1a1a0a0'")},
            {"detail_hsv", pStr("New detail HSV string")},
            {"add_groups", pArrInt("Group IDs to add")}, {"remove_groups", pArrInt("Group IDs to remove")},
            {"dont_fade", pBool("Set dontFade")}, {"dont_enter", pBool("Set dontEnter")},
            {"no_effects", pBool("Set noEffects")},
            {"high_detail", pBool("Set highDetail")}, {"no_touch", pBool("Set noTouch")},
            {"passable", pBool("Set passable")}, {"hide", pBool("Set hide")},
            {"group_parent", pBool("Set groupParent")},
            {"text", pStr("New text for Text Object 914")},
            {"extra_props", pObj("Raw GD property key->value updates")},
            {"no_undo", pBool("Skip undo entry when rebuilding objects (default false)")},
        })));

    tools.push(makeToolSchema("delete_objects",
        "Delete objects by indices, unique_ids, group_id, object_id, bounds (worldY by default), selected_only, or all=true.",
        matjson::makeObject({
            {"indices", pArrInt("Object indices to delete")},
            {"unique_ids", pArrInt("Object uniqueIDs to delete")},
            {"group_id", pInt("Delete objects in group")},
            {"object_id", pInt("Delete objects of ID")},
            {"bounds", pObj("{x_min, x_max, y_min, y_max} in world Y by default")},
            {"world_coords", pBool("Use world Y for bounds (default true)")},
            {"selected_only", pBool("Delete selected objects")},
            {"all", pBool("Delete ALL objects in the level")},
            {"no_undo", pBool("Skip undo entry")},
        })));

    tools.push(makeToolSchema("transform_objects",
        "Apply geometric transforms (move, rotate, scale, flip_x, flip_y, duplicate) to matching objects.",
        matjson::makeObject({
            {"operation", pStrEnum("Transform operation", {"move", "rotate", "scale", "flip_x", "flip_y", "duplicate"})},
            {"indices", pArrInt("Target indices")}, {"unique_ids", pArrInt("Target uniqueIDs")},
            {"group_id", pInt("Target group")}, {"selected_only", pBool("Use selected objects")},
            {"dx", pNum("Delta X for move/duplicate")}, {"dy", pNum("Delta Y for move/duplicate")},
            {"degrees", pNum("Rotation degrees")},
            {"lock_rotation", pBool("Keep individual object rotations locked during group rotate")},
            {"scale", pNum("Uniform scale factor")}, {"scale_x", pNum("X scale factor")}, {"scale_y", pNum("Y scale factor")},
            {"pivot_x", pNum("Optional pivot X")}, {"pivot_y", pNum("Optional pivot Y (world Y by default)")},
            {"world_coords", pBool("Use world Y for pivot_y (default true)")},
        }), strArray({"operation"})));

    tools.push(makeToolSchema("select_objects",
        "Select objects in the editor UI (and optionally focus camera on them), or deselect with none=true.",
        matjson::makeObject({
            {"none", pBool("Deselect all")},
            {"additive", pBool("Add to existing selection")},
            {"focus_camera", pBool("Center editor camera on selection")},
            {"indices", pArrInt("Indices")}, {"unique_ids", pArrInt("UniqueIDs")},
            {"group_id", pInt("Group ID")}, {"object_id", pInt("Object ID")},
            {"bounds", pObj("{x_min, x_max, y_min, y_max} in world Y by default")},
            {"world_coords", pBool("Use world Y for bounds (default true)")},
        })));

    tools.push(makeToolSchema("undo", "Undo the last editor action.", matjson::makeObject({})));
    tools.push(makeToolSchema("redo", "Redo the last undone editor action.", matjson::makeObject({})));

    tools.push(makeToolSchema("manage_groups",
        "Allocate free group/block/layer IDs, assign or remove groups, set group parents, or summarize all used groups.",
        matjson::makeObject({
            {"action", pStrEnum("Group action", {"summary", "allocate", "add", "remove", "set_parent"})},
            {"kind", pStrEnum("ID kind for allocate", {"group", "block", "layer"})},
            {"count", pInt("For allocate: how many IDs")},
            {"group_id", pInt("Target group ID for add/remove/set_parent")},
            {"indices", pArrInt("Object indices")}, {"unique_ids", pArrInt("Object uniqueIDs")},
            {"selected_only", pBool("Apply to selected objects")},
        })));

    tools.push(makeToolSchema("get_color_channels",
        "Inspect color channels (1..999 and special channels 1000=BG, 1001=G1, 1002=Line, 1003=3DL, 1004=Obj, 1005=P1, 1006=P2, 1007=LBG, 1009=G2, etc.).",
        matjson::makeObject({
            {"channel_id", pInt("Inspect one specific channel ID")},
            {"max_custom", pInt("Scan custom channels up to (default 50)")},
            {"only_active", pBool("Skip untouched default channels (default true)")},
        })));

    tools.push(makeToolSchema("set_color_channel",
        "Configure one color channel (channel_id) or an entire palette atomically (channels array) with RGB/hex, opacity, blending, player color, or copy channel.",
        matjson::makeObject({
            {"channel_id", pInt("Channel ID (1..999 or 1000..1012) when updating a single channel")},
            {"hex", pStr("Hex color e.g. '#00e5ff'")},
            {"r", pInt("Red 0..255")}, {"g", pInt("Green 0..255")}, {"b", pInt("Blue 0..255")},
            {"opacity", pNum("Opacity 0.0..1.0")},
            {"blending", pBool("Enable additive blending")},
            {"player_color", pInt("0=None, 1=Player1, 2=Player2")},
            {"copy_color_id", pInt("Channel ID to copy")},
            {"copy_opacity", pBool("Copy opacity from copy_color_id")},
            {"channels", matjson::makeObject({
                {"type", "array"},
                {"description", "Batch palette update: array of {channel_id, hex/r/g/b, opacity, blending, player_color, copy_color_id, copy_opacity}"},
                {"items", matjson::makeObject({{"type", "object"}})},
            })},
        })));

    tools.push(makeToolSchema("get_level_settings",
        "Read level settings (start gamemode, speed, mini, dual, platformer, BG/G/MG styles, song, physics flags).",
        matjson::makeObject({})));

    tools.push(makeToolSchema("set_level_settings",
        "Update level settings (gamemode, speed, BG/ground/MG textures, song, name, platformer, etc.).",
        matjson::makeObject({
            {"level_name", pStr("Level name")},
            {"start_mode", pInt("0=Cube,1=Ship,2=Ball,3=UFO,4=Wave,5=Robot,6=Spider,7=Swing")},
            {"start_speed", pInt("0=1x, 1=0.5x, 2=2x, 3=3x, 4=4x")},
            {"start_mini", pBool("Start mini")}, {"start_dual", pBool("Start dual")},
            {"two_player_mode", pBool("2-player mode")}, {"platformer_mode", pBool("Platformer mode")},
            {"mirror_mode", pBool("Start mirrored")}, {"rotate_gameplay", pBool("Rotate gameplay")},
            {"reverse_gameplay", pBool("Reverse gameplay")},
            {"background_index", pInt("Background texture index (1..59)")},
            {"ground_index", pInt("Ground texture index (1..22)")},
            {"middleground_index", pInt("Middleground texture index (0..3)")},
            {"ground_line_index", pInt("Ground line index")},
            {"font_index", pInt("Font index")},
            {"audio_track", pInt("Official audio track")}, {"song_id", pInt("Custom song ID")},
            {"song_offset", pNum("Song offset in seconds")},
            {"spawn_group", pInt("Level start spawn group")},
            {"enable_22_changes", pBool("Enable 2.2 physics/changes")},
        })));

    tools.push(makeToolSchema("set_editor_camera",
        "Pan/zoom the editor camera (worldY by default), change current editor layer, and toggle editor options (grid, preview mode, trigger boxes, etc.).",
        matjson::makeObject({
            {"x", pNum("Camera center X")}, {"y", pNum("Camera center Y (world Y by default, ground = 0)")},
            {"world_coords", pBool("Use world Y for y (default true)")},
            {"zoom", pNum("Editor zoom (0.1..4.0)")},
            {"current_layer", pInt("Active editor layer (-1 = All)")},
            {"show_grid", pBool("Show block grid")},
            {"draw_trigger_boxes", pBool("Draw trigger boxes")},
            {"draw_effect_lines", pBool("Draw effect lines")},
            {"show_duration_lines", pBool("Show duration lines")},
            {"show_ground", pBool("Show ground")},
            {"hide_background", pBool("Hide background")},
            {"preview_mode", pBool("Enable live color/trigger preview mode")},
            {"preview_particles", pBool("Preview particles")},
            {"preview_animations", pBool("Preview animations")},
            {"preview_shaders", pBool("Preview 2.2 shaders")},
            {"layer_locking", pBool("Enable layer locking")},
            {"swipe", pBool("Toggle swipe mode")}, {"free_move", pBool("Toggle free move")},
            {"snap", pBool("Toggle snap")}, {"mode", pInt("1=Delete, 2=Build, 3=Edit, 4=View")},
        })));

    tools.push(makeToolSchema("render_1to1",
        "Capture a real-time 1:1 OpenGL PNG render directly from Geometry Dash's LevelEditorLayer (exact sprites, glow, blending, Z-layers, shaders, colors) so you can visually inspect and refine your work!",
        matjson::makeObject({
            {"mode", pStrEnum("Capture framing mode", {"1to1", "viewport", "overview"})},
            {"x", pNum("Camera center X in GD world units (optional, defaults to current camera)")},
            {"y", pNum("Camera center world Y in GD units (ground = 0, optional, defaults to current camera)")},
            {"world_coords", pBool("Use world Y for y and overview bounds (default true)")},
            {"zoom", pNum("1.0 = exact 1:1 gameplay zoom (default 1.0)")},
            {"width", pInt("Output PNG width in pixels (64..3840, default 1280)")},
            {"height", pInt("Output PNG height in pixels (64..3840, default 720)")},
            {"show_grid", pBool("Overlay GD block grid for alignment inspection (default false)")},
            {"show_coords", pBool("Overlay world X / worldY ruler ticks and ground line (defaults to show_grid)")},
            {"show_hitboxes", pBool("Draw solid (green), hazard (red), and missing-no_touch deco (orange) hitbox outlines")},
            {"highlight_group", pInt("Highlight all objects in this group ID with cyan outlines")},
            {"show_ui", pBool("Include EditorUI buttons (default false)")},
            {"show_trigger_boxes", pBool("Draw trigger hitboxes (default false)")},
            {"preview_colors", pBool("Enable previewMode during render for accurate colors (default true)")},
            {"bounds", pObj("For mode='overview': {x_min, x_max, y_min, y_max} in world Y")},
            {"save_path", pStr("Optional path to also write PNG to disk")},
        })));

    tools.push(makeToolSchema("search_object_catalog",
        "Search all 4000+ Geometry Dash 2.2091 object IDs and sprite frames (blocks, spikes, slopes, portals, orbs, pads, saws, glow, pixel art, triggers).",
        matjson::makeObject({
            {"query", pStr("Keyword or numeric object ID, e.g. 'glow', 'spike', 'brick', 'pixel', 'saw', 'portal'")},
            {"category", pStr("Optional filter: Blocks, Spikes, Saws, Slopes, Pads, Orbs, Portals, Triggers, Glow, Pixel Art, ObjectToolbox")},
            {"limit", pInt("Max results (1..250, default 40)")},
        })));

    tools.push(makeToolSchema("get_gd_reference",
        "Get complete Geometry Dash 2.2091 reference info: all triggers & their property keys, object property keys, color channels, Z-layers, easings, gamemodes, editor options, geometry cheatsheet, and decoration tips.",
        matjson::makeObject({
            {"topic", pStrEnum("Reference topic", {"all", "triggers", "properties", "colors", "zlayers", "easings", "gamemodes", "editor_options", "level_settings", "decoration", "geometry"})},
            {"filter", pStr("Optional keyword or ID filter")},
        })));

    tools.push(makeToolSchema("inspect_structure_bounds",
        "Analyze solid blocks, open perimeter faces (openTop/Bottom/Left/Right), slopes (exact span/angle/hypotenuse), hazards, and bounding boxes in world Y (ground=0).",
        matjson::makeObject({
            {"bounds", pObj("Optional {x_min, x_max, y_min, y_max} in world Y")},
            {"group_id", pInt("Optional group ID filter")},
            {"exclude_triggers", pBool("Exclude triggers from bounds (default true)")},
        })));

    tools.push(makeToolSchema("audit_level_quality",
        "Audit level/structure quality: detects duplicates, default-sprite bleed-through, broken trigger wiring, color wash-out/invisible channels, skewed rotations, missing no_touch, below-ground objects, scattered orphans, and unrequested background layers.",
        matjson::makeObject({
            {"structure_only", pBool("Flag background Z-layers (B5/B4) as errors (default false)")},
            {"bounds", pObj("Optional core {x_min, x_max, y_min, y_max} in world Y")},
            {"margin", pNum("Allowed distance outside core bounds before flagging orphan (default 90.0)")},
        })));

    tools.push(makeToolSchema("get_sprite_metrics",
        "Query live GD sprite frame dimensions (widthUnits, heightUnits, offsets) and pivot/border notes for one or more object IDs before placing them.",
        matjson::makeObject({
            {"object_id", pInt("Single GD object ID")},
            {"object_ids", pArrInt("List of GD object IDs (up to 40)")},
        })));

    tools.push(makeToolSchema("validate_gameplay_path",
        "Analyze playable path metrics (start mode/speed, estimated cube jump height & span, duration, End trigger presence) and detect hazards overlapping solid blocks.",
        matjson::makeObject({
            {"x_min", pNum("Optional minimum X")},
            {"x_max", pNum("Optional maximum X")},
        })));

    tools.push(makeToolSchema("mirror_or_clone_structure",
        "Clone and/or mirror a decorated structure (group_id, unique_ids, or world-Y bounds) with optional offset, group allocation, and color channel remapping.",
        matjson::makeObject({
            {"group_id", pInt("Source group ID")},
            {"unique_ids", pArrInt("Source uniqueIDs")},
            {"bounds", pObj("Source {x_min, x_max, y_min, y_max} in world Y")},
            {"dx", pNum("Offset X")}, {"dy", pNum("Offset Y")},
            {"mirror_x", pBool("Mirror horizontally around pivot")},
            {"mirror_y", pBool("Mirror vertically around pivot")},
            {"pivot_x", pNum("Optional mirror pivot X")}, {"pivot_y", pNum("Optional mirror pivot world Y")},
            {"new_group_id", pInt("Assign cloned objects to this group ID")},
            {"allocate_group", pBool("Allocate next free group ID if new_group_id omitted")},
            {"color_map", pObj("Optional channel remap {\"1\": 21, \"2\": 22}")},
            {"no_undo", pBool("Skip undo entry")},
        })));

    return matjson::makeObject({{"tools", tools}});
}

matjson::Value buildResourcesListResult() {
    return matjson::makeObject({
        {"resources", jsonArray({
            matjson::makeObject({
                {"uri", "gd://reference/triggers"},
                {"name", "All Geometry Dash 2.2091 Triggers"},
                {"description", "Complete catalog of GD triggers with Object IDs, categories, and property keys."},
                {"mimeType", "text/markdown"},
            }),
            matjson::makeObject({
                {"uri", "gd://reference/object-properties"},
                {"name", "GD Object Save-String Property Keys"},
                {"description", "Numeric property keys (1..535+) used by GD objects and triggers."},
                {"mimeType", "text/markdown"},
            }),
            matjson::makeObject({
                {"uri", "gd://reference/editor-options"},
                {"name", "GD Editor Options & Level Settings"},
                {"description", "All editor toggles, camera options, Z-layers, color channels, gamemodes, and level settings."},
                {"mimeType", "application/json"},
            }),
            matjson::makeObject({
                {"uri", "gd://reference/decoration-guide"},
                {"name", "GD Coordinate System & Decoration Guide"},
                {"description", "1:1 block metrics, Z-layering, blending glow, vector shapes, and visual workflow."},
                {"mimeType", "text/markdown"},
            }),
            matjson::makeObject({
                {"uri", "gd://reference/geometry-cheatsheet"},
                {"name", "GD Vector Primitives & Geometry Cheatsheet"},
                {"description", "Exact triangle/circle/arc pivots, slope hypotenuse lengths/angles, and speed/jump metrics."},
                {"mimeType", "text/markdown"},
            }),
            matjson::makeObject({
                {"uri", "gd://editor/state"},
                {"name", "Live Editor State"},
                {"description", "Real-time snapshot of the currently open Geometry Dash editor."},
                {"mimeType", "application/json"},
            }),
        })},
    });
}

matjson::Value readMcpResource(std::string_view uri) {
    if (uri == "gd://reference/triggers") {
        return matjson::makeObject({{"contents", jsonArray({
            matjson::makeObject({
                {"uri", std::string(uri)},
                {"mimeType", "text/markdown"},
                {"text", buildReferenceMarkdown("triggers")},
            }),
        })}});
    }
    if (uri == "gd://reference/object-properties") {
        return matjson::makeObject({{"contents", jsonArray({
            matjson::makeObject({
                {"uri", std::string(uri)},
                {"mimeType", "text/markdown"},
                {"text", buildReferenceMarkdown("properties")},
            }),
        })}});
    }
    if (uri == "gd://reference/decoration-guide") {
        return matjson::makeObject({{"contents", jsonArray({
            matjson::makeObject({
                {"uri", std::string(uri)},
                {"mimeType", "text/markdown"},
                {"text", buildReferenceMarkdown("decoration")},
            }),
        })}});
    }
    if (uri == "gd://reference/geometry-cheatsheet") {
        return matjson::makeObject({{"contents", jsonArray({
            matjson::makeObject({
                {"uri", std::string(uri)},
                {"mimeType", "text/markdown"},
                {"text", buildReferenceMarkdown("geometry")},
            }),
        })}});
    }
    if (uri == "gd://reference/editor-options") {
        return matjson::makeObject({{"contents", jsonArray({
            matjson::makeObject({
                {"uri", std::string(uri)},
                {"mimeType", "application/json"},
                {"text", buildReferenceJson("all").dump(2)},
            }),
        })}});
    }
    if (uri == "gd://editor/state") {
        auto stateRes = invokeMcpTool("get_editor_state", matjson::makeObject({}));
        std::string txt = "{}";
        if (stateRes.contains("content") && stateRes["content"].isArray() && stateRes["content"].size() > 0) {
            txt = stateRes["content"][0]["text"].asString().unwrapOr("{}");
        }
        return matjson::makeObject({{"contents", jsonArray({
            matjson::makeObject({
                {"uri", std::string(uri)},
                {"mimeType", "application/json"},
                {"text", txt},
            }),
        })}});
    }
    return matjson::makeObject({{"contents", matjson::Value::array()}});
}

matjson::Value buildPromptsListResult() {
    return matjson::makeObject({
        {"prompts", jsonArray({
            matjson::makeObject({
                {"name", "build_and_decorate"},
                {"description", "Build a cohesive, playable level or section with zero scattered objects, clean alignment, and zoomed 1:1 visual QA."},
                {"arguments", jsonArray({
                    matjson::makeObject({{"name", "theme"}, {"description", "Visual theme (e.g. neon cyber, sunset synthwave, sakura shrine)"}, {"required", false}}),
                    matjson::makeObject({{"name", "include_background"}, {"description", "true/false - only add background if the user wants a full scene (default false for structure-only requests)"}, {"required", false}}),
                })},
            }),
            matjson::makeObject({
                {"name", "decorate_structure"},
                {"description", "Decorate an existing structure strictly in-place (no unrequested background), preserving hitboxes and verifying flush corners/slopes."},
                {"arguments", jsonArray({
                    matjson::makeObject({{"name", "style"}, {"description", "Target visual style or color palette"}, {"required", false}}),
                })},
            }),
            matjson::makeObject({
                {"name", "audit_and_polish"},
                {"description", "Audit an existing level or structure to eliminate scattered objects, misaligned corners, internal seams, and z-order bleeds."},
                {"arguments", jsonArray({
                    matjson::makeObject({{"name", "structure_only"}, {"description", "true if no background should exist"}, {"required", false}}),
                })},
            }),
            matjson::makeObject({
                {"name", "style_from_reference"},
                {"description", "Translate an artwork or reference style into clean GD vector primitives, isolated color channels, and structural motifs."},
                {"arguments", jsonArray({
                    matjson::makeObject({{"name", "target"}, {"description", "Structure or scene target"}, {"required", false}}),
                })},
            }),
            matjson::makeObject({
                {"name", "build_coherent_level"},
                {"description", "Blueprint for a complete, playable, rhythmic level by seconds/beats with validated gameplay and zero scattered deco."},
                {"arguments", jsonArray({
                    matjson::makeObject({{"name", "duration_seconds"}, {"description", "Target level duration in seconds (e.g. 5, 15, 30)"}, {"required", false}}),
                    matjson::makeObject({{"name", "theme"}, {"description", "Visual theme and palette"}, {"required", false}}),
                })},
            }),
            matjson::makeObject({
                {"name", "create_trigger_system"},
                {"description", "Design and wire GD 2.2 trigger chains (Move, Rotate, Pulse, Spawn, Item/Timer logic) cleanly."},
                {"arguments", jsonArray({
                    matjson::makeObject({{"name", "goal"}, {"description", "Effect or mechanic to build"}, {"required", false}}),
                })},
            }),
        })},
    });
}

matjson::Value getMcpPrompt(std::string_view name, matjson::Value const& args) {
    if (name == "build_and_decorate") {
        std::string const theme = paimon::json::stringOr(args["theme"], "modern neon");
        std::string const includeBg = paimon::json::stringOr(args["include_background"], "false");
        return matjson::makeObject({
            {"description", "Build and decorate in Geometry Dash with strict scope, zero scattered objects, and zoomed 1:1 visual QA"},
            {"messages", jsonArray({
                matjson::makeObject({
                    {"role", "user"},
                    {"content", matjson::makeObject({
                        {"type", "text"},
                        {"text", fmt::format(
                            "Inspect the editor first with get_editor_state, get_objects, and get_color_channels. "
                            "Search object IDs with search_object_catalog and check get_gd_reference(topic='decoration'). "
                            "Design a cohesive, purposeful '{}' creation (1 block = 30 units, ground Y=0, row 0 center Y=15) "
                            "with readable gameplay, fair physics spacing, unified color harmony, and ZERO scattered or orphaned objects. "
                            "Strict scope rule (include_background={}): if the user asked only for a structure or gameplay layout, "
                            "do NOT add unrequested background scenery. Set no_touch=true on all decorative pieces, allocate unused "
                            "group/color IDs, keep every corner, slope hypotenuse, and border completely flush with no internal seam "
                            "lines or unfinished edges, and place custom block covers on z_layer='T2' so default sprites do not bleed through. "
                            "Mandatory QA: call render_1to1 in mode='1to1' AND zoomed-in (zoom=2.0, show_grid=true) at corners, slopes, "
                            "and spikes, fixing every misalignment or incomplete detail before finishing.", theme, includeBg)},
                    })},
                }),
            })},
        });
    }
    if (name == "decorate_structure") {
        std::string const style = paimon::json::stringOr(args["style"], "clean modern");
        return matjson::makeObject({
            {"description", "Decorate an existing structure strictly without unrequested background, verifying alignment at zoom"},
            {"messages", jsonArray({
                matjson::makeObject({
                    {"role", "user"},
                    {"content", matjson::makeObject({
                        {"type", "text"},
                        {"text", fmt::format(
                            "Call get_editor_state, get_objects(compact=true), and get_color_channels first (all coordinates use worldY, ground=0). "
                            "Decorate ONLY the existing structure and its spikes/hazards in '{}' style. "
                            "STRICT RULES: (1) Do NOT add any unrequested background, sky, or unrelated scenery outside the structure. "
                            "(2) Never move, delete, or alter original collision hitboxes. "
                            "(3) Allocate unused color channels and a new group ID so existing level colors/groups remain untouched. "
                            "(4) Cover default blocks/slopes cleanly on z_layer='T2' with no_touch=true; ensure every border, corner, "
                            "and slope hypotenuse meets flush with zero overhanging tips, missing sides, or stray internal seams. "
                            "(5) Use unified macro-silhouettes (211, 694, 1759/1339, 1202) with two-tone bevel bands and diagonal gradient "
                            "shading (503/1009 + key 43 HSV shifts or warpY/warpX 131/132) instead of repetitive per-cell tiles; keep decorations "
                            "cohesive with zero scattered or floating orphan pieces. "
                            "(6) Inspect with render_1to1 (mode='1to1' AND zoomed zoom=2.0, show_grid=true on corners, slopes, and spikes) "
                            "and refine until every pixel and edge is clean.", style)},
                    })},
                }),
            })},
        });
    }
    if (name == "audit_and_polish") {
        std::string const structureOnly = paimon::json::stringOr(args["structure_only"], "false");
        return matjson::makeObject({
            {"description", "Audit and polish alignment, bounds, and z-order using automated checks and zoomed 1:1 renders"},
            {"messages", jsonArray({
                matjson::makeObject({
                    {"role", "user"},
                    {"content", matjson::makeObject({
                        {"type", "text"},
                        {"text", fmt::format(
                            "Run audit_level_quality(structure_only={}) and inspect_structure_bounds first. "
                            "Then render zoomed views with render_1to1(mode='1to1', zoom=2.0, show_grid=true) centered on every "
                            "corner, slope junction, spike, and overhang. Remove any scattered orphan objects, fix protruding border "
                            "tips or stray internal seams across contiguous blocks, verify no_touch=true on all deco, and re-run "
                            "audit_level_quality + render_1to1 until zero issues remain.", structureOnly)},
                    })},
                }),
            })},
        });
    }
    if (name == "style_from_reference") {
        std::string const target = paimon::json::stringOr(args["target"], "the open structure");
        return matjson::makeObject({
            {"description", "Adapt a reference artwork's palette and motifs into clean GD vector art"},
            {"messages", jsonArray({
                matjson::makeObject({
                    {"role", "user"},
                    {"content", matjson::makeObject({
                        {"type", "text"},
                        {"text", fmt::format(
                            "Inspect get_color_channels, get_gd_reference(topic='geometry'), and inspect_structure_bounds for {}. "
                            "Extract the reference's palette into unused custom color channels (1..999; never overwrite existing channels "
                            "used by other structures or BG/G unless asked). On light/ivory palettes, keep blending=false for translucent "
                            "layers so highlights do not wash out to white. Build organic art with fine chained 507/508 curved lineart "
                            "(0.12x-0.25x) over soft layered gradients (1009, 1838/1835, 1764, 1887), and build structures as unified "
                            "two-tone beveled silhouettes (211, 694, 1759, 1202, 503, warpY/warpX 131/132) verified via get_sprite_metrics; "
                            "inspect zoomed corners with render_1to1.", target)},
                    })},
                }),
            })},
        });
    }
    if (name == "build_coherent_level") {
        std::string const dur = paimon::json::stringOr(args["duration_seconds"], "10");
        std::string const theme = paimon::json::stringOr(args["theme"], "modern minimalist");
        return matjson::makeObject({
            {"description", "Build a complete playable level with rhythmic pacing, validated gameplay, and zero scattered objects"},
            {"messages", jsonArray({
                matjson::makeObject({
                    {"role", "user"},
                    {"content", matjson::makeObject({
                        {"type", "text"},
                        {"text", fmt::format(
                            "Read get_gd_reference(topic='geometry') and get_gd_reference(topic='gamemodes'). "
                            "Plan a {}s '{}' level by beats: convert target seconds to GD units using the speed table "
                            "(1x = 311.58 u/s, 2x = 387.42 u/s, etc.) and place an End trigger (3600) at the target X. "
                            "Keep jumps/orbs/pads fair and well-spaced, design a cohesive complementary or harmonious palette, "
                            "and ensure zero scattered objects. Run validate_gameplay_path, audit_level_quality, and render_1to1 "
                            "(overview + 1to1 across each section) to verify both playability and visual polish.", dur, theme)},
                    })},
                }),
            })},
        });
    }
    std::string const goal = paimon::json::stringOr(args["goal"], "animated camera and pulsing environment");
    return matjson::makeObject({
        {"description", "Create a GD 2.2 trigger system"},
        {"messages", jsonArray({
            matjson::makeObject({
                {"role", "user"},
                {"content", matjson::makeObject({
                    {"type", "text"},
                    {"text", fmt::format(
                        "Call manage_groups(action='allocate') to reserve unused group IDs, then implement the trigger setup for: {}.\n"
                        "Verify with audit_level_quality and render_1to1.\n\n{}",
                        goal, buildTriggerCatalogPromptBlock())},
                })},
            }),
        })},
    });
}

} // namespace paimon::editormcp
