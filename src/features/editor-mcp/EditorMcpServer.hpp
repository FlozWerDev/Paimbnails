#pragma once

#include "EditorMcpTypes.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace paimon::editormcp {

class EditorMcpServer final {
public:
    static EditorMcpServer& get();
    static bool isInitialized();

    void init();
    void requestStop();
    void waitShutdown(std::chrono::steady_clock::time_point deadline);
    void shutdown();
    void restart();

    bool isRunning() const;
    bool supported() const;
    uint16_t port() const;
    std::string mcpUrl() const;
    std::string renderUrl() const;
    ServerStats stats() const;

    void recordToolCall(std::string_view toolName);
    void recordRenderServed();
    void recordObjectsCreated(uint64_t count);

private:
    EditorMcpServer();
    ~EditorMcpServer();

    void start();
    void stop();

    struct Impl;
    std::shared_ptr<Impl> m_impl;
    bool m_initialized = false;
};

} // namespace paimon::editormcp
