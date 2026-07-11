#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;
namespace fs = std::filesystem;

namespace {

bool qsOn() { return moduleEnabled("editor-mod-quick-save"); }
bool asOn() { return moduleEnabled("editor-mod-auto-save"); }
bool bkOn() { return moduleEnabled("editor-mod-backups"); }

std::string levelKey(GJGameLevel* level) {
    if (!level) return "unknown";
    int lid = level->m_levelID.value();
    if (lid > 0) return std::to_string(lid);
    // Local levels: use name + assigned seed-ish
    std::string name = level->m_levelName;
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    }
    if (name.empty()) name = "unnamed";
    return fmt::format("local_{}_{}", name, level->m_M_ID);
}

fs::path backupRoot() {
    return Mod::get()->getSaveDir() / "editor-backups";
}

fs::path levelDir(GJGameLevel* level) {
    return backupRoot() / levelKey(level);
}

std::string timestampName() {
    auto t = std::chrono::system_clock::now().time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
    return fmt::format("{}.txt", millis);
}

bool writeBackup(LevelEditorLayer* lel, char const* tag) {
    if (!lel || !lel->m_level) return false;
    auto* level = lel->m_level;
    // Serialize the live editor directly. Running EditorPauseLayer::saveLevel
    // here would persist an autosave and break "exit without saving".
    std::string body = lel->getLevelString();
    if (body.empty()) return false;

    auto dir = levelDir(level);
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto path = dir / fmt::format("{}_{}", tag, timestampName());
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "# paimon-backup\n";
    out << "# name=" << std::string(level->m_levelName) << "\n";
    out << "# tag=" << tag << "\n";
    out << body;
    out.flush();
    return out.good();
}

fs::file_time_type safeWriteTime(fs::path const& path) {
    std::error_code ec;
    auto value = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type::min() : value;
}

std::vector<fs::path> backupFiles(GJGameLevel* level) {
    auto dir = levelDir(level);
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) return files;

    fs::directory_iterator it(dir, ec);
    fs::directory_iterator end;
    while (!ec && it != end) {
        std::error_code typeError;
        if (it->is_regular_file(typeError) && !typeError) files.push_back(it->path());
        it.increment(ec);
    }
    std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) {
        return safeWriteTime(a) > safeWriteTime(b);
    });
    return files;
}

void pruneOld(GJGameLevel* level, size_t keep = 8) {
    auto files = backupFiles(level);
    if (files.size() <= keep) return;
    for (size_t i = keep; i < files.size(); ++i) {
        std::error_code ec;
        fs::remove(files[i], ec);
    }
}

} // namespace

// --- Quick save button + autosave tick on EditorUI ---
class $modify(PaimonBackupEditorUI, EditorUI) {
    struct Fields {
        int autosaveSeconds = 0;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onQuickSave(CCObject*) {
        if (!qsOn() || !m_editorLayer) return;
        if (writeBackup(m_editorLayer, "quick")) {
            pruneOld(m_editorLayer->m_level, 12);
            Notification::create("Quick save OK", NotificationIcon::Success)->show();
        } else {
            Notification::create("Quick save failed", NotificationIcon::Error)->show();
        }
    }

    void autosaveTick(float) {
        if (!asOn() || !m_editorLayer) return;
        if (m_editorLayer->m_playbackMode == PlaybackMode::Playing) return;
        int interval = static_cast<int>(moduleSetting<int64_t>("editor-mod-auto-save-minutes", 5)) * 60;
        if (interval < 60) interval = 60;
        m_fields->autosaveSeconds += 1;
        if (m_fields->autosaveSeconds < interval) return;
        m_fields->autosaveSeconds = 0;
        if (writeBackup(m_editorLayer, "auto")) {
            pruneOld(m_editorLayer->m_level, 8);
            Notification::create("Auto-saved backup", NotificationIcon::Info, 1.2f)->show();
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        m_fields->autosaveSeconds = 0;

        if (qsOn() || asOn()) {
            auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("undo-menu"));
            if (menu && qsOn()) {
                // Custom: paim_quick-save.png  |  Fallback: download btn
                auto* spr = circleIcon(
                    files::quickSave, { "GJ_downloadBtn_001.png" },
                    0.85f, CircleBaseColor::Green, CircleBaseSize::Small
                );
                if (spr) {
                    auto* btn = CCMenuItemSpriteExtra::create(
                        spr, this, menu_selector(PaimonBackupEditorUI::onQuickSave)
                    );
                    btn->setID("paimbnails/quick-save-btn");
                    normalizeToolbarItem(btn);
                    menu->addChild(btn);
                    menu->updateLayout();
                }
            }
            if (asOn()) {
                this->schedule(schedule_selector(PaimonBackupEditorUI::autosaveTick), 1.f);
            }
        }
        return true;
    }
};

// --- Backups button in pause ---
class $modify(PaimonBackupPause, EditorPauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    void onOpenBackups(CCObject*);

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorPauseLayer::init(lel)) return false;
        if (!bkOn()) return true;
        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("info-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("settings-menu"));
        if (!menu) return true;
        // Custom: paim_backups.png  |  Fallback: text "Backups"
        auto* btn = iconOrTextButton(
            files::backups, {},
            "Backups", "GJ_button_04.png", 0.38f, CircleBaseColor::Green,
            [this] { this->onOpenBackups(nullptr); }
        );
        if (!btn) return true;
        btn->setID("paimbnails/backups-btn");
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
};

class BackupListLayer : public CCLayer {
public:
    WeakRef<LevelEditorLayer> m_lel;

    static BackupListLayer* create(LevelEditorLayer* lel) {
        auto* r = new BackupListLayer();
        if (r && r->init(lel)) { r->autorelease(); return r; }
        CC_SAFE_DELETE(r);
        return nullptr;
    }

    bool init(LevelEditorLayer* lel) {
        if (!CCLayer::init()) return false;
        m_lel.swap(lel);
        setTouchEnabled(true);
        setKeypadEnabled(true);
        setID("paimbnails/backup-list");

        auto win = CCDirector::get()->getWinSize();
        addChild(CCLayerColor::create({0, 0, 0, 170}, win.width, win.height), -1);

        auto* panel = CCScale9Sprite::create("GJ_square01.png");
        if (!panel) panel = CCScale9Sprite::create("square02_001.png");
        panel->setContentSize({340.f, 260.f});
        panel->setPosition(win / 2.f);
        addChild(panel);

        auto* title = CCLabelBMFont::create("Level Backups", "goldFont.fnt");
        title->setScale(0.55f);
        title->setPosition(win / 2.f + ccp(0.f, 110.f));
        addChild(title);

        auto* scroll = ScrollLayer::create({300.f, 180.f});
        scroll->setPosition(win / 2.f + ccp(-150.f, -95.f));
        scroll->m_contentLayer->setLayout(
            ColumnLayout::create()->setAxisReverse(true)->setAutoScale(false)->setGap(4.f)
                ->setAxisAlignment(AxisAlignment::End)
        );

        auto files = backupFiles(lel ? lel->m_level : nullptr);

        if (files.empty()) {
            scroll->m_contentLayer->addChild(CCLabelBMFont::create("No backups yet", "bigFont.fnt"));
        } else {
            int i = 0;
            for (auto const& p : files) {
                if (i++ > 30) break;
                auto name = p.filename().string();
                auto* row = ButtonSprite::create(name.substr(0, 28).c_str(), "chatFont.fnt", "GJ_button_05.png", 0.45f);
                auto* btn = CCMenuItemSpriteExtra::create(row, this, menu_selector(BackupListLayer::onRestore));
                // Store path in user object via tag index + static map is messy; use node ID
                btn->setID(fmt::format("paimbnails/backup/{}", name));
                // Encode path length-limited in string ID is fragile — keep full path in a map
                m_paths[btn] = p.string();
                auto* menu = CCMenu::create();
                menu->setContentSize({280.f, 28.f});
                menu->addChild(btn);
                menu->updateLayout();
                scroll->m_contentLayer->addChild(menu);
            }
        }
        scroll->m_contentLayer->updateLayout();
        scroll->scrollToTop();
        addChild(scroll);

        auto* menu = CCMenu::create();
        menu->setPosition({0, 0});
        addChild(menu);
        auto* close = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(BackupListLayer::onClose)
        );
        close->setPosition(win / 2.f + ccp(155.f, 115.f));
        menu->addChild(close);
        return true;
    }

    std::unordered_map<CCObject*, std::string> m_paths;

    static std::string parseBackupBody(std::string const& data) {
        std::string body;
        std::istringstream ss(data);
        std::string line;
        bool header = true;
        while (std::getline(ss, line)) {
            if (header && !line.empty() && line[0] == '#') continue;
            header = false;
            if (!body.empty()) body.push_back('\n');
            body += line;
        }
        if (body.empty()) {
            auto pos = data.find("kA");
            if (pos == std::string::npos) pos = data.find("1,");
            body = pos == std::string::npos ? data : data.substr(pos);
        }
        // Level object strings are usually one long line; strip accidental newlines.
        if (body.find(';') != std::string::npos && body.find('\n') != std::string::npos) {
            std::string flat;
            for (char c : body) {
                if (c != '\n' && c != '\r') flat.push_back(c);
            }
            body = std::move(flat);
        }
        return body;
    }

    void onRestore(CCObject* sender) {
        auto it = m_paths.find(sender);
        auto editor = m_lel.lock();
        if (it == m_paths.end() || !editor || !editor->m_level) return;
        std::ifstream in(it->second, std::ios::binary);
        if (!in) {
            Notification::create("Cannot read backup", NotificationIcon::Error)->show();
            return;
        }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string body = parseBackupBody(data);
        if (body.empty()) {
            Notification::create("Backup is empty", NotificationIcon::Error)->show();
            return;
        }

        WeakRef<LevelEditorLayer> editorRef(editor);
        PopupManager::get().quickPopup(
            "Restore Backup",
            "Replace <cr>all objects</c> in this editor with the backup?\n"
            "This cannot be undone with Ctrl+Z.",
            "Cancel", "Restore",
            [editorRef, body = std::move(body)](FLAlertLayer*, bool btn2) {
                if (!btn2) return;
                auto lel = editorRef.lock();
                if (!lel || !lel->m_level) return;

                if (body.size() > 64u * 1024u * 1024u
                    || body.find(',') == std::string::npos) {
                    Notification::create("Invalid backup data", NotificationIcon::Error)->show();
                    return;
                }

                // Reopen the editor from the restored level string. Replacing
                // thousands of live objects in-place risks leaving cached
                // section/trigger pointers owned by the current editor.
                Ref<GJGameLevel> level = lel->m_level;
                auto previous = std::string(level->m_levelString);
                level->m_levelString = body;
                Loader::get()->queueInMainThread(
                    [level = std::move(level), previous = std::move(previous)]() mutable {
                        auto* scene = LevelEditorLayer::scene(level, false);
                        if (!scene) {
                            level->m_levelString = previous;
                            Notification::create("Restore failed", NotificationIcon::Error)->show();
                            return;
                        }
                        CCDirector::get()->replaceScene(scene);
                        Notification::create("Backup restored", NotificationIcon::Success)->show();
                    }
                );
            }
        ).showInstant();
        onClose(nullptr);
    }

    void keyBackClicked() override { onClose(nullptr); }
    void onClose(CCObject*) { removeFromParentAndCleanup(true); }
};

void PaimonBackupPause::onOpenBackups(CCObject*) {
    if (!bkOn() || !m_editorLayer) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || scene->getChildByID("paimbnails/backup-list")) return;
    if (auto* layer = BackupListLayer::create(m_editorLayer)) {
        scene->addChild(layer, 300);
    }
}

// Offer restore of newest autosave once on MenuLayer if crash-ish leftover exists
class $modify(PaimonBackupMenu, MenuLayer) {
    $override
    bool init() {
        if (!MenuLayer::init()) return false;
        // Soft note only — avoid aggressive restore UX.
        return true;
    }
};
