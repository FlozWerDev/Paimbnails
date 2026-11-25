#pragma once

#include <matjson.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace paimon::editormcp {

inline matjson::Value jsonArray(std::initializer_list<matjson::Value> items) {
    auto arr = matjson::Value::array();
    for (auto const& item : items) arr.push(item);
    return arr;
}

inline matjson::Value strArray(std::initializer_list<char const*> items) {
    auto arr = matjson::Value::array();
    for (char const* item : items) arr.push(item);
    return arr;
}

struct ServerStats {
    bool running = false;
    bool supported = true;
    uint16_t port = 21690;
    std::string status = "Stopped";
    uint64_t totalRequests = 0;
    uint64_t toolCalls = 0;
    uint64_t rendersServed = 0;
    uint64_t objectsCreated = 0;
    std::string lastTool;
    std::string lastError;
};

struct RenderRequest {
    std::string mode = "1to1"; // "1to1", "viewport", "overview"
    bool hasCenter = false;
    float centerX = 0.f;
    float centerY = 105.f;
    bool worldCoords = true;
    float zoom = 1.f;
    int width = 1280;
    int height = 720;
    bool showGrid = false;
    bool showCoords = false;
    bool showHitboxes = false;
    int highlightGroup = 0;
    bool showUI = false;
    bool showTriggerBoxes = false;
    bool previewColors = true;
    float boundsMinX = 0.f;
    float boundsMinY = 0.f;
    float boundsMaxX = 0.f;
    float boundsMaxY = 0.f;
    bool hasBounds = false;
    std::string savePath;
};

struct RenderResult {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> pngBytes;
    std::string pngBase64;
    int width = 0;
    int height = 0;
    float centerX = 0.f;
    float centerY = 0.f;
    float zoom = 1.f;
    float worldMinX = 0.f;
    float worldMaxX = 0.f;
    float worldMinY = 0.f;
    float worldMaxY = 0.f;
    float pixelsPerUnit = 1.f;
    int visibleObjectCount = 0;
    int totalObjectCount = 0;
    std::string savedPath;
};

} // namespace paimon::editormcp
