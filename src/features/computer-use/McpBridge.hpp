#pragma once

// Cliente JSON-RPC minimo hacia el mod GD MCP (127.0.0.1, puerto ajustable).
// El chat escribe tareas y decisiones; la IA externa las lee y propone.

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

namespace paimon::computeruse {

struct InboxTask {
    unsigned long id = 0;
    std::string text;
    std::string context;
    std::string status;
    std::string proposal;
    std::string result;
};

class McpBridge {
public:
    static McpBridge& get();

    McpBridge(McpBridge const&) = delete;
    McpBridge& operator=(McpBridge const&) = delete;

    using TasksCallback = std::function<void(bool ok, std::vector<InboxTask> tasks, std::string error)>;
    using IdCallback = std::function<void(bool ok, unsigned long id, std::string error)>;
    using OkCallback = std::function<void(bool ok, std::string error)>;

    void pushTask(std::string text, std::string context, IdCallback cb);
    void fetchTasks(TasksCallback cb);
    void decideTask(unsigned long id, bool approved, OkCallback cb);

private:
    McpBridge() = default;

    int port();
    void call(std::string tool, matjson::Value args,
              std::function<void(bool ok, matjson::Value structured, std::string error)> cb);
};

} // namespace paimon::computeruse
