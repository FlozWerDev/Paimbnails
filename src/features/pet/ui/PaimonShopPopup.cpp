#include "PaimonShopPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/PetManager.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../moderation/services/ModeratorUtils.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;

PaimonShopPopup* PaimonShopPopup::create() {
    auto ret = new PaimonShopPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonShopPopup::init() {
    if (!Popup::init(300.f, 240.f)) return false;

    this->setTitle("Paimon Pet Shop");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // info button
    auto infoMenu = CCMenu::create();
    infoMenu->setPosition({0, 0});
    m_mainLayer->addChild(infoMenu, 15);

    auto iBtn = PaimonInfo::createInfoBtn("Paimon Pet Shop",
        "Browse and download community-made pet images!\n\n"
        "Each pet shows its <cy>name</c>, <cy>creator</c>, and <cy>file size</c>.\n"
        "Tap <cg>Download</c> to add it to your Pet Gallery.\n\n"
        "Pets are uploaded by <cr>moderators</c>.", this);
    if (iBtn) {
        iBtn->setPosition({content.width - 20.f, content.height - 20.f});
        infoMenu->addChild(iBtn);
    }

    // admin upload button (only visible to moderators)
    if (PaimonUtils::isUserModerator()) {
        auto uploadSpr = ButtonSprite::create("Upload", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        uploadSpr->setScale(0.6f);
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            uploadSpr, this, menu_selector(PaimonShopPopup::onUploadPet));
        uploadBtn->setPosition({55.f, content.height - 20.f});
        infoMenu->addChild(uploadBtn);
    }

    // status label
    m_statusLabel = CCLabelBMFont::create("Loading shop...", "chatFont.fnt");
    m_statusLabel->setScale(0.6f);
    m_statusLabel->setColor({200, 200, 200});
    m_statusLabel->setPosition({cx, content.height / 2.f});
    m_mainLayer->addChild(m_statusLabel, 5);

    // scroll area
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 50.f;
    m_scrollLayer = ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_scrollLayer->setVisible(false);
    m_mainLayer->addChild(m_scrollLayer, 5);

    fetchShopList();
    paimon::markDynamicPopup(this);
    return true;
}

void PaimonShopPopup::fetchShopList() {
    WeakRef<PaimonShopPopup> self = this;
    HttpClient::get().getPetShopList([self](bool success, std::string const& response) {
        auto popup = self.lock();
        if (!popup) return;

        if (!success) {
            if (popup->m_statusLabel) popup->m_statusLabel->setString("Failed to load shop");
            return;
        }

        auto res = matjson::parse(response);
        if (!res.isOk()) {
            if (popup->m_statusLabel) popup->m_statusLabel->setString("Invalid response");
            return;
        }
        auto json = res.unwrap();

        popup->m_items.clear();
        matjson::Value arr;
        if (json.contains("items") && json["items"].isArray()) {
            arr = json["items"];
        } else if (json.isArray()) {
            arr = json;
        } else {
            if (popup->m_statusLabel) popup->m_statusLabel->setString("No pets available yet");
            return;
        }

        auto arrRes = arr.asArray();
        if (!arrRes.isOk()) {
            if (popup->m_statusLabel) popup->m_statusLabel->setString("Error parsing shop data");
            return;
        }

        for (auto& item : arrRes.unwrap()) {
            PetShopItem p;
            p.id = item["id"].asString().unwrapOr("");
            p.name = item["name"].asString().unwrapOr("Unknown");
            p.creator = item["creator"].asString().unwrapOr("Unknown");
            p.format = item["format"].asString().unwrapOr("png");
            p.fileSize = static_cast<int>(item["fileSize"].asInt().unwrapOr(0));
            if (!p.id.empty()) popup->m_items.push_back(p);
        }

        if (popup->m_items.empty()) {
            if (popup->m_statusLabel) popup->m_statusLabel->setString("No pets available yet");
        } else {
            if (popup->m_statusLabel) popup->m_statusLabel->setVisible(false);
            popup->buildList();
        }
    });
}

void PaimonShopPopup::buildList() {
    if (!m_scrollLayer) return;

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 16.f;
    float rowH = 50.f;
    float totalH = std::max(rowH * (float)m_items.size() + 10.f,
                            m_scrollLayer->getContentSize().height);

    auto* sc = m_scrollLayer->m_contentLayer;
    sc->removeAllChildren();
    sc->setContentSize({scrollW, totalH});

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({scrollW, totalH});
    sc->addChild(menu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 30.f;

    for (auto& item : m_items) {
        // background stripe
        auto stripe = CCLayerColor::create({0, 0, 0, 40});
        stripe->setContentSize({scrollW - 4.f, rowH - 4.f});
        stripe->setPosition({2.f, y - rowH / 2.f + 2.f});
        sc->addChild(stripe);

        // name
        auto nameLbl = CCLabelBMFont::create(item.name.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.3f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({10.f, y + 8.f});
        sc->addChild(nameLbl);

        // creator + size
        std::string meta = "by " + item.creator + " | " + formatFileSize(item.fileSize) + " | " + item.format;
        auto metaLbl = CCLabelBMFont::create(meta.c_str(), "chatFont.fnt");
        metaLbl->setScale(0.45f);
        metaLbl->setColor({180, 180, 180});
        metaLbl->setAnchorPoint({0.f, 0.5f});
        metaLbl->setPosition({10.f, y - 10.f});
        sc->addChild(metaLbl);

        // download button or "Downloaded" label
        std::string filename = item.id + "." + item.format;
        if (isAlreadyInGallery(filename)) {
            auto checkLbl = CCLabelBMFont::create("Downloaded", "bigFont.fnt");
            checkLbl->setScale(0.22f);
            checkLbl->setColor({100, 255, 100});
            checkLbl->setPosition({scrollW - 45.f, y});
            sc->addChild(checkLbl);
        } else {
            auto dlSpr = ButtonSprite::create("Get", "bigFont.fnt", "GJ_button_01.png", 0.6f);
            dlSpr->setScale(0.55f);
            auto dlBtn = CCMenuItemSpriteExtra::create(
                dlSpr, this, menu_selector(PaimonShopPopup::onDownload));
            dlBtn->setPosition({scrollW - 45.f, y});
            // store item id + format in user object
            auto data = CCString::createWithFormat("%s|%s|%s", item.id.c_str(), item.format.c_str(), item.name.c_str());
            dlBtn->setUserObject(data);
            menu->addChild(dlBtn);
        }

        y -= rowH;
    }

    m_scrollLayer->setVisible(true);
    m_scrollLayer->moveToTop();
}

void PaimonShopPopup::onDownload(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto dataStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!dataStr) return;

    std::string raw = dataStr->getCString();
    // parse "id|format|name"
    auto pos1 = raw.find('|');
    auto pos2 = raw.find('|', pos1 + 1);
    if (pos1 == std::string::npos || pos2 == std::string::npos) return;

    std::string itemId = raw.substr(0, pos1);
    std::string format = raw.substr(pos1 + 1, pos2 - pos1 - 1);
    std::string name = raw.substr(pos2 + 1);

    if (m_downloading.count(itemId)) return; // already downloading
    m_downloading.insert(itemId);

    PaimonNotify::create("Downloading " + name + "...", NotificationIcon::Info)->show();

        WeakRef<PaimonShopPopup> self = this;
    HttpClient::get().downloadPetShopItem(itemId, format,
        [self, itemId, format, name](bool success, std::vector<uint8_t> const& data) {
            auto popup = self.lock();
            if (!popup) return;

            popup->m_downloading.erase(itemId);

            if (!success || data.empty()) {
                log::error("[PetShop] Download failed for '{}' (success={}, dataSize={})", name, success, data.size());
                PaimonNotify::create("Failed to download " + name, NotificationIcon::Error)->show();
                return;
            }

            log::info("[PetShop] Downloaded '{}': {} bytes", name, data.size());

            // save to pet gallery
            std::string filename = itemId + "." + format;
            auto path = PetManager::get().galleryDir() / filename;
            {
                std::error_code ec;
                std::filesystem::create_directories(PetManager::get().galleryDir(), ec);
                std::ofstream f(path, std::ios::binary);
                if (!f.is_open()) {
                    log::error("[PetShop] Could not open file for writing: {}", geode::utils::string::pathToString(path));
                    PaimonNotify::create("Failed to save " + name, NotificationIcon::Error)->show();
                    return;
                }
                f.write(reinterpret_cast<char const*>(data.data()), data.size());
                f.close();

                // verify write
                std::error_code verifyEc;
                if (f.fail() || !std::filesystem::exists(path, verifyEc) || std::filesystem::file_size(path, verifyEc) != data.size()) {
                    log::error("[PetShop] File write verification failed for: {}", geode::utils::string::pathToString(path));
                    std::filesystem::remove(path, verifyEc);
                    PaimonNotify::create("Failed to save " + name, NotificationIcon::Error)->show();
                    return;
                }

                log::info("[PetShop] Saved '{}' to gallery: {}", name, geode::utils::string::pathToString(path));

                PaimonNotify::create(name + " added to gallery!", NotificationIcon::Success)->show();

                // auto-select if no pet selected
                if (PetManager::get().config().selectedImage.empty()) {
                    PetManager::get().setImage(filename);
                }
            }

            // refresh list so button changes to "Downloaded"
            popup->fetchShopList();
        });
}

void PaimonShopPopup::onUploadPet(CCObject*) {
    WeakRef<PaimonShopPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto filepath = std::filesystem::path(*pathOpt);
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec)) return;

        // detect format
        auto ext = geode::utils::string::pathToString(filepath.extension());
        for (auto& c : ext) c = (char)std::tolower(c);
        std::string format = "png";
        if (ImageLoadHelper::isAnimatedImage(filepath)) {
            format = "gif";
        }

        std::vector<uint8_t> data;
        if (format == "gif") {
            std::ifstream f(filepath, std::ios::binary);
            if (!f) {
                PaimonNotify::create("Failed to read file", NotificationIcon::Error)->show();
                return;
            }
            data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            f.close();
        } else {
            auto loaded = ImageLoadHelper::loadStaticImage(filepath, 16);
            if (!loaded.success || !loaded.texture || !loaded.buffer || loaded.width <= 0 || loaded.height <= 0) {
                PaimonNotify::create("Failed to convert image to PNG", NotificationIcon::Error)->show();
                return;
            }

            if (!ImageConverter::rgbaToPngBuffer(loaded.buffer.get(),
                    static_cast<uint32_t>(loaded.width),
                    static_cast<uint32_t>(loaded.height),
                    data)) {
                loaded.texture->release();
                PaimonNotify::create("Failed to convert image to PNG", NotificationIcon::Error)->show();
                return;
            }
            loaded.texture->release();
        }

        if (data.empty()) {
            PaimonNotify::create("Empty file", NotificationIcon::Error)->show();
            return;
        }

        // ask for pet name with a simple alert input approach
        // use the filename (without extension) as default name
        std::string defaultName = geode::utils::string::pathToString(filepath.stem());

        auto* accountManager = GJAccountManager::get();
        if (!accountManager) {
            PaimonNotify::create("Account manager unavailable", NotificationIcon::Error)->show();
            return;
        }

        std::string creator = accountManager->m_username;
        if (creator.empty()) {
            PaimonNotify::create("You must be logged in to upload pets", NotificationIcon::Error)->show();
            return;
        }

        PaimonNotify::create("Uploading " + defaultName + "...", NotificationIcon::Info)->show();

        HttpClient::get().uploadPetShopItem(defaultName, creator, data, format,
            [self, defaultName](bool success, std::string const& response) {
                auto popup = self.lock();
                if (success) {
                    PaimonNotify::create(defaultName + " uploaded to shop!", NotificationIcon::Success)->show();
                    if (popup) {
                        popup->fetchShopList();
                    }
                } else {
                    PaimonNotify::create("Upload failed: " + response, NotificationIcon::Error)->show();
                }
            }
        );
    });
}

bool PaimonShopPopup::isAlreadyInGallery(std::string const& filename) const {
    auto path = PetManager::get().galleryDir() / filename;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string PaimonShopPopup::formatFileSize(int bytes) const {
    if (bytes < 1024) return fmt::format("{} B", bytes);
    if (bytes < 1024 * 1024) return fmt::format("{:.1f} KB", bytes / 1024.f);
    return fmt::format("{:.1f} MB", bytes / (1024.f * 1024.f));
}
