#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <vector>
#include <memory>

class ListThumbnailManager {
public:
    static ListThumbnailManager& get();

    // callback: levelid, textura — Geode v5: CopyableFunction
    using ListCallback = geode::CopyableFunction<void(int, cocos2d::CCTexture2D*)>;

    void processList(std::vector<int> const& levelIDs, ListCallback callback, std::shared_ptr<bool> callerAlive = nullptr);

private:
    ListThumbnailManager() = default;
};
