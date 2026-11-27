#include "EditorMcpProtocol.hpp"

#include "EditorMcpBridge.hpp"
#include "EditorMcpCatalog.hpp"

#include <string>
#include <vector>

namespace paimon::editormcp {

namespace {

constexpr char const* kServerInstructions =
    "Geometry Dash 2.2091 Editor MCP Server (Paimbnails).\n"
    "Coordinate system: 1 standard block = 30x30 GD units. Ground surface is worldY=0; "
    "the center of the first block row on the ground is worldY=15. ALL tools (place_object, place_objects_batch, "
    "place_trigger, get_objects, delete_objects, select_objects, transform_objects, set_editor_camera, render_1to1, "
    "inspect_structure_bounds, audit_level_quality, mirror_or_clone_structure) operate in worldY by default "
    "(get_objects also exposes internal y=worldY+90). "
    "Standard 16:9 1:1 viewport height at zoom=1.0 is 320 GD units (~10.67 blocks).\n"
    "Strict Rules (MUST ALWAYS FOLLOW):\n"
    "1. Obey user scope strictly: build ONLY what the user asks for. If the user asks to build or decorate a structure, "
    "decorate ONLY the structure (blocks, slopes, edges, internal details, and its spikes/hazards) - do NOT add unrequested "
    "backgrounds, skies, silhouettes, or scenery unless explicitly requested.\n"
    "2. Zero scattered or orphaned objects: every placed object must belong intentionally to a cohesive structure, detail, "
    "or gameplay element. Never leave stray floating fragments, unfinished faces, or disconnected decorations.\n"
    "3. Complete, flush geometry & alignment: inspect existing structures with inspect_structure_bounds / get_objects (compact=true saves context) first "
    "and preserve original gameplay hitboxes. Check get_sprite_metrics when unsure of object bounds. Ensure every fill, border, "
    "corner, and slope hypotenuse meets flush with zero gaps, overhanging line tips, or stray internal seam lines across "
    "contiguous blocks. Use z_layer='T2' (or higher) when covering Default blocks/slopes so default sprites never bleed through.\n"
    "4. Coherent level & gameplay design: for full levels, validate playability with validate_gameplay_path, ensure readable "
    "sightlines, fair physics spacing for the active gamemode/speed, clear hazard contrast, unified color palette harmony "
    "(use set_color_channel with channels=[...] to configure a full palette atomically), and dedicated unused group/color IDs per structure.\n"
    "5. Mandatory Visual QA before finishing: run audit_level_quality AND render_1to1 in mode='1to1' AND zoomed-in "
    "(zoom=1.8..2.2, show_grid=true — automatically renders world X/Y coordinate rulers!) on corners, slopes, spikes, and junctions "
    "(pass show_hitboxes=true when checking solid/hazard/no_touch alignment) to verify nothing is misaligned, clipped, "
    "incomplete, or out of frame. Fix every defect before reporting done.\n"
    "Workflow:\n"
    "1. Call get_editor_state + inspect_structure_bounds (or get_objects compact=true) + get_color_channels to inspect geometry and free IDs.\n"
    "2. Call get_gd_reference (topics: decoration, geometry) and get_sprite_metrics / search_object_catalog for exact IDs/pivots.\n"
    "3. Build/decorate cleanly with place_object, place_objects_batch (supports trigger+params too), mirror_or_clone_structure, place_trigger, set_color_channel, and set_level_settings.\n"
    "4. Verify with audit_level_quality, validate_gameplay_path (if gameplay), and render_1to1 (overview + 1to1 + zoomed corners/slopes with show_grid=true).\n"
    "Mythic / Feature-Grade Visual & Art Standard (for full deco, art, or complex scenes):\n"
    "- Macro-structured blocks (NO per-cell grid repetition): treat connected platforms/pillars as unified architectural silhouettes "
    "(211, 694, 1759/1339, 1202/1220) with two-tone bevel bands (desaturated main body + contrasting lit side/top bevel) and "
    "stepped diagonal shading strips (503/1009/211 + HSV Value shifts key 43), or true 3D skewing via warpY/warpX (keys 131/132). "
    "Place 4-point corner flares (1835/1837 + 1764) sparingly ONLY on light-catching corners.\n"
    "- Illustrated character & organic art: construct hair, clothing folds, hands, mascots, and detailed props by layering soft "
    "gradients (1009, 1838/1835, 1764, 1887) inside thin curved lineart made of chained small 507/508 segments (0.12x-0.25x scale, "
    "HSV-tinted) instead of crude primitive circles/triangles or raw 914 text.\n"
    "- Depth & color discipline: B5/B4 far silhouettes low-contrast and faded with mist/clouds (129/131 + 503/1888); B2/B1 midground "
    "muted without harsh outlines; T1-T3 foreground crisp at full contrast. Use blending=false with low opacity (0.18-0.45) on "
    "bright/pastel palettes; reserve blending=true + 503/504 rim lights + 2065 atmospheric particles (key 145, key 146=1; triggered via "
    "3608 Spawn Particle with 51=particleGroup, 71=posGroup) for dark/neon scenes.";

matjson::Value rpcOk(matjson::Value id, matjson::Value result) {
    return matjson::makeObject({
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"result", std::move(result)},
    });
}

matjson::Value rpcErr(matjson::Value id, int code, std::string message) {
    return matjson::makeObject({
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"error", matjson::makeObject({
            {"code", code},
            {"message", std::move(message)},
        })},
    });
}

bool handleSingleMessage(matjson::Value const& msg, matjson::Value& outResponse) {
    if (!msg.isObject()) {
        outResponse = rpcErr(nullptr, -32600, "Invalid Request: expected JSON object");
        return true;
    }

    bool const hasId = msg.contains("id") && !msg["id"].isNull();
    matjson::Value id = hasId ? msg["id"] : matjson::Value(nullptr);

    if (!msg.contains("method") || !msg["method"].isString()) {
        if (!hasId) return false;
        outResponse = rpcErr(std::move(id), -32600, "Invalid Request: missing method");
        return true;
    }

    std::string const method = msg["method"].asString().unwrapOr("");
    matjson::Value const params = msg.contains("params") && msg["params"].isObject()
        ? msg["params"] : matjson::makeObject({});

    if (method == "notifications/initialized" || method == "notifications/cancelled" ||
        method.starts_with("notifications/")) {
        return false; // JSON-RPC notification -> no response object
    }

    if (method == "initialize") {
        std::string requestedVer = params["protocolVersion"].asString().unwrapOr("2025-03-26");
        if (requestedVer != "2024-11-05" && requestedVer != "2025-03-26") {
            requestedVer = "2025-03-26";
        }
        auto fullInstructions = std::string(kServerInstructions) + "\n\n" + buildTriggerCatalogPromptBlock();
        auto res = matjson::makeObject({
            {"protocolVersion", requestedVer},
            {"capabilities", matjson::makeObject({
                {"tools", matjson::makeObject({{"listChanged", false}})},
                {"resources", matjson::makeObject({{"subscribe", false}, {"listChanged", false}})},
                {"prompts", matjson::makeObject({{"listChanged", false}})},
            })},
            {"serverInfo", matjson::makeObject({
                {"name", "geometry-dash-editor-mcp"},
                {"title", "Geometry Dash Editor MCP (Paimbnails)"},
                {"version", "1.0.0"},
            })},
            {"instructions", std::move(fullInstructions)},
        });
        outResponse = rpcOk(std::move(id), std::move(res));
        return true;
    }

    if (method == "ping") {
        outResponse = rpcOk(std::move(id), matjson::makeObject({}));
        return true;
    }

    if (method == "tools/list") {
        outResponse = rpcOk(std::move(id), buildToolsListResult());
        return true;
    }

    if (method == "tools/call") {
        if (!params.contains("name") || !params["name"].isString()) {
            outResponse = rpcErr(std::move(id), -32602, "Invalid params: missing tool name");
            return true;
        }
        std::string const toolName = params["name"].asString().unwrapOr("");
        matjson::Value const toolArgs = params.contains("arguments") && params["arguments"].isObject()
            ? params["arguments"] : matjson::makeObject({});
        outResponse = rpcOk(std::move(id), invokeMcpTool(toolName, toolArgs));
        return true;
    }

    if (method == "resources/list") {
        outResponse = rpcOk(std::move(id), buildResourcesListResult());
        return true;
    }

    if (method == "resources/read") {
        std::string const uri = params["uri"].asString().unwrapOr("");
        if (uri.empty()) {
            outResponse = rpcErr(std::move(id), -32602, "Invalid params: missing resource uri");
            return true;
        }
        outResponse = rpcOk(std::move(id), readMcpResource(uri));
        return true;
    }

    if (method == "prompts/list") {
        outResponse = rpcOk(std::move(id), buildPromptsListResult());
        return true;
    }

    if (method == "prompts/get") {
        std::string const promptName = params["name"].asString().unwrapOr("");
        if (promptName.empty()) {
            outResponse = rpcErr(std::move(id), -32602, "Invalid params: missing prompt name");
            return true;
        }
        matjson::Value const pArgs = params.contains("arguments") && params["arguments"].isObject()
            ? params["arguments"] : matjson::makeObject({});
        outResponse = rpcOk(std::move(id), getMcpPrompt(promptName, pArgs));
        return true;
    }

    if (!hasId) return false;
    outResponse = rpcErr(std::move(id), -32601, "Method not found: " + method);
    return true;
}

} // namespace

JsonRpcDispatchResult dispatchMcpJsonRpc(std::string_view requestBody) {
    auto parsed = matjson::parse(requestBody);
    if (parsed.isErr()) {
        return {
            400,
            rpcErr(nullptr, -32700, "Parse error: invalid JSON").dump(matjson::NO_INDENTATION),
        };
    }

    matjson::Value const& val = parsed.unwrap();
    if (val.isArray()) {
        auto arr = matjson::Value::array();
        for (auto const& item : val) {
            matjson::Value resp;
            if (handleSingleMessage(item, resp)) {
                arr.push(std::move(resp));
            }
        }
        if (arr.size() == 0) {
            return {202, {}};
        }
        return {200, arr.dump(matjson::NO_INDENTATION)};
    }

    matjson::Value resp;
    if (!handleSingleMessage(val, resp)) {
        return {202, {}};
    }
    return {200, resp.dump(matjson::NO_INDENTATION)};
}

} // namespace paimon::editormcp
