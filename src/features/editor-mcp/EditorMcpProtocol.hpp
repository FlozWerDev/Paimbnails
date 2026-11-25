#pragma once

#include <matjson.hpp>
#include <string>
#include <string_view>

namespace paimon::editormcp {

struct JsonRpcDispatchResult {
    int httpStatus = 200;
    std::string body; // empty for pure notifications (HTTP 202)
};

// Pure JSON-RPC 2.0 router for MCP methods.
JsonRpcDispatchResult dispatchMcpJsonRpc(std::string_view requestBody);

} // namespace paimon::editormcp
