#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <memory>

class ListThumbnailManager {
public:
    static ListThumbnailManager& get();

    using ListCallback = geode::CopyableFunction<void(int, cocos2d::CCTexture2D*)>;

    void processList(std::vector<int> const& levelIDs, ListCallback callback, std::shared_ptr<bool> callerAlive = nullptr);

private:
    ListThumbnailManager() = default;
};
