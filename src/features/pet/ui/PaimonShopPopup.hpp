#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

struct PetShopItem {
    std::string id;
    std::string name;
    std::string creator;
    std::string format; // "png", "gif"
    int fileSize = 0;   // bytes
};

class PaimonShopPopup : public geode::Popup {
protected:
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::vector<PetShopItem> m_items;
    std::set<std::string> m_downloading; // ids currently downloading

    bool init();

    void fetchShopList();
    void buildList();
    void onDownload(cocos2d::CCObject* sender);
    void onUploadPet(cocos2d::CCObject* sender);

    bool isAlreadyInGallery(std::string const& filename) const;
    std::string formatFileSize(int bytes) const;

public:
    static PaimonShopPopup* create();
};

