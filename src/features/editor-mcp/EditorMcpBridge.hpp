#pragma once

#include "EditorMcpTypes.hpp"
#include <matjson.hpp>
#include <string>
#include <string_view>

namespace paimon::editormcp {

// MCP JSON schema declarations
matjson::Value buildToolsListResult();
matjson::Value buildResourcesListResult();
matjson::Value buildPromptsListResult();
matjson::Value readMcpResource(std::string_view uri);
matjson::Value getMcpPrompt(std::string_view name, matjson::Value const& args);

// Dispatches an MCP tools/call safely to the main thread when needed and updates stats.
matjson::Value invokeMcpTool(std::string_view name, matjson::Value const& args);

// Direct main-thread helper for GET /render.png or UI test preview.
RenderResult capturePreviewMainThread(RenderRequest const& req);

} // namespace paimon::editormcp
