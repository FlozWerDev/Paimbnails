#include "EditorHistoryPopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;
using namespace paimon::editorhistory;

namespace {

ButtonSprite* kindChip(char const* text, ObjChangeKind kind, int count) {
    auto col = objChangeColor(kind);
    char const* frame = count > 0 ? "GJ_button_02.png" : "GJ_button_04.png";
    auto* s = ButtonSprite::create(text, "bigFont.fnt", frame, 0.38f);
    s->setScale(0.55f);
    if (count > 0) {
        if (auto* lab = typeinfo_cast<CCLabelBMFont*>(s->getChildByTag(1))) {
            lab->setColor(col);
        }
    }
    return s;
}

} // namespace

EditorUndoPanel* EditorUndoPanel::create(EditorUI* ui) {
    auto* ret = new EditorUndoPanel();
    if (ret->init(ui)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

EditorUndoPanel* EditorUndoPanel::findOpen() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return nullptr;
    return typeinfo_cast<EditorUndoPanel*>(scene->getChildByID("editor-undo-panel"_spr));
}

void EditorUndoPanel::closeIfOpen() {
    if (auto* p = findOpen()) p->removeFromParent();
}

void EditorUndoPanel::toggle(EditorUI* ui) {
    if (!historyEnabled() || !ui) return;
    if (auto* existing = findOpen()) {
        existing->removeFromParent();
        return;
    }
    if (auto* p = create(ui)) {
        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        scene->addChild(p, 200);
    }
}

bool EditorUndoPanel::init(EditorUI* ui) {
    if (!CCNode::init()) return false;
    m_ui = ui;
    setID("editor-undo-panel"_spr);
    setAnchorPoint({0.5f, 0.5f});

    auto win = CCDirector::get()->getWinSize();
    setContentSize(win);
    setPosition(win / 2.f);

    auto* dimMenu = CCMenu::create();
    dimMenu->setPosition({0.f, 0.f});
    dimMenu->setContentSize(win);
    dimMenu->setAnchorPoint({0.f, 0.f});
    dimMenu->ignoreAnchorPointForPosition(false);
    auto* dimHit = CCMenuItemSpriteExtra::create(
        CCLayerColor::create({0, 0, 0, 70}, win.width, win.height),
        this,
        menu_selector(EditorUndoPanel::onClose)
    );
    dimHit->setAnchorPoint({0.f, 0.f});
    dimHit->setPosition({0.f, 0.f});
    dimMenu->addChild(dimHit);
    addChild(dimMenu, 0);

    constexpr float cardW = 320.f;
    constexpr float cardH = 240.f;
    auto* card = CCScale9Sprite::create("GJ_square02.png");
    if (!card) card = CCScale9Sprite::create("square02b_001.png");
    card->setContentSize({cardW, cardH});
    card->setPosition({win.width * 0.5f, win.height * 0.55f});
    card->setOpacity(245);
    card->setID("editor-undo-card"_spr);
    addChild(card, 1);

    auto* title = CCLabelBMFont::create("Editor History", "goldFont.fnt");
    title->setScale(0.45f);
    title->setPosition({cardW / 2.f, cardH - 16.f});
    card->addChild(title);

    m_hint = CCLabelBMFont::create("Color / Groups / Layers", "chatFont.fnt");
    m_hint->setScale(0.38f);
    m_hint->setOpacity(180);
    m_hint->setPosition({cardW / 2.f, cardH - 32.f});
    card->addChild(m_hint);

    // Filter chips
    m_menu = CCMenu::create();
    m_menu->setContentSize({cardW - 16.f, 28.f});
    m_menu->setPosition({cardW / 2.f, cardH - 55.f});
    m_menu->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    card->addChild(m_menu);

    // History list
    m_scroll = ScrollLayer::create({cardW - 24.f, 140.f});
    m_scroll->setPosition({12.f, 18.f});
    m_scroll->m_contentLayer->setLayout(
        ColumnLayout::create()
            ->setAxisReverse(true)
            ->setAutoScale(false)
            ->setGap(3.f)
            ->setAxisAlignment(AxisAlignment::End)
    );
    card->addChild(m_scroll);

    auto* closeMenu = CCMenu::create();
    closeMenu->setPosition({0, 0});
    card->addChild(closeMenu, 5);
    auto* close = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
        this, menu_selector(EditorUndoPanel::onClose)
    );
    close->setScale(0.55f);
    close->setPosition({cardW - 12.f, cardH - 12.f});
    closeMenu->addChild(close);

    refresh();
    this->schedule(schedule_selector(EditorUndoPanel::onTick), 0.35f);
    return true;
}

void EditorUndoPanel::onTick(float) {
    ObjectTimelineStore::get().tick();
    if (ObjectTimelineStore::get().revision() != m_seen) refresh();
}

void EditorUndoPanel::refresh() {
    if (!m_menu) return;
    m_menu->removeAllChildren();

    auto& store = ObjectTimelineStore::get();
    struct Entry { char const* name; int kind; };
    Entry entries[] = {
        {"All", -1},
        {"Color", static_cast<int>(ObjChangeKind::Color)},
        {"Groups", static_cast<int>(ObjChangeKind::Groups)},
        {"Layers", static_cast<int>(ObjChangeKind::EditorLayer)},
    };

    for (auto const& e : entries) {
        int n = e.kind < 0 ? store.eventCount() : store.countOfKind(static_cast<ObjChangeKind>(e.kind));
        std::string label = n > 0 ? fmt::format("{} ({})", e.name, n) : std::string(e.name);
        auto* spr = ButtonSprite::create(
            label.c_str(), "bigFont.fnt",
            m_filter == e.kind ? "GJ_button_01.png" : "GJ_button_05.png", 0.32f
        );
        spr->setScale(0.55f);
        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(EditorUndoPanel::onFilter)
        );
        btn->setTag(e.kind);
        m_menu->addChild(btn);
    }
    m_menu->updateLayout();

    // Quick undo last of each kind
    auto* quick = CCMenu::create();
    // attached after filters via rebuildList only for nodes

    rebuildList();

    if (m_hint) {
        m_hint->setString(
            store.eventCount() > 0
                ? fmt::format("{} tracked edits — tap Undo on a row", store.eventCount()).c_str()
                : "No color / groups / layers edits yet"
        );
    }
    m_seen = store.revision();
}

void EditorUndoPanel::rebuildList() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto nodes = ObjectTimelineStore::get().recentNodes(60);
    int shown = 0;
    for (auto const& n : nodes) {
        if (m_filter >= 0 && static_cast<int>(n.kind) != m_filter) continue;
        ++shown;

        auto* row = CCNode::create();
        row->setContentSize({290.f, 26.f});

        auto* bg = CCLayerColor::create({20, 20, 30, 160}, 290.f, 24.f);
        bg->setPosition({0.f, 1.f});
        row->addChild(bg);

        auto col = objChangeColor(n.kind);
        auto* lab = CCLabelBMFont::create(
            fmt::format("{}  #{}  {}", objChangeName(n.kind), n.uniqueId, n.label).c_str(),
            "chatFont.fnt"
        );
        lab->setScale(0.4f);
        lab->setAnchorPoint({0.f, 0.5f});
        lab->setColor(col);
        lab->setPosition({6.f, 13.f});
        row->addChild(lab);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(row->getContentSize());
        row->addChild(menu);

        auto* undoSpr = ButtonSprite::create("Undo", "bigFont.fnt", "GJ_button_06.png", 0.3f);
        undoSpr->setScale(0.55f);
        auto* undoBtn = CCMenuItemSpriteExtra::create(
            undoSpr, this, menu_selector(EditorUndoPanel::onUndoNode)
        );
        // Encode id in tag (low 32 bits) — ids are sequential uint64 but fit int for a while
        undoBtn->setTag(static_cast<int>(n.id & 0x7fffffff));
        undoBtn->setUserObject(CCInteger::create(static_cast<int>(n.id >> 32)));
        undoBtn->setPosition({230.f, 13.f});
        menu->addChild(undoBtn);

        auto* focSpr = ButtonSprite::create("Go", "bigFont.fnt", "GJ_button_04.png", 0.3f);
        focSpr->setScale(0.55f);
        auto* focBtn = CCMenuItemSpriteExtra::create(
            focSpr, this, menu_selector(EditorUndoPanel::onFocusNode)
        );
        focBtn->setTag(n.uniqueId);
        focBtn->setPosition({270.f, 13.f});
        menu->addChild(focBtn);

        m_scroll->m_contentLayer->addChild(row);
    }

    if (shown == 0) {
        auto* empty = CCLabelBMFont::create("Nothing here", "bigFont.fnt");
        empty->setScale(0.35f);
        m_scroll->m_contentLayer->addChild(empty);
    }

    m_scroll->m_contentLayer->updateLayout();
    m_scroll->scrollToTop();
}

void EditorUndoPanel::onFilter(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item) return;
    m_filter = item->getTag();
    refresh();
}

void EditorUndoPanel::onPick(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item || !m_ui) return;
    auto kind = static_cast<ObjChangeKind>(item->getTag());
    bool ok = ObjectTimelineStore::get().undoLastOfKind(m_ui, kind);
    Notification::create(
        ok ? fmt::format("Undid last {}", objChangeName(kind))
           : fmt::format("Nothing to undo for {}", objChangeName(kind)),
        ok ? NotificationIcon::Success : NotificationIcon::Info,
        1.2f
    )->show();
    refresh();
}

void EditorUndoPanel::onUndoNode(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item || !m_ui) return;
    uint64_t id = static_cast<uint32_t>(item->getTag());
    if (auto* hi = typeinfo_cast<CCInteger*>(item->getUserObject())) {
        id |= (static_cast<uint64_t>(static_cast<uint32_t>(hi->getValue())) << 32);
    }
    bool ok = ObjectTimelineStore::get().undoNode(m_ui, id);
    Notification::create(
        ok ? "Undid change" : "Could not undo",
        ok ? NotificationIcon::Success : NotificationIcon::Warning,
        1.1f
    )->show();
    refresh();
}

void EditorUndoPanel::onFocusNode(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item || !m_ui || !m_ui->m_editorLayer) return;
    int uid = item->getTag();
    auto* lel = m_ui->m_editorLayer;
    if (!lel->m_objects) return;
    for (auto* o : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (o && o->m_uniqueID == uid) {
            m_ui->selectObject(o, true);
            // Center camera roughly
            if (lel->m_objectLayer) {
                auto win = CCDirector::get()->getWinSize();
                auto world = lel->m_objectLayer->convertToWorldSpace(o->getPosition());
                auto delta = win / 2.f - world;
                lel->m_objectLayer->setPosition(lel->m_objectLayer->getPosition() + delta);
                m_ui->updateSlider();
            }
            Notification::create("Focused object", NotificationIcon::Info, 0.8f)->show();
            return;
        }
    }
}

void EditorUndoPanel::onClose(CCObject*) {
    this->removeFromParent();
}
