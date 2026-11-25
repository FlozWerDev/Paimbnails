#include "../src/features/editor-mcp/EditorMcpBridge.hpp"
#include "../src/features/editor-mcp/EditorMcpCatalog.hpp"
#include "../src/features/editor-mcp/EditorMcpProtocol.hpp"

#include <matjson.hpp>

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_set>

namespace paimon::editormcp {

matjson::Value invokeMcpTool(std::string_view name, matjson::Value const& args) {
    if (name == "get_gd_reference") {
        auto topic = args["topic"].asString().unwrapOr("all");
        auto filter = args["filter"].asString().unwrapOr("");
        return matjson::makeObject({
            {"content", jsonArray({
                matjson::makeObject({
                    {"type", "text"},
                    {"text", buildReferenceJson(topic, filter).dump(matjson::NO_INDENTATION)},
                }),
            })},
            {"isError", false},
        });
    }
    if (name == "search_object_catalog") {
        auto q = args["query"].asString().unwrapOr("");
        auto cat = args["category"].asString().unwrapOr("");
        int lim = static_cast<int>(args["limit"].asInt().unwrapOr(40));
        return matjson::makeObject({
            {"content", jsonArray({
                matjson::makeObject({
                    {"type", "text"},
                    {"text", searchObjectCatalog(q, cat, lim, false).dump(matjson::NO_INDENTATION)},
                }),
            })},
            {"isError", false},
        });
    }
    return matjson::makeObject({
        {"content", jsonArray({
            matjson::makeObject({{"type", "text"}, {"text", "{\"ok\":true,\"headless\":true}"}}),
        })},
        {"isError", false},
    });
}

} // namespace paimon::editormcp

int main() {
    using namespace paimon::editormcp;

    // 1. Trigger catalog, uniqueness & alias resolution
    assert(triggerCatalog().size() >= 60);
    {
        std::unordered_set<int> seenTriggerIds;
        for (auto const& t : triggerCatalog()) {
            assert(seenTriggerIds.insert(t.id).second && "Duplicate trigger ID in triggerCatalog()");
        }
    }
    assert(resolveTriggerId("move") == 901);
    assert(resolveTriggerId("Color") == 899);
    assert(resolveTriggerId("camera_zoom") == 1913);
    assert(resolveTriggerId("shader") == 2904);
    assert(resolveTriggerId("1268") == 1268);
    assert(resolveTriggerId("nonexistent_trigger_xyz") == 0);

    // 2. Reference JSON & Markdown
    auto refAll = buildReferenceJson("all");
    assert(refAll.contains("triggers"));
    assert(refAll.contains("objectProperties"));
    assert(refAll.contains("colorChannels"));
    assert(refAll.contains("zLayers"));
    assert(refAll.contains("easings"));
    assert(refAll.contains("editorOptions"));
    assert(refAll["triggers"].size() == triggerCatalog().size());

    auto md = buildReferenceMarkdown("triggers");
    assert(md.find("901") != std::string::npos);
    assert(md.find("Move") != std::string::npos);

    // 3. Headless object catalog search
    auto glowSearch = searchObjectCatalog("glow", "", 20, false);
    assert(glowSearch["count"].asInt().unwrapOr(0) >= 3);

    // 4. MCP JSON-RPC initialize
    {
        auto res = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})");
        assert(res.httpStatus == 200);
        auto parsed = matjson::parse(res.body);
        assert(parsed.isOk());
        auto& v = parsed.unwrap();
        assert(v["id"].asInt().unwrapOr(0) == 1);
        assert(v["result"]["protocolVersion"].asString().unwrapOr("") == "2025-03-26");
        assert(v["result"]["capabilities"].contains("tools"));
        assert(v["result"]["capabilities"].contains("resources"));
        assert(v["result"]["capabilities"].contains("prompts"));
        auto instr = v["result"]["instructions"].asString().unwrapOr("");
        assert(instr.find("do NOT add unrequested backgrounds") != std::string::npos);
        assert(instr.find("Zero scattered or orphaned objects") != std::string::npos);
        assert(instr.find("Mandatory Visual QA") != std::string::npos);
        assert(instr.find("901 | Move") != std::string::npos);
        assert(instr.find("28=moveX") != std::string::npos);
        assert(instr.find("3619 | Item Edit") != std::string::npos);
        assert(instr.find("copy_color_id->50") != std::string::npos);
        assert(instr.find("use Color 899") == std::string::npos);
    }

    // 5. Notification returns 202 empty
    {
        auto res = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        assert(res.httpStatus == 202);
        assert(res.body.empty());
    }

    // 6. tools/list lists all 29 tools & upgraded schemas
    {
        auto res = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
        assert(res.httpStatus == 200);
        auto parsed = matjson::parse(res.body).unwrap();
        auto const& tools = parsed["result"]["tools"];
        assert(tools.size() == 29);

        bool checkedRender = false;
        bool checkedColor = false;
        bool checkedDelete = false;
        bool checkedGetObjects = false;
        for (auto const& t : tools) {
            auto tname = t["name"].asString().unwrapOr("");
            auto const& props = t["inputSchema"]["properties"];
            if (tname == "render_1to1") {
                assert(props["mode"].contains("enum"));
                assert(props.contains("show_coords"));
                assert(props.contains("show_hitboxes"));
                assert(props.contains("highlight_group"));
                assert(props.contains("world_coords"));
                checkedRender = true;
            } else if (tname == "set_color_channel") {
                assert(props.contains("channels"));
                checkedColor = true;
            } else if (tname == "delete_objects") {
                assert(props.contains("world_coords"));
                checkedDelete = true;
            } else if (tname == "get_objects") {
                assert(props.contains("compact"));
                assert(props.contains("world_coords"));
                checkedGetObjects = true;
            }
        }
        assert(checkedRender && checkedColor && checkedDelete && checkedGetObjects);
    }

    // 7. resources/list & resources/read
    {
        auto res = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":3,"method":"resources/list"})");
        assert(res.httpStatus == 200);
        auto parsed = matjson::parse(res.body).unwrap();
        assert(parsed["result"]["resources"].size() == 6);

        auto readRes = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"gd://reference/triggers"}})");
        assert(readRes.httpStatus == 200);
        auto readParsed = matjson::parse(readRes.body).unwrap();
        assert(readParsed["result"]["contents"].size() == 1);

        auto geoReadRes = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":41,"method":"resources/read","params":{"uri":"gd://reference/geometry-cheatsheet"}})");
        assert(geoReadRes.httpStatus == 200);
        auto geoReadParsed = matjson::parse(geoReadRes.body).unwrap();
        assert(geoReadParsed["result"]["contents"].size() == 1);
        auto geoMd = geoReadParsed["result"]["contents"][0]["text"].asString().unwrapOr("");
        assert(geoMd.find("1.414") != std::string::npos);
        assert(geoMd.find("2.236") != std::string::npos);
    }

    // 8. tools/call get_gd_reference (pure, no game loop needed)
    {
        auto res = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"get_gd_reference","arguments":{"topic":"triggers","filter":"move"}}})");
        assert(res.httpStatus == 200);
        auto parsed = matjson::parse(res.body).unwrap();
        assert(!parsed["result"]["isError"].asBool().unwrapOr(true));
        auto text = parsed["result"]["content"][0]["text"].asString().unwrapOr("");
        assert(text.find("901") != std::string::npos);

        auto decoRes = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"get_gd_reference","arguments":{"topic":"decoration"}}})");
        assert(decoRes.httpStatus == 200);
        auto decoText = matjson::parse(decoRes.body).unwrap()["result"]["content"][0]["text"].asString().unwrapOr("");
        assert(decoText.find("STRICT SCOPE") != std::string::npos);
        assert(decoText.find("NO SCATTERED OBJECTS") != std::string::npos);

        auto geoRes = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":61,"method":"tools/call","params":{"name":"get_gd_reference","arguments":{"topic":"geometry"}}})");
        assert(geoRes.httpStatus == 200);
        auto geoText = matjson::parse(geoRes.body).unwrap()["result"]["content"][0]["text"].asString().unwrapOr("");
        assert(geoText.find("geometryCheatsheet") != std::string::npos);

        auto promptsRes = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":7,"method":"prompts/list"})");
        assert(promptsRes.httpStatus == 200);
        auto promptsJson = matjson::parse(promptsRes.body).unwrap();
        assert(promptsJson["result"]["prompts"].size() == 6);

        auto trigPromptRes = dispatchMcpJsonRpc(R"({"jsonrpc":"2.0","id":8,"method":"prompts/get","params":{"name":"create_trigger_system","arguments":{"goal":"custom trigger system"}}})");
        assert(trigPromptRes.httpStatus == 200);
        auto trigPromptText = matjson::parse(trigPromptRes.body).unwrap()["result"]["messages"][0]["content"]["text"].asString().unwrapOr("");
        assert(trigPromptText.find("901 | Move") != std::string::npos);
        assert(trigPromptText.find("3602 | SFX") != std::string::npos);
        assert(trigPromptText.find("move_x->28") != std::string::npos);
        assert(trigPromptText.find("Easing codes (key 30)") != std::string::npos);
    }

    // 9. Batch JSON-RPC & invalid JSON
    {
        auto res = dispatchMcpJsonRpc(R"([{"jsonrpc":"2.0","id":10,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/initialized"}])");
        assert(res.httpStatus == 200);
        auto parsed = matjson::parse(res.body).unwrap();
        assert(parsed.isArray());
        assert(parsed.size() == 1);

        auto bad = dispatchMcpJsonRpc("{not valid json");
        assert(bad.httpStatus == 400);
    }

    std::cout << "Editor MCP protocol & catalog checks passed ("
              << triggerCatalog().size() << " triggers, 29 MCP tools).\n";
    return 0;
}
