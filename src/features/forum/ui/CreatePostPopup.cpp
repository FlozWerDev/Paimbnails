#include "CreatePostPopup.hpp"
#include "../services/ForumHelpers.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/cocos/touch_dispatcher/CCTouchDispatcher.h>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;
using namespace cocos2d;
using paimon::forum::CreatePostRequest;
using paimon::forum::ForumApi;
using paimon::forum::Post;

namespace {

constexpr float POPUP_W = 468.f;
constexpr float POPUP_H = 340.f;
constexpr int kMaxTagsPerPost = 10;

struct PostDraft {
    std::string title;
    std::string desc;
    std::string levelId;
    std::vector<std::string> tags;
};

PostDraft& sessionDraft() {
    static PostDraft s_draft;
    return s_draft;
}

bool isSpanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

std::string tf(char const* key, char const* en, char const* es) {
    return paimon::loc::tr(key, isSpanish() ? es : en);
}

class NewTagDialog : public PaimonPopup {
public:
    static NewTagDialog* create(std::function<void(std::string const&)> onAdd) {
        auto* ret = new NewTagDialog();
        if (ret && ret->init(std::move(onAdd))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    geode::TextInput* m_input = nullptr;
    std::function<void(std::string const&)> m_onAdd;

    bool init(std::function<void(std::string const&)> onAdd) {
        if (!PaimonPopup::init(260.f, 136.f)) return false;
        m_onAdd = std::move(onAdd);
        this->setTitle(tf("pai.hub.forum.create_tag", "Create Custom Tag", "Crear Etiqueta").c_str());

        auto size = m_mainLayer->getContentSize();
        auto* inset = paimon::ui::makeInset({size.width - 28.f, 50.f}, 85);
        inset->setPosition({14.f, 44.f});
        m_mainLayer->addChild(inset, -1);

        m_input = TextInput::create(210.f,
            tf("pai.hub.forum.new_tag", "Tag name...", "Nombre de etiqueta...").c_str(),
            "chatFont.fnt");
        m_input->setCommonFilter(CommonFilter::Text);
        m_input->setMaxCharCount(24);
        m_input->setPosition({size.width / 2.f, 69.f});
        m_input->setScale(0.82f);
        m_mainLayer->addChild(m_input, 2);

        WeakRef<NewTagDialog> self = this;
        auto* addBtn = paimon::ui::makeButton(
            tf("pai.hub.forum.create", "Add", "Anadir").c_str(),
            [self] {
                if (auto p = self.lock()) p->submit();
            },
            paimon::ui::Btn::Green, 90.f, 0.65f
        );
        addBtn->setPosition({size.width / 2.f, 22.f});
        m_buttonMenu->addChild(addBtn);

        paimon::markDynamicPopup(this);
        return true;
    }

    void submit() {
        std::string val = m_input ? paimon::forum::trimAscii(m_input->getString()) : "";
        if (val.empty()) {
            PaimonNotify::show(
                isSpanish() ? "Escribe un nombre para la etiqueta" : "Enter a tag name first",
                NotificationIcon::Warning
            );
            return;
        }
        auto cb = m_onAdd;
        this->onClose(nullptr);
        if (cb) cb(val);
    }

    void onExit() override {
        paimon::ui::detachGeodeTextInput(m_input);
        Popup::onExit();
    }
};

} // namespace

bool CreatePostPopup::init(
    std::vector<std::string> availableTags,
    CopyableFunction<void(Post const&)> onCreated
) {
    if (!PaimonPopup::init(POPUP_W, POPUP_H)) return false;
    this->setMouseEnabled(true);

    bool const es = isSpanish();
    m_availableTags.clear();
    for (auto const& t : availableTags) {
        std::string key = paimon::forum::normalizeTagKey(t);
        if (key.empty()) continue;
        bool dup = std::any_of(m_availableTags.begin(), m_availableTags.end(),
            [&](std::string const& x) { return paimon::forum::normalizeTagKey(x) == key; });
        if (!dup) m_availableTags.push_back(paimon::forum::displayTag(t));
    }
    m_onCreated = std::move(onCreated);

    this->setTitle(tf("pai.hub.forum.create.title", "Create New Post", "Crear Nuevo Post").c_str());
    this->addInfoButton(
        tf("pai.hub.forum.create.info_title", "Create Post", "Crear Post"),
        es ? "Elige una plantilla o escribe tu titulo y descripcion.\n\n"
             "- Puedes adjuntar un <cy>ID de nivel</c> opcional para que otros lo abran directo en GD.\n"
             "- Selecciona hasta <cg>10 etiquetas</c> o crea una propia con <cg>+</c>.\n"
             "- Tu borrador se conserva si cierras esta ventana."
           : "Pick a quick template or write your own title and details.\n\n"
             "- Attach an optional <cy>GD Level ID</c> so readers can open it with one tap.\n"
             "- Select up to <cg>10 tags</c> or add a custom tag with <cg>+</c>.\n"
             "- Your draft is preserved if you close this popup."
    );

    auto contentSize = m_mainLayer->getContentSize();
    float const cx = contentSize.width / 2.f;
    float const padX = 14.f;
    float const innerW = contentSize.width - padX * 2.f;

    WeakRef<CreatePostPopup> self = this;

    // -------------------------------------------------------------------------
    // 1. Optional Login Warning or Quick Templates Strip
    // -------------------------------------------------------------------------
    auto* acc = GJAccountManager::get();
    bool const loggedIn = acc && acc->m_accountID > 0;
    float topCursor = contentSize.height - 34.f;

    if (!loggedIn) {
        auto* warnBg = paimon::ui::makeInset({innerW, 18.f}, 115, {120, 48, 22});
        warnBg->setPosition({padX, topCursor - 18.f});
        m_mainLayer->addChild(warnBg, 0);

        auto* warnLbl = CCLabelBMFont::create(
            es ? "Inicia sesion en tu cuenta de GD para publicar en el servidor."
               : "Sign in to your GD account to publish posts online.",
            "chatFont.fnt"
        );
        warnLbl->setScale(0.42f);
        warnLbl->setColor(paimon::ui::palette::warning);
        warnLbl->setPosition({cx, topCursor - 9.f});
        m_mainLayer->addChild(warnLbl, 1);
        topCursor -= 22.f;
    }

    // Templates strip
    {
        auto* tplLbl = CCLabelBMFont::create(
            es ? "Plantillas:" : "Templates:", "bigFont.fnt"
        );
        tplLbl->setScale(0.28f);
        tplLbl->setColor(paimon::ui::palette::muted);
        tplLbl->setAnchorPoint({0.f, 0.5f});
        tplLbl->setPosition({padX, topCursor - 9.f});
        m_mainLayer->addChild(tplLbl);

        auto* tplMenu = CCMenu::create();
        tplMenu->setContentSize({innerW - 74.f, 20.f});
        tplMenu->setAnchorPoint({0.f, 0.5f});
        tplMenu->setPosition({padX + 74.f, topCursor - 9.f});
        if (auto* disp = CCDirector::get()->getTouchDispatcher()) {
            tplMenu->setTouchPriority(disp->getTargetPrio() - 2);
        }
        tplMenu->setLayout(
            RowLayout::create()->setGap(4.f)->setAutoScale(false)->setAxisAlignment(AxisAlignment::Start)
        );
        m_mainLayer->addChild(tplMenu, 6);

        char const* tpls[] = {"Question", "Showcase", "Guide", "Bug", "Suggestion"};
        for (char const* tpl : tpls) {
            std::string tag = tpl;
            auto* btn = paimon::ui::makeButton(
                tpl,
                [self, tag] {
                    if (auto p = self.lock()) p->applyTemplate(tag);
                },
                paimon::ui::Btn::Blue, 0.f, 0.42f, "bigFont.fnt"
            );
            tplMenu->addChild(btn);
        }
        tplMenu->updateLayout();
        topCursor -= 22.f;
    }

    // -------------------------------------------------------------------------
    // 2. Main Form Card (Title + Level ID + Description)
    // -------------------------------------------------------------------------
    float const formH = 118.f;
    float const formBot = topCursor - formH;
    auto* formInset = paimon::ui::makeInset({innerW, formH}, 80);
    formInset->setPosition({padX, formBot});
    m_mainLayer->addChild(formInset, -1);

    float const leftColW = innerW - 128.f;
    float const row1LblY = topCursor - 11.f;
    float const row1InputY = topCursor - 28.f;

    auto* titleLbl = CCLabelBMFont::create(
        tf("pai.hub.forum.title_placeholder", "Title", "Titulo").c_str(), "bigFont.fnt");
    titleLbl->setScale(0.32f);
    titleLbl->setAnchorPoint({0.f, 0.5f});
    titleLbl->setPosition({padX + 8.f, row1LblY});
    titleLbl->setColor(paimon::ui::palette::text);
    m_mainLayer->addChild(titleLbl);

    m_titleCountLabel = CCLabelBMFont::create("0/80", "chatFont.fnt");
    m_titleCountLabel->setScale(0.36f);
    m_titleCountLabel->setColor(paimon::ui::palette::dim);
    m_titleCountLabel->setAnchorPoint({1.f, 0.5f});
    m_titleCountLabel->setPosition({padX + leftColW + 6.f, row1LblY});
    m_mainLayer->addChild(m_titleCountLabel);

    constexpr float kInputScale = 0.80f;
    m_titleInput = TextInput::create(leftColW / kInputScale,
        es ? "Titulo de la publicacion..." : "Post title...", "chatFont.fnt");
    m_titleInput->setCommonFilter(CommonFilter::Text);
    m_titleInput->setMaxCharCount(80);
    m_titleInput->setScale(kInputScale);
    m_titleInput->setPosition({padX + 8.f + leftColW / 2.f, row1InputY});
    m_titleInput->setCallback([self](std::string const&) {
        if (auto p = self.lock()) p->updateCharCounters();
    });
    m_mainLayer->addChild(m_titleInput, 2);

    // Optional GD Level ID field on the right of Title
    float const lvlColX = padX + leftColW + 14.f;
    float const lvlColW = innerW - leftColW - 22.f;
    auto* lvlLbl = CCLabelBMFont::create(
        es ? "ID Nivel (opc.)" : "Level ID (opt.)", "bigFont.fnt");
    lvlLbl->setScale(0.28f);
    lvlLbl->setAnchorPoint({0.f, 0.5f});
    lvlLbl->setPosition({lvlColX, row1LblY});
    lvlLbl->setColor(paimon::ui::palette::info);
    m_mainLayer->addChild(lvlLbl);

    m_levelIdInput = TextInput::create(lvlColW / kInputScale, "12345678", "chatFont.fnt");
    m_levelIdInput->setCommonFilter(CommonFilter::Uint);
    m_levelIdInput->setMaxCharCount(10);
    m_levelIdInput->setScale(kInputScale);
    m_levelIdInput->setPosition({lvlColX + lvlColW / 2.f, row1InputY});
    m_mainLayer->addChild(m_levelIdInput, 2);

    // Description field
    float const row2LblY = topCursor - 49.f;
    float const row2InputY = topCursor - 72.f;

    auto* descLbl = CCLabelBMFont::create(
        tf("pai.hub.forum.desc_placeholder", "Description", "Descripcion").c_str(), "bigFont.fnt");
    descLbl->setScale(0.32f);
    descLbl->setAnchorPoint({0.f, 0.5f});
    descLbl->setPosition({padX + 8.f, row2LblY});
    descLbl->setColor(paimon::ui::palette::text);
    m_mainLayer->addChild(descLbl);

    m_descCountLabel = CCLabelBMFont::create("0/500", "chatFont.fnt");
    m_descCountLabel->setScale(0.36f);
    m_descCountLabel->setColor(paimon::ui::palette::dim);
    m_descCountLabel->setAnchorPoint({1.f, 0.5f});
    m_descCountLabel->setPosition({padX + innerW - 8.f, row2LblY});
    m_mainLayer->addChild(m_descCountLabel);

    float const descW = innerW - 16.f;
    m_descInput = TextInput::create(descW / kInputScale,
        es ? "Anade detalles, consejos o explicacion..." : "Add details, tips, or context...", "chatFont.fnt");
    m_descInput->setCommonFilter(CommonFilter::TextEmojis);
    m_descInput->setMaxCharCount(500);
    m_descInput->setScale(kInputScale);
    m_descInput->setPosition({cx, row2InputY});
    m_descInput->setCallback([self](std::string const&) {
        if (auto p = self.lock()) p->updateCharCounters();
    });
    m_mainLayer->addChild(m_descInput, 2);

    auto* mdHint = CCLabelBMFont::create(
        es ? "Soporta Markdown basico. Los IDs de nivel se detectan automaticamente."
           : "Supports basic Markdown. GD Level IDs are detected automatically.",
        "chatFont.fnt"
    );
    mdHint->setScale(0.36f);
    mdHint->setColor(paimon::ui::palette::dim);
    mdHint->setAnchorPoint({0.f, 0.5f});
    mdHint->setPosition({padX + 8.f, formBot + 12.f});
    m_mainLayer->addChild(mdHint);

    // -------------------------------------------------------------------------
    // 3. Tags Section (Header + Scrollable Tag Grid)
    // -------------------------------------------------------------------------
    float const tagsHeaderY = formBot - 12.f;
    m_tagsHint = CCLabelBMFont::create("", "bigFont.fnt");
    m_tagsHint->setScale(0.30f);
    m_tagsHint->setColor(paimon::ui::palette::gold);
    m_tagsHint->setAnchorPoint({0.f, 0.5f});
    m_tagsHint->setPosition({padX, tagsHeaderY});
    m_mainLayer->addChild(m_tagsHint);

    auto* plusBtn = paimon::ui::makeButton(
        es ? "+ Etiqueta" : "+ Custom",
        [self] {
            if (auto p = self.lock()) p->onAddCustomTag(nullptr);
        },
        paimon::ui::Btn::Cyan, 0.f, 0.42f, "bigFont.fnt"
    );
    plusBtn->setPosition({padX + innerW - plusBtn->getScaledContentSize().width / 2.f, tagsHeaderY});
    m_buttonMenu->addChild(plusBtn);

    float const tagBoxTop = tagsHeaderY - 12.f;
    float const tagBoxBot = 40.f;
    float const tagBoxH = std::max(44.f, tagBoxTop - tagBoxBot);

    auto* tagsBg = paimon::ui::makeInset({innerW, tagBoxH}, 70);
    tagsBg->setPosition({padX, tagBoxBot});
    m_mainLayer->addChild(tagsBg, -1);

    m_tagScroll = ScrollLayer::create({innerW - 8.f, tagBoxH - 6.f});
    if (m_tagScroll) m_tagScroll->setAnchorPoint({0.f, 0.f});
    m_tagScroll->setPosition({padX + 4.f, tagBoxBot + 3.f});
    m_tagScroll->setStealingTouches(true);
    m_tagScroll->enableScrollWheel(false);
    m_tagScroll->setID("create-post-tags-scroll"_spr);
    m_mainLayer->addChild(m_tagScroll, 2);

    m_tagMenu = CCMenu::create();
    m_tagMenu->setID("create-post-tags"_spr);
    m_tagMenu->setAnchorPoint({0.f, 0.f});
    m_tagMenu->setPosition({0.f, 0.f});
    if (auto* disp = CCDirector::get()->getTouchDispatcher()) {
        m_tagMenu->setTouchPriority(disp->getTargetPrio() - 2);
    }
    m_tagMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(true)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Start)
    );
    m_tagScroll->m_contentLayer->addChild(m_tagMenu);

    // -------------------------------------------------------------------------
    // 4. Bottom Action Row (Clear Draft, Cooldown, Publish)
    // -------------------------------------------------------------------------
    m_cooldownLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_cooldownLabel->setScale(0.42f);
    m_cooldownLabel->setColor(paimon::ui::palette::warning);
    m_cooldownLabel->setPosition({cx, 35.f});
    m_cooldownLabel->setVisible(false);
    m_mainLayer->addChild(m_cooldownLabel, 10);

    auto* clearBtn = paimon::ui::makeButton(
        es ? "Limpiar" : "Clear",
        [self] {
            if (auto p = self.lock()) p->onClearDraft(nullptr);
        },
        paimon::ui::Btn::Gray, 82.f, 0.62f
    );
    clearBtn->setPosition({padX + 46.f, 18.f});
    m_buttonMenu->addChild(clearBtn);

    m_submitBtn = paimon::ui::makeButton(
        tf("pai.hub.forum.publish", "Publish", "Publicar").c_str(),
        [self] {
            if (auto p = self.lock()) p->onSubmit(nullptr);
        },
        paimon::ui::Btn::Green, 118.f, 0.68f
    );
    m_submitBtn->setID("submit-post-btn"_spr);
    m_submitBtn->setPosition({cx + 42.f, 18.f});
    m_buttonMenu->addChild(m_submitBtn);

    // Restore session draft if present
    auto const& draft = sessionDraft();
    if (!draft.title.empty() && m_titleInput) m_titleInput->setString(draft.title);
    if (!draft.desc.empty() && m_descInput) m_descInput->setString(draft.desc);
    if (!draft.levelId.empty() && m_levelIdInput) m_levelIdInput->setString(draft.levelId);
    for (auto const& t : draft.tags) {
        std::string disp = paimon::forum::displayTag(t);
        std::string key = paimon::forum::normalizeTagKey(t);
        bool exists = std::any_of(m_availableTags.begin(), m_availableTags.end(),
            [&](std::string const& x) { return paimon::forum::normalizeTagKey(x) == key; });
        if (!exists && !key.empty()) m_availableTags.push_back(disp);
        if (!key.empty() && static_cast<int>(m_selectedTags.size()) < kMaxTagsPerPost) {
            bool already = std::any_of(m_selectedTags.begin(), m_selectedTags.end(),
                [&](std::string const& x) { return paimon::forum::normalizeTagKey(x) == key; });
            if (!already) m_selectedTags.push_back(disp);
        }
    }

    rebuildTagChips();
    updateCharCounters();
    updateCooldownLabel();
    paimon::markDynamicPopup(this);
    this->scheduleUpdate();
    return true;
}

void CreatePostPopup::applyTemplate(std::string const& tagName) {
    bool const es = isSpanish();
    std::string key = paimon::forum::normalizeTagKey(tagName);
    bool exists = std::any_of(m_availableTags.begin(), m_availableTags.end(),
        [&](std::string const& x) { return paimon::forum::normalizeTagKey(x) == key; });
    if (!exists) m_availableTags.push_back(tagName);

    bool alreadySelected = std::any_of(m_selectedTags.begin(), m_selectedTags.end(),
        [&](std::string const& x) { return paimon::forum::normalizeTagKey(x) == key; });
    if (!alreadySelected && static_cast<int>(m_selectedTags.size()) < kMaxTagsPerPost) {
        m_selectedTags.push_back(tagName);
    }
    rebuildTagChips();

    if (m_descInput && paimon::forum::trimAscii(m_descInput->getString()).empty()) {
        std::string scaffold;
        if (key == "bug") {
            scaffold = es ? "Pasos para reproducir: ... | Esperado: ... | Actual: ..."
                          : "Steps to reproduce: ... | Expected: ... | Actual: ...";
        } else if (key == "showcase") {
            scaffold = es ? "Mira mi nivel / creacion! Dificultad y detalles:"
                          : "Check out my showcase! Details & feedback welcome:";
        } else if (key == "guide") {
            scaffold = es ? "Guia rapida paso a paso:"
                          : "Quick step-by-step guide:";
        } else if (key == "question") {
            scaffold = es ? "Alguien sabe como..."
                          : "Does anyone know how to...";
        } else if (key == "suggestion") {
            scaffold = es ? "Idea para mejorar Paimbnails:"
                          : "Suggestion to improve Paimbnails:";
        }
        if (!scaffold.empty()) {
            m_descInput->setString(scaffold);
            updateCharCounters();
        }
    }
}

void CreatePostPopup::rebuildTagChips() {
    if (!m_tagMenu || !m_tagScroll) return;
    m_tagMenu->removeAllChildren();

    bool const es = isSpanish();
    if (m_tagsHint) {
        m_tagsHint->setString(fmt::format(
            fmt::runtime(es ? "Etiquetas ({}/{})" : "Tags ({}/{})"),
            m_selectedTags.size(), kMaxTagsPerPost
        ).c_str());
    }

    float const scrollW = m_tagScroll->getContentSize().width;
    float const scrollH = m_tagScroll->getContentSize().height;
    m_tagMenu->setContentSize({scrollW, scrollH});

    WeakRef<CreatePostPopup> self = this;
    for (size_t i = 0; i < m_availableTags.size(); ++i) {
        std::string key = paimon::forum::normalizeTagKey(m_availableTags[i]);
        bool selected = std::any_of(m_selectedTags.begin(), m_selectedTags.end(),
            [&](std::string const& s) { return paimon::forum::normalizeTagKey(s) == key; });
        auto* btn = paimon::ui::makeButton(
            m_availableTags[i].c_str(),
            [self, idx = static_cast<int>(i)] {
                if (auto p = self.lock()) p->toggleTagAt(idx);
            },
            selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray,
            0.f, 0.44f, "bigFont.fnt"
        );
        btn->setTag(static_cast<int>(i));
        m_tagMenu->addChild(btn);
    }
    m_tagMenu->updateLayout();

    float const layoutH = std::max(scrollH, m_tagMenu->getContentSize().height + 6.f);
    m_tagScroll->m_contentLayer->setContentSize({scrollW, layoutH});
    m_tagMenu->setPosition({0.f, layoutH - m_tagMenu->getContentSize().height - 3.f});
    m_tagScroll->moveToTop();
    m_tagScrollTargetSet = false;
}

void CreatePostPopup::onToggleTag(CCObject* sender) {
    toggleTagAt(static_cast<CCNode*>(sender)->getTag());
}

void CreatePostPopup::toggleTagAt(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_availableTags.size())) return;
    auto const& tag = m_availableTags[idx];
    std::string key = paimon::forum::normalizeTagKey(tag);

    auto it = std::find_if(m_selectedTags.begin(), m_selectedTags.end(),
        [&](std::string const& s) { return paimon::forum::normalizeTagKey(s) == key; });
    if (it != m_selectedTags.end()) {
        m_selectedTags.erase(it);
    } else {
        if (static_cast<int>(m_selectedTags.size()) >= kMaxTagsPerPost) {
            PaimonNotify::show(
                isSpanish() ? "Maximo 10 etiquetas por post" : "Maximum 10 tags per post",
                NotificationIcon::Warning
            );
            return;
        }
        m_selectedTags.push_back(tag);
    }
    rebuildTagChips();
}

void CreatePostPopup::onAddCustomTag(CCObject*) {
    WeakRef<CreatePostPopup> self = this;
    auto* dialog = NewTagDialog::create([self](std::string const& val) {
        auto popup = self.lock();
        if (!popup || !popup->getParent()) return;
        std::string key = paimon::forum::normalizeTagKey(val);
        if (key.empty()) return;
        std::string disp = paimon::forum::displayTag(val);

        bool exists = std::any_of(popup->m_availableTags.begin(), popup->m_availableTags.end(),
            [&](std::string const& t) { return paimon::forum::normalizeTagKey(t) == key; });
        if (!exists) popup->m_availableTags.push_back(disp);

        bool selected = std::any_of(popup->m_selectedTags.begin(), popup->m_selectedTags.end(),
            [&](std::string const& t) { return paimon::forum::normalizeTagKey(t) == key; });
        if (!selected && static_cast<int>(popup->m_selectedTags.size()) < kMaxTagsPerPost) {
            popup->m_selectedTags.push_back(disp);
        }
        popup->rebuildTagChips();
    });
    if (dialog) paimon::configkit::showAbove(dialog, this);
}

void CreatePostPopup::onClearDraft(CCObject*) {
    if (m_titleInput) m_titleInput->setString("");
    if (m_descInput) m_descInput->setString("");
    if (m_levelIdInput) m_levelIdInput->setString("");
    m_selectedTags.clear();
    sessionDraft() = PostDraft{};
    rebuildTagChips();
    updateCharCounters();
    PaimonNotify::show(
        isSpanish() ? "Borrador limpiado" : "Draft cleared",
        NotificationIcon::Info
    );
}

void CreatePostPopup::updateCharCounters() {
    if (m_titleCountLabel && m_titleInput) {
        size_t len = m_titleInput->getString().size();
        m_titleCountLabel->setString(fmt::format("{}/80", len).c_str());
        m_titleCountLabel->setColor(len >= 72 ? paimon::ui::palette::warning : paimon::ui::palette::dim);
    }
    if (m_descCountLabel && m_descInput) {
        size_t len = m_descInput->getString().size();
        m_descCountLabel->setString(fmt::format("{}/500", len).c_str());
        m_descCountLabel->setColor(len >= 460 ? paimon::ui::palette::warning : paimon::ui::palette::dim);
    }
}

void CreatePostPopup::saveDraft() {
    if (m_published) {
        sessionDraft() = PostDraft{};
        return;
    }
    auto& d = sessionDraft();
    d.title = m_titleInput ? std::string(m_titleInput->getString()) : "";
    d.desc = m_descInput ? std::string(m_descInput->getString()) : "";
    d.levelId = m_levelIdInput ? std::string(m_levelIdInput->getString()) : "";
    d.tags = m_selectedTags;
}

void CreatePostPopup::onSubmit(CCObject*) {
    if (m_submitting) return;
    bool const es = isSpanish();

    auto* acc = GJAccountManager::get();
    if (!acc || acc->m_accountID <= 0) {
        PaimonNotify::show(
            es ? "Inicia sesion en tu cuenta de GD para publicar." : "Sign in to your GD account to publish posts.",
            NotificationIcon::Warning
        );
        return;
    }

    std::string title = m_titleInput ? paimon::forum::trimAscii(m_titleInput->getString()) : "";
    std::string desc  = m_descInput  ? paimon::forum::trimAscii(m_descInput->getString())  : "";
    std::string lvlId = m_levelIdInput ? paimon::forum::trimAscii(m_levelIdInput->getString()) : "";

    if (title.empty()) {
        PaimonNotify::show(es ? "Escribe un titulo primero" : "Please enter a title", NotificationIcon::Warning);
        return;
    }

    if (!lvlId.empty() && desc.find(lvlId) == std::string::npos && title.find(lvlId) == std::string::npos) {
        if (!desc.empty()) desc += "\n";
        desc += "Level ID: " + lvlId;
    }

    auto cd = ForumApi::get().getPostCooldownRemaining();
    if (cd > 0) {
        PaimonNotify::show(
            fmt::format(fmt::runtime(es ? "Espera {} segundos antes de publicar." : "Please wait {} seconds before posting again."), cd).c_str(),
            NotificationIcon::Warning
        );
        return;
    }

    m_submitting = true;
    if (m_submitBtn) {
        m_submitBtn->setEnabled(false);
        m_submitBtn->setOpacity(140);
    }

    CreatePostRequest req;
    req.title = title;
    req.description = desc;
    req.tags = m_selectedTags;

    auto onCreated = m_onCreated;
    WeakRef<CreatePostPopup> self = this;
    ForumApi::get().createPost(req, [self, onCreated, es](paimon::forum::Result<Post> res) {
        auto popup = self.lock();
        if (!res.ok) {
            if (popup) {
                popup->m_submitting = false;
                if (popup->m_submitBtn) {
                    popup->m_submitBtn->setEnabled(true);
                    popup->m_submitBtn->setOpacity(255);
                }
            }
            std::string msg = res.error;
            if (msg.find("Rate limited") != std::string::npos) {
                PaimonNotify::show(
                    es ? "Publicando demasiado rapido. Espera un momento." : "You're posting too fast. Please wait a moment.",
                    NotificationIcon::Warning
                );
            } else {
                PaimonNotify::show(((es ? "Error: " : "Failed: ") + msg).c_str(), NotificationIcon::Error);
            }
            return;
        }
        if (popup) {
            popup->m_submitting = false;
            popup->m_published = true;
        }
        sessionDraft() = PostDraft{};
        PaimonNotify::show(es ? "Post publicado!" : "Post published!", NotificationIcon::Success);
        if (onCreated) onCreated(res.data);
        if (popup) popup->onClose(nullptr);
    });
}

void CreatePostPopup::updateCooldownLabel() {
    if (!m_cooldownLabel) return;
    auto cd = ForumApi::get().getPostCooldownRemaining();
    if (cd > 0) {
        m_cooldownLabel->setString(
            fmt::format(fmt::runtime(isSpanish() ? "Espera {}s para publicar" : "Wait {}s to post"), cd).c_str()
        );
        m_cooldownLabel->setVisible(true);
    } else {
        m_cooldownLabel->setString("");
        m_cooldownLabel->setVisible(false);
    }
}

bool CreatePostPopup::scrollWheel(float x, float y) {
    if (paimon::configkit::queueWheelScroll(m_tagScroll, x, y, m_tagScrollTarget, m_tagScrollTargetSet)) {
        return true;
    }
    return PaimonPopup::scrollWheel(x, y);
}

void CreatePostPopup::onExit() {
    this->unscheduleUpdate();
    saveDraft();
    paimon::ui::detachGeodeTextInput(m_titleInput);
    paimon::ui::detachGeodeTextInput(m_levelIdInput);
    paimon::ui::detachGeodeTextInput(m_descInput);
    Popup::onExit();
}

void CreatePostPopup::update(float dt) {
    paimon::configkit::stepWheelScroll(m_tagScroll, m_tagScrollTarget, m_tagScrollTargetSet, dt);
    updateCooldownLabel();
}

CreatePostPopup* CreatePostPopup::create(
    std::vector<std::string> availableTags,
    CopyableFunction<void(Post const&)> onCreated
) {
    if (!paimon::modules::isEnabled("paimbnails.forum.menu")) return nullptr;

    auto* ret = new CreatePostPopup();
    if (ret && ret->init(std::move(availableTags), std::move(onCreated))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
