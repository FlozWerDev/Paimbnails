#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace paimon::feedback {

// live-screen snapshot used as the feedback canvas. rgba is top-down.
struct FeedbackShot {
    geode::Ref<cocos2d::CCTexture2D> texture;
    std::shared_ptr<uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool valid = false;
};

// main thread + live gl context only.
FeedbackShot captureScreen();

struct FeedbackMeta {
    std::string username;
    int accountID = 0;
    std::string levelName;
    int levelID = 0;
    std::string scene;
};

using SubmitCallback = geode::CopyableFunction<void(bool ok, std::string const& message)>;

// player + location info attached to a report.
FeedbackMeta collectCurrentMeta();

// posts to the worker; on transport/server failure the report is kept in
// <save>/feedback_outbox so nothing the player wrote is lost.
void submit(
    std::string const& message,
    std::vector<uint8_t> const& annotatedPng,
    bool hasImage,
    FeedbackMeta const& meta,
    SubmitCallback callback
);

std::filesystem::path outboxDir();

} // namespace paimon::feedback
