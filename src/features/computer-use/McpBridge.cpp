#include "McpBridge.hpp"

#include <Geode/loader/Mod.hpp>

#include "../../utils/WebHelper.hpp"

using namespace geode::prelude;

namespace paimon::computeruse {

namespace {

int s_rpcId = 0;

bool structuredError(matjson::Value const& sc, std::string& error) {
    auto err = sc["error"].asString().unwrapOr("");
    if (err.empty()) return false;
    error = err;
    return true;
}

} // namespace

McpBridge& McpBridge::get() {
    static McpBridge bridge;
    return bridge;
}

int McpBridge::port() {
    auto port = Mod::get()->getSettingValue<int64_t>("computeruse-mcp-port");
    if (port < 1024 || port > 65535) return 38475;
    return static_cast<int>(port);
}

void McpBridge::call(std::string tool, matjson::Value args,
                     std::function<void(bool, matjson::Value, std::string)> cb) {
    auto rpc = matjson::Value::object();
    rpc["jsonrpc"] = "2.0";
    rpc["id"] = ++s_rpcId;
    rpc["method"] = "tools/call";
    auto params = matjson::Value::object();
    params["name"] = tool;
    params["arguments"] = std::move(args);
    rpc["params"] = std::move(params);

    auto request = web::WebRequest();
    request.timeout(std::chrono::seconds(8));
    request.userAgent("");
    request.header("Content-Type", "application/json");
    request.bodyString(rpc.dump(matjson::NO_INDENTATION));

    auto url = "http://127.0.0.1:" + std::to_string(port()) + "/mcp";
    WebHelper::dispatch(
        std::move(request), "POST", url,
        [cb = std::move(cb)](web::WebResponse response) mutable {
            if (!response.ok()) {
                cb(false, matjson::Value::object(), "Sin conexion con el mod MCP en 127.0.0.1. Revisa puerto y ajustes.");
                return;
            }
            auto parsed = matjson::Value::parse(response.string().unwrapOr(""));
            if (parsed.isErr()) {
                cb(false, matjson::Value::object(), "Respuesta MCP ilegible.");
                return;
            }
            auto body = std::move(parsed).unwrap();
            auto sc = body["result"]["structuredContent"];
            if (sc.isNull()) {
                cb(false, matjson::Value::object(), "El MCP devolvio error.");
                return;
            }
            cb(true, std::move(sc), "");
        }
    );
}

void McpBridge::pushTask(std::string text, std::string context, IdCallback cb) {
    auto args = matjson::Value::object();
    args["text"] = text;
    args["context"] = context;
    call("gd_agent_push_task", std::move(args),
        [cb = std::move(cb)](bool ok, matjson::Value sc, std::string error) mutable {
            if (!ok) {
                cb(false, 0, error);
                return;
            }
            std::string toolError;
            if (structuredError(sc, toolError)) {
                cb(false, 0, toolError);
                return;
            }
            cb(true, static_cast<unsigned long>(sc["id"].as<int>().unwrapOr(0)), "");
        }
    );
}

void McpBridge::fetchTasks(TasksCallback cb) {
    call("gd_agent_tasks", matjson::Value::object(),
        [cb = std::move(cb)](bool ok, matjson::Value sc, std::string error) mutable {
            if (!ok) {
                cb(false, {}, error);
                return;
            }
            std::vector<InboxTask> tasks;
            auto arr = sc["tasks"];
            for (std::size_t i = 0; i < arr.size(); ++i) {
                InboxTask task;
                task.id = static_cast<unsigned long>(arr[i]["id"].as<int>().unwrapOr(0));
                task.text = arr[i]["text"].asString().unwrapOr("");
                task.context = arr[i]["context"].asString().unwrapOr("");
                task.status = arr[i]["status"].asString().unwrapOr("");
                task.proposal = arr[i]["proposal"].asString().unwrapOr("");
                task.result = arr[i]["result"].asString().unwrapOr("");
                if (task.id > 0) tasks.push_back(std::move(task));
            }
            cb(true, std::move(tasks), "");
        }
    );
}

void McpBridge::decideTask(unsigned long id, bool approved, OkCallback cb) {
    auto args = matjson::Value::object();
    args["id"] = static_cast<int>(id);
    args["approved"] = approved;
    args["note"] = "";
    call("gd_agent_decide", std::move(args),
        [cb = std::move(cb)](bool ok, matjson::Value sc, std::string error) mutable {
            if (!ok) {
                cb(false, error);
                return;
            }
            std::string toolError;
            if (structuredError(sc, toolError)) {
                cb(false, toolError);
                return;
            }
            cb(true, "");
        }
    );
}

} // namespace paimon::computeruse
