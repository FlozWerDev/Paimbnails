#include "CustomTransitionEditorPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "CustomTransitionScene.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../layers/PaimonInfoPopup.hpp"
#include <exception>

using namespace geode::prelude;
using namespace cocos2d;

// ════════════════════════════════════════════════════════════
// Static helpers
// ════════════════════════════════════════════════════════════

static CCMenuItemSpriteExtra* makeArrow(bool left, CCObject* t, SEL_MenuHandler s) {
    auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (left) spr->setFlipX(true);
    spr->setScale(0.3f);
    return CCMenuItemSpriteExtra::create(spr, t, s);
}

static CCMenuItemSpriteExtra* makeSmallBtn(const char* text, CCObject* t, SEL_MenuHandler s, const char* bg = "GJ_button_04.png") {
    auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, .6f);
    spr->setScale(0.5f);
    return CCMenuItemSpriteExtra::create(spr, t, s);
}

// ════════════════════════════════════════════════════════════
// allActions — all available command actions
// ════════════════════════════════════════════════════════════

std::vector<CommandAction> const& CustomTransitionEditorPopup::allActions() {
    static const std::vector<CommandAction> actions = {
        CommandAction::FadeOut,
        CommandAction::FadeIn,
        CommandAction::Move,
        CommandAction::Scale,
        CommandAction::Rotate,
        CommandAction::Wait,
        CommandAction::Color,
        CommandAction::EaseIn,
        CommandAction::EaseOut,
        CommandAction::Bounce,
        CommandAction::Shake,
        CommandAction::Image,
    };
    return actions;
}

int CustomTransitionEditorPopup::validateAndSanitizeForSave(std::vector<TransitionCommand>& commands) {
    return TransitionManager::sanitizeCommands(commands);
}

std::string CustomTransitionEditorPopup::actionDisplayName(CommandAction a) {
    switch (a) {
        case CommandAction::FadeOut:    return "Fade Out";
        case CommandAction::FadeIn:     return "Fade In";
        case CommandAction::Move:       return "Move";
        case CommandAction::Scale:      return "Scale";
        case CommandAction::Rotate:     return "Rotate";
        case CommandAction::Wait:       return "Wait";
        case CommandAction::Color:      return "Color";
        case CommandAction::EaseIn:     return "Ease In";
        case CommandAction::EaseOut:    return "Ease Out";
        case CommandAction::Bounce:     return "Bounce";
        case CommandAction::Shake:      return "Shake";
        case CommandAction::Image:      return "Image";
        case CommandAction::Spawn:      return "Spawn";
    }
    return "?";
}

// ════════════════════════════════════════════════════════════
// Create / Setup
// ════════════════════════════════════════════════════════════

CustomTransitionEditorPopup* CustomTransitionEditorPopup::create(TransitionConfig* config, bool isGlobal) {
    auto ret = new CustomTransitionEditorPopup();
    if (ret && ret->init(config, isGlobal)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomTransitionEditorPopup::init(TransitionConfig* config, bool isGlobal) {
    if (!Popup::init(520.f, 340.f)) return false;

    m_config = config;
    m_isGlobal = isGlobal;
    m_commands = config->commands;

    this->setTitle("Custom Transition Editor");

    auto ws = m_mainLayer->getContentSize();
    float cx = ws.width / 2.f;

    // ════════════════════════════════════════════════════
    // LEFT SIDE: Command list with scrollable area
    // ════════════════════════════════════════════════════
    float listW = 180.f;
    float listH = ws.height - 80.f;
    float listX = 15.f;
    float listY = 35.f;

    auto listTitle = CCLabelBMFont::create("Commands", "goldFont.fnt");
    listTitle->setScale(0.4f);
    listTitle->setPosition({listX + listW / 2, ws.height - 40.f});
    m_mainLayer->addChild(listTitle);

    // Dark panel behind list
    auto listPanel = paimon::SpriteHelper::createDarkPanel(listW, listH, 80);
    listPanel->setPosition({listX, listY});
    m_mainLayer->addChild(listPanel);

    m_scrollSize = CCSize(listW, listH);
    m_commandScroll = ScrollLayer::create(m_scrollSize);
    m_commandScroll->setPosition({listX, listY});
    m_mainLayer->addChild(m_commandScroll, 5);

    m_commandListMenu = CCNode::create();
    m_commandListMenu->setPosition({0, 0});
    m_commandListMenu->setContentSize(m_scrollSize);
    m_commandScroll->m_contentLayer->addChild(m_commandListMenu);

    // Buttons below list: Add, Remove, Up, Down, Duplicate
    float btnY = 18.f;
    float btnBaseX = listX + 10.f;

    auto addBtn = makeSmallBtn("+", this, menu_selector(CustomTransitionEditorPopup::onAddCommand), "GJ_button_01.png");
    addBtn->setPosition({btnBaseX, btnY});
    m_buttonMenu->addChild(addBtn);

    auto delBtn = makeSmallBtn("-", this, menu_selector(CustomTransitionEditorPopup::onRemoveCommand), "GJ_button_06.png");
    delBtn->setPosition({btnBaseX + 30, btnY});
    m_buttonMenu->addChild(delBtn);

    auto upBtn = makeSmallBtn("^", this, menu_selector(CustomTransitionEditorPopup::onMoveUp));
    upBtn->setPosition({btnBaseX + 60, btnY});
    m_buttonMenu->addChild(upBtn);

    auto downBtn = makeSmallBtn("v", this, menu_selector(CustomTransitionEditorPopup::onMoveDown));
    downBtn->setPosition({btnBaseX + 90, btnY});
    m_buttonMenu->addChild(downBtn);

    auto dupBtn = makeSmallBtn("Dup", this, menu_selector(CustomTransitionEditorPopup::onDuplicateCommand));
    dupBtn->setPosition({btnBaseX + 125, btnY});
    m_buttonMenu->addChild(dupBtn);

    auto prevCmdBtn = makeSmallBtn("<", this, menu_selector(CustomTransitionEditorPopup::onPrevCommand));
    prevCmdBtn->setPosition({btnBaseX + 155, btnY});
    m_buttonMenu->addChild(prevCmdBtn);

    auto nextCmdBtn = makeSmallBtn(">", this, menu_selector(CustomTransitionEditorPopup::onNextCommand));
    nextCmdBtn->setPosition({btnBaseX + 175, btnY});
    m_buttonMenu->addChild(nextCmdBtn);

    // ════════════════════════════════════════════════════
    // RIGHT SIDE: Editor panel
    // ════════════════════════════════════════════════════
    float editX = listX + listW + 10.f;
    float editW = ws.width - editX - 10.f;
    float editH = listH;
    float editY = listY;

    auto editPanel = paimon::SpriteHelper::createDarkPanel(editW, editH, 60);
    editPanel->setPosition({editX, editY});
    m_mainLayer->addChild(editPanel);

    auto editTitle = CCLabelBMFont::create("Command Editor", "goldFont.fnt");
    editTitle->setScale(0.35f);
    editTitle->setPosition({editX + editW / 2, ws.height - 40.f});
    m_mainLayer->addChild(editTitle);

    m_editorPanel = CCNode::create();
    m_editorPanel->setContentSize({editW, editH});
    m_editorPanel->setPosition({editX, editY});
    m_mainLayer->addChild(m_editorPanel, 5);

    float ey = editH - 10.f;
    float ecx = editW / 2.f;

    // Action selector
    auto actLbl = CCLabelBMFont::create("Action:", "bigFont.fnt");
    actLbl->setScale(0.25f);
    actLbl->setAnchorPoint({1.f, 0.5f});
    actLbl->setPosition({ecx - 55, ey});
    m_editorPanel->addChild(actLbl);

    auto actLeft = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onActionPrev));
    actLeft->setPosition({editX + ecx - 50, editY + ey});
    m_buttonMenu->addChild(actLeft);

    m_actionLabel = CCLabelBMFont::create("Wait", "bigFont.fnt");
    m_actionLabel->setScale(0.3f);
    m_actionLabel->setPosition({ecx + 10, ey});
    m_editorPanel->addChild(m_actionLabel);

    auto actRight = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onActionNext));
    actRight->setPosition({editX + ecx + 70, editY + ey});
    m_buttonMenu->addChild(actRight);

    // Target selector
    ey -= 22;
    auto tgtLbl = CCLabelBMFont::create("Target:", "bigFont.fnt");
    tgtLbl->setScale(0.25f);
    tgtLbl->setAnchorPoint({1.f, 0.5f});
    tgtLbl->setPosition({ecx - 55, ey});
    m_editorPanel->addChild(tgtLbl);

    m_targetLabel = CCLabelBMFont::create("from", "bigFont.fnt");
    m_targetLabel->setScale(0.3f);
    m_targetLabel->setColor({100, 200, 255});
    m_targetLabel->setPosition({ecx + 10, ey});
    m_editorPanel->addChild(m_targetLabel);

    auto tgtBtn = makeSmallBtn("Toggle", this, menu_selector(CustomTransitionEditorPopup::onTargetToggle));
    tgtBtn->setPosition({editX + ecx + 70, editY + ey});
    m_buttonMenu->addChild(tgtBtn);

    // Duration
    ey -= 22;
    auto durLbl = CCLabelBMFont::create("Duration:", "bigFont.fnt");
    durLbl->setScale(0.25f);
    durLbl->setAnchorPoint({1.f, 0.5f});
    durLbl->setPosition({ecx - 55, ey});
    m_editorPanel->addChild(durLbl);

    auto durDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onDurationDown));
    durDown->setPosition({editX + ecx - 50, editY + ey});
    m_buttonMenu->addChild(durDown);

    m_durationLabel = CCLabelBMFont::create("0.30s", "bigFont.fnt");
    m_durationLabel->setScale(0.28f);
    m_durationLabel->setPosition({ecx + 10, ey});
    m_editorPanel->addChild(m_durationLabel);

    auto durUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onDurationUp));
    durUp->setPosition({editX + ecx + 70, editY + ey});
    m_buttonMenu->addChild(durUp);

    // Delay
    ey -= 22;
    auto delayLbl = CCLabelBMFont::create("Delay:", "bigFont.fnt");
    delayLbl->setScale(0.25f);
    delayLbl->setAnchorPoint({1.f, 0.5f});
    delayLbl->setPosition({ecx - 55, ey});
    m_editorPanel->addChild(delayLbl);

    auto delayDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onDelayDown));
    delayDown->setPosition({editX + ecx - 50, editY + ey});
    m_buttonMenu->addChild(delayDown);

    m_delayLabel = CCLabelBMFont::create("0.00s", "bigFont.fnt");
    m_delayLabel->setScale(0.28f);
    m_delayLabel->setPosition({ecx + 10, ey});
    m_editorPanel->addChild(m_delayLabel);

    auto delayUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onDelayUp));
    delayUp->setPosition({editX + ecx + 70, editY + ey});
    m_buttonMenu->addChild(delayUp);

    // From value (opacity/scale/rotation depending on action)
    ey -= 22;
    auto fromLbl = CCLabelBMFont::create("From:", "bigFont.fnt");
    fromLbl->setScale(0.25f);
    fromLbl->setAnchorPoint({1.f, 0.5f});
    fromLbl->setPosition({ecx - 55, ey});
    m_editorPanel->addChild(fromLbl);

    auto fromDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onFromValDown));
    fromDown->setPosition({editX + ecx - 50, editY + ey});
    m_buttonMenu->addChild(fromDown);

    m_fromLabel = CCLabelBMFont::create("0", "bigFont.fnt");
    m_fromLabel->setScale(0.28f);
    m_fromLabel->setPosition({ecx + 10, ey});
    m_editorPanel->addChild(m_fromLabel);

    auto fromUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onFromValUp));
    fromUp->setPosition({editX + ecx + 70, editY + ey});
    m_buttonMenu->addChild(fromUp);

    // To value
    ey -= 22;
    auto toLbl = CCLabelBMFont::create("To:", "bigFont.fnt");
    toLbl->setScale(0.25f);
    toLbl->setAnchorPoint({1.f, 0.5f});
    toLbl->setPosition({ecx - 55, ey});
    m_editorPanel->addChild(toLbl);

    auto toDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onToValDown));
    toDown->setPosition({editX + ecx - 50, editY + ey});
    m_buttonMenu->addChild(toDown);

    m_toLabel = CCLabelBMFont::create("0", "bigFont.fnt");
    m_toLabel->setScale(0.28f);
    m_toLabel->setPosition({ecx + 10, ey});
    m_editorPanel->addChild(m_toLabel);

    auto toUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onToValUp));
    toUp->setPosition({editX + ecx + 70, editY + ey});
    m_buttonMenu->addChild(toUp);

    // Extra info label (for move: shows X/Y, for color: shows RGB, etc.)
    ey -= 22;
    m_extraLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_extraLabel->setScale(0.4f);
    m_extraLabel->setColor({200, 200, 200});
    m_extraLabel->setPosition({ecx, ey});
    m_editorPanel->addChild(m_extraLabel);

    // Move X/Y controls
    ey -= 18;
    auto fxLbl = CCLabelBMFont::create("FromX:", "bigFont.fnt");
    fxLbl->setScale(0.2f);
    fxLbl->setAnchorPoint({1.f, 0.5f});
    fxLbl->setPosition({ecx - 70, ey});
    fxLbl->setTag(100);
    m_editorPanel->addChild(fxLbl);

    auto fxDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onFromXDown));
    fxDown->setPosition({editX + ecx - 65, editY + ey});
    fxDown->setTag(101);
    m_buttonMenu->addChild(fxDown);

    auto fxUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onFromXUp));
    fxUp->setPosition({editX + ecx - 25, editY + ey});
    fxUp->setTag(102);
    m_buttonMenu->addChild(fxUp);

    auto fyLbl = CCLabelBMFont::create("FromY:", "bigFont.fnt");
    fyLbl->setScale(0.2f);
    fyLbl->setAnchorPoint({1.f, 0.5f});
    fyLbl->setPosition({ecx + 15, ey});
    fyLbl->setTag(103);
    m_editorPanel->addChild(fyLbl);

    auto fyDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onFromYDown));
    fyDown->setPosition({editX + ecx + 20, editY + ey});
    fyDown->setTag(104);
    m_buttonMenu->addChild(fyDown);

    auto fyUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onFromYUp));
    fyUp->setPosition({editX + ecx + 60, editY + ey});
    fyUp->setTag(105);
    m_buttonMenu->addChild(fyUp);

    ey -= 18;
    auto txLbl = CCLabelBMFont::create("ToX:", "bigFont.fnt");
    txLbl->setScale(0.2f);
    txLbl->setAnchorPoint({1.f, 0.5f});
    txLbl->setPosition({ecx - 70, ey});
    txLbl->setTag(110);
    m_editorPanel->addChild(txLbl);

    auto txDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onToXDown));
    txDown->setPosition({editX + ecx - 65, editY + ey});
    txDown->setTag(111);
    m_buttonMenu->addChild(txDown);

    auto txUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onToXUp));
    txUp->setPosition({editX + ecx - 25, editY + ey});
    txUp->setTag(112);
    m_buttonMenu->addChild(txUp);

    auto tyLbl = CCLabelBMFont::create("ToY:", "bigFont.fnt");
    tyLbl->setScale(0.2f);
    tyLbl->setAnchorPoint({1.f, 0.5f});
    tyLbl->setPosition({ecx + 15, ey});
    tyLbl->setTag(113);
    m_editorPanel->addChild(tyLbl);

    auto tyDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onToYDown));
    tyDown->setPosition({editX + ecx + 20, editY + ey});
    tyDown->setTag(114);
    m_buttonMenu->addChild(tyDown);

    auto tyUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onToYUp));
    tyUp->setPosition({editX + ecx + 60, editY + ey});
    tyUp->setTag(115);
    m_buttonMenu->addChild(tyUp);

    // Intensity (for Shake)
    ey -= 18;
    auto intLbl = CCLabelBMFont::create("Intensity:", "bigFont.fnt");
    intLbl->setScale(0.2f);
    intLbl->setAnchorPoint({1.f, 0.5f});
    intLbl->setPosition({ecx - 55, ey});
    intLbl->setTag(120);
    m_editorPanel->addChild(intLbl);

    auto intDown = makeArrow(true, this, menu_selector(CustomTransitionEditorPopup::onIntensityDown));
    intDown->setPosition({editX + ecx - 50, editY + ey});
    intDown->setTag(121);
    m_buttonMenu->addChild(intDown);

    auto intUp = makeArrow(false, this, menu_selector(CustomTransitionEditorPopup::onIntensityUp));
    intUp->setPosition({editX + ecx + 70, editY + ey});
    intUp->setTag(122);
    m_buttonMenu->addChild(intUp);

    // ════════════════════════════════════════════════════
    // MINI PREVIEW (Scene A -> B)
    // ════════════════════════════════════════════════════
    float prevW = editW - 20;
    float prevH = 40.f;
    float prevY = editY + 5.f;

    m_previewArea = CCNode::create();
    m_previewArea->setContentSize({prevW, prevH});
    m_previewArea->setPosition({editX + 10, prevY});
    m_mainLayer->addChild(m_previewArea, 6);

    // Scene A mini box
    m_previewFrom = CCLayerColor::create({60, 60, 180, 255}, prevW / 2 - 5, prevH);
    m_previewFrom->setPosition({0, 0});
    m_previewArea->addChild(m_previewFrom);

    auto fromLblPrev = CCLabelBMFont::create("Scene A", "chatFont.fnt");
    fromLblPrev->setScale(0.35f);
    fromLblPrev->setPosition({(prevW / 2 - 5) / 2, prevH / 2});
    m_previewFrom->addChild(fromLblPrev);

    // Arrow
    auto arrow = CCLabelBMFont::create("->", "bigFont.fnt");
    arrow->setScale(0.3f);
    arrow->setPosition({prevW / 2, prevH / 2});
    m_previewArea->addChild(arrow);

    // Scene B mini box
    m_previewTo = CCLayerColor::create({180, 60, 60, 255}, prevW / 2 - 5, prevH);
    m_previewTo->setPosition({prevW / 2 + 5, 0});
    m_previewArea->addChild(m_previewTo);

    auto toLblPrev = CCLabelBMFont::create("Scene B", "chatFont.fnt");
    toLblPrev->setScale(0.35f);
    toLblPrev->setPosition({(prevW / 2 - 5) / 2, prevH / 2});
    m_previewTo->addChild(toLblPrev);

    // ════════════════════════════════════════════════════
    // BOTTOM BUTTONS: Save, Preview, Load Preset
    // ════════════════════════════════════════════════════
    float bbY = 18.f;
    float bbX = editX + editW / 2;

    auto saveBtn = makeSmallBtn("Save", this, menu_selector(CustomTransitionEditorPopup::onSave), "GJ_button_01.png");
    saveBtn->setPosition({bbX - 80, bbY});
    m_buttonMenu->addChild(saveBtn);

    auto prevBtn = makeSmallBtn("Preview", this, menu_selector(CustomTransitionEditorPopup::onPreviewTransition));
    prevBtn->setPosition({bbX - 20, bbY});
    m_buttonMenu->addChild(prevBtn);

    auto presetBtn = makeSmallBtn("Presets", this, menu_selector(CustomTransitionEditorPopup::onLoadPreset));
    presetBtn->setPosition({bbX + 40, bbY});
    m_buttonMenu->addChild(presetBtn);

    auto imgBtn = makeSmallBtn("Image", this, menu_selector(CustomTransitionEditorPopup::onSelectImage));
    imgBtn->setPosition({bbX + 95, bbY});
    m_buttonMenu->addChild(imgBtn);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.2f);
    m_statusLabel->setColor({100, 255, 100});
    m_statusLabel->setPosition({cx, 8});
    m_mainLayer->addChild(m_statusLabel);

    // If no commands, add default ones
    if (m_commands.empty()) {
        m_commands.push_back({CommandAction::FadeOut, "from", 0.25f, 0,0,0,0, 255.f, 0.f});
        m_commands.push_back({CommandAction::FadeIn, "to", 0.25f, 0,0,0,0, 0.f, 255.f});
    }

    rebuildCommandList();
    selectCommand(0);

    paimon::markDynamicPopup(this);
    return true;
}

// ════════════════════════════════════════════════════════════
// Command list
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::rebuildCommandList() {
    m_commandListMenu->removeAllChildren();

    float cellH = 22.f;
    float totalH = cellH * static_cast<float>(m_commands.size());
    float viewH = m_scrollSize.height;
    float contentH = std::max(totalH, viewH);

    m_commandListMenu->setContentSize({m_scrollSize.width, contentH});

    for (int i = 0; i < static_cast<int>(m_commands.size()); i++) {
        auto& cmd = m_commands[i];

        float y = contentH - (i + 0.5f) * cellH;

        // Cell background
        auto bg = paimon::SpriteHelper::createColorPanel(
            m_scrollSize.width - 6.f, cellH - 2.f,
            i == m_selectedIdx ? ccColor3B{80, 120, 200} : ccColor3B{40, 40, 40},
            i == m_selectedIdx ? 180 : 100);
        bg->setPosition({(m_scrollSize.width - 6.f) / 2.f + 3.f, y - (cellH - 2.f) / 2.f});
        m_commandListMenu->addChild(bg);

        // Command index + name
        char buf[64];
        snprintf(buf, sizeof(buf), "%d. %s [%s] %.2fs",
            i + 1, actionDisplayName(cmd.action).c_str(),
            cmd.target.c_str(), cmd.duration);

        auto lbl = CCLabelBMFont::create(buf, "chatFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0, 0.5f});
        lbl->setPosition({8.f, y});
        lbl->setColor(i == m_selectedIdx ? ccColor3B{255, 255, 100} : ccColor3B{220, 220, 220});
        m_commandListMenu->addChild(lbl);
    }

    m_commandScroll->m_contentLayer->setContentSize({m_scrollSize.width, contentH});
    m_commandScroll->scrollToTop();
}

// ════════════════════════════════════════════════════════════
// Selection
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::selectCommand(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_commands.size())) {
        m_selectedIdx = -1;
    } else {
        m_selectedIdx = idx;
    }
    rebuildCommandList();
    updateEditorPanel();
}

TransitionCommand& CustomTransitionEditorPopup::selectedCmd() {
    static TransitionCommand dummy;
    if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_commands.size()))
        return m_commands[m_selectedIdx];
    return dummy;
}

// ════════════════════════════════════════════════════════════
// Editor panel display
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::updateEditorPanel() {
    if (m_selectedIdx < 0 || m_selectedIdx >= static_cast<int>(m_commands.size())) {
        m_actionLabel->setString("(none)");
        m_targetLabel->setString("-");
        m_durationLabel->setString("-");
        m_delayLabel->setString("-");
        m_fromLabel->setString("-");
        m_toLabel->setString("-");
        m_extraLabel->setString("Select a command to edit");
        return;
    }

    auto& cmd = m_commands[m_selectedIdx];

    m_actionLabel->setString(actionDisplayName(cmd.action).c_str());
    m_targetLabel->setString(cmd.target.c_str());
    m_targetLabel->setColor(cmd.target == "from" ? ccColor3B{100, 200, 255} : ccColor3B{255, 150, 100});

    char durBuf[16]; snprintf(durBuf, sizeof(durBuf), "%.2fs", cmd.duration);
    m_durationLabel->setString(durBuf);

    char delBuf[16]; snprintf(delBuf, sizeof(delBuf), "%.2fs", cmd.delay);
    m_delayLabel->setString(delBuf);

    // From/To display depends on action type
    bool showMoveXY = false;
    bool showIntensity = false;

    switch (cmd.action) {
        case CommandAction::FadeOut:
        case CommandAction::FadeIn:
        case CommandAction::EaseIn:
        case CommandAction::EaseOut:
        case CommandAction::Bounce: {
            char fb[16]; snprintf(fb, sizeof(fb), "%.0f", cmd.fromVal);
            char tb[16]; snprintf(tb, sizeof(tb), "%.0f", cmd.toVal);
            m_fromLabel->setString(fb);
            m_toLabel->setString(tb);
            m_extraLabel->setString("Opacity: 0-255");
            break;
        }
        case CommandAction::Scale: {
            char fb[16]; snprintf(fb, sizeof(fb), "%.2f", cmd.fromVal);
            char tb[16]; snprintf(tb, sizeof(tb), "%.2f", cmd.toVal);
            m_fromLabel->setString(fb);
            m_toLabel->setString(tb);
            m_extraLabel->setString("Scale factor");
            break;
        }
        case CommandAction::Rotate: {
            char fb[16]; snprintf(fb, sizeof(fb), "%.1f", cmd.fromVal);
            char tb[16]; snprintf(tb, sizeof(tb), "%.1f", cmd.toVal);
            m_fromLabel->setString(fb);
            m_toLabel->setString(tb);
            m_extraLabel->setString("Rotation degrees");
            break;
        }
        case CommandAction::Move: {
            showMoveXY = true;
            m_fromLabel->setString("-");
            m_toLabel->setString("-");
            char buf[128];
            snprintf(buf, sizeof(buf), "From(%.0f,%.0f) To(%.0f,%.0f)",
                cmd.fromX, cmd.fromY, cmd.toX, cmd.toY);
            m_extraLabel->setString(buf);
            break;
        }
        case CommandAction::Color: {
            char buf[64];
            snprintf(buf, sizeof(buf), "RGB(%d, %d, %d)", cmd.r, cmd.g, cmd.b);
            m_fromLabel->setString("-");
            m_toLabel->setString("-");
            m_extraLabel->setString(buf);
            break;
        }
        case CommandAction::Wait: {
            m_fromLabel->setString("-");
            m_toLabel->setString("-");
            m_extraLabel->setString("Pauses the timeline");
            break;
        }
        case CommandAction::Shake: {
            showIntensity = true;
            m_fromLabel->setString("-");
            m_toLabel->setString("-");
            char buf[64];
            snprintf(buf, sizeof(buf), "Shake intensity: %.1f", cmd.intensity);
            m_extraLabel->setString(buf);
            break;
        }
        case CommandAction::Image: {
            m_fromLabel->setString("-");
            m_toLabel->setString("-");
            m_extraLabel->setString(cmd.imagePath.empty() ? "No image set" : cmd.imagePath.c_str());
            break;
        }
        default: {
            m_fromLabel->setString("-");
            m_toLabel->setString("-");
            m_extraLabel->setString("");
            break;
        }
    }

    // Show/hide move XY controls
    for (int tag = 100; tag <= 115; tag++) {
        auto* n = m_editorPanel->getChildByTag(tag);
        if (n) n->setVisible(showMoveXY);
        auto* b = m_buttonMenu->getChildByTag(tag);
        if (b) b->setVisible(showMoveXY);
    }

    // Show/hide intensity controls
    for (int tag = 120; tag <= 122; tag++) {
        auto* n = m_editorPanel->getChildByTag(tag);
        if (n) n->setVisible(showIntensity);
        auto* b = m_buttonMenu->getChildByTag(tag);
        if (b) b->setVisible(showIntensity);
    }
}

void CustomTransitionEditorPopup::refreshDisplay() {
    rebuildCommandList();
    updateEditorPanel();
    updatePreviewArea();
}

// ════════════════════════════════════════════════════════════
// Command list callbacks
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::onAddCommand(CCObject*) {
    TransitionCommand cmd;
    cmd.action = CommandAction::FadeIn;
    cmd.target = "to";
    cmd.duration = 0.3f;
    cmd.fromVal = 0.f;
    cmd.toVal = 255.f;

    if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_commands.size())) {
        m_commands.insert(m_commands.begin() + m_selectedIdx + 1, cmd);
        selectCommand(m_selectedIdx + 1);
    } else {
        m_commands.push_back(cmd);
        selectCommand(static_cast<int>(m_commands.size()) - 1);
    }
}

void CustomTransitionEditorPopup::onRemoveCommand(CCObject*) {
    if (m_selectedIdx < 0 || m_selectedIdx >= static_cast<int>(m_commands.size())) return;
    if (m_commands.size() <= 1) {
        m_statusLabel->setString("Need at least 1 command");
        return;
    }
    m_commands.erase(m_commands.begin() + m_selectedIdx);
    if (m_selectedIdx >= static_cast<int>(m_commands.size()))
        m_selectedIdx = static_cast<int>(m_commands.size()) - 1;
    refreshDisplay();
}

void CustomTransitionEditorPopup::onMoveUp(CCObject*) {
    if (m_selectedIdx <= 0) return;
    std::swap(m_commands[m_selectedIdx], m_commands[m_selectedIdx - 1]);
    m_selectedIdx--;
    refreshDisplay();
}

void CustomTransitionEditorPopup::onMoveDown(CCObject*) {
    if (m_selectedIdx < 0 || m_selectedIdx >= static_cast<int>(m_commands.size()) - 1) return;
    std::swap(m_commands[m_selectedIdx], m_commands[m_selectedIdx + 1]);
    m_selectedIdx++;
    refreshDisplay();
}

void CustomTransitionEditorPopup::onDuplicateCommand(CCObject*) {
    if (m_selectedIdx < 0 || m_selectedIdx >= static_cast<int>(m_commands.size())) return;
    auto copy = m_commands[m_selectedIdx];
    m_commands.insert(m_commands.begin() + m_selectedIdx + 1, copy);
    selectCommand(m_selectedIdx + 1);
}

void CustomTransitionEditorPopup::onPrevCommand(CCObject*) {
    if (m_selectedIdx > 0)
        selectCommand(m_selectedIdx - 1);
    else if (!m_commands.empty())
        selectCommand(static_cast<int>(m_commands.size()) - 1);
}

void CustomTransitionEditorPopup::onNextCommand(CCObject*) {
    if (m_selectedIdx < static_cast<int>(m_commands.size()) - 1)
        selectCommand(m_selectedIdx + 1);
    else if (!m_commands.empty())
        selectCommand(0);
}

// ════════════════════════════════════════════════════════════
// Editor callbacks
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::onActionPrev(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& actions = allActions();
    auto& cmd = selectedCmd();
    int idx = 0;
    for (int i = 0; i < static_cast<int>(actions.size()); i++) {
        if (actions[i] == cmd.action) { idx = i; break; }
    }
    idx = (idx - 1 + static_cast<int>(actions.size())) % static_cast<int>(actions.size());
    cmd.action = actions[idx];

    // Set reasonable defaults when switching action
    switch (cmd.action) {
        case CommandAction::FadeOut: cmd.fromVal = 255; cmd.toVal = 0; break;
        case CommandAction::FadeIn:  cmd.fromVal = 0; cmd.toVal = 255; break;
        case CommandAction::Scale:   cmd.fromVal = 1; cmd.toVal = 1; break;
        case CommandAction::Rotate:  cmd.fromVal = 0; cmd.toVal = 360; break;
        case CommandAction::Shake:   cmd.intensity = 5; break;
        default: break;
    }
    refreshDisplay();
}

void CustomTransitionEditorPopup::onActionNext(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& actions = allActions();
    auto& cmd = selectedCmd();
    int idx = 0;
    for (int i = 0; i < static_cast<int>(actions.size()); i++) {
        if (actions[i] == cmd.action) { idx = i; break; }
    }
    idx = (idx + 1) % static_cast<int>(actions.size());
    cmd.action = actions[idx];

    switch (cmd.action) {
        case CommandAction::FadeOut: cmd.fromVal = 255; cmd.toVal = 0; break;
        case CommandAction::FadeIn:  cmd.fromVal = 0; cmd.toVal = 255; break;
        case CommandAction::Scale:   cmd.fromVal = 1; cmd.toVal = 1; break;
        case CommandAction::Rotate:  cmd.fromVal = 0; cmd.toVal = 360; break;
        case CommandAction::Shake:   cmd.intensity = 5; break;
        default: break;
    }
    refreshDisplay();
}

void CustomTransitionEditorPopup::onTargetToggle(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& cmd = selectedCmd();
    if (cmd.target == "from") cmd.target = "to";
    else cmd.target = "from";
    refreshDisplay();
}

void CustomTransitionEditorPopup::onDurationDown(CCObject*) {
    if (m_selectedIdx < 0) return;
    selectedCmd().duration = std::max(0.01f, selectedCmd().duration - 0.05f);
    refreshDisplay();
}

void CustomTransitionEditorPopup::onDurationUp(CCObject*) {
    if (m_selectedIdx < 0) return;
    selectedCmd().duration = std::min(5.0f, selectedCmd().duration + 0.05f);
    refreshDisplay();
}

void CustomTransitionEditorPopup::onDelayDown(CCObject*) {
    if (m_selectedIdx < 0) return;
    selectedCmd().delay = std::max(0.0f, selectedCmd().delay - 0.05f);
    updateEditorPanel();
}

void CustomTransitionEditorPopup::onDelayUp(CCObject*) {
    if (m_selectedIdx < 0) return;
    selectedCmd().delay = std::min(5.0f, selectedCmd().delay + 0.05f);
    updateEditorPanel();
}

void CustomTransitionEditorPopup::onFromValDown(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& cmd = selectedCmd();
    float step = (cmd.action == CommandAction::Scale) ? 0.1f : 10.f;
    cmd.fromVal -= step;
    updateEditorPanel();
}

void CustomTransitionEditorPopup::onFromValUp(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& cmd = selectedCmd();
    float step = (cmd.action == CommandAction::Scale) ? 0.1f : 10.f;
    cmd.fromVal += step;
    updateEditorPanel();
}

void CustomTransitionEditorPopup::onToValDown(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& cmd = selectedCmd();
    float step = (cmd.action == CommandAction::Scale) ? 0.1f : 10.f;
    cmd.toVal -= step;
    updateEditorPanel();
}

void CustomTransitionEditorPopup::onToValUp(CCObject*) {
    if (m_selectedIdx < 0) return;
    auto& cmd = selectedCmd();
    float step = (cmd.action == CommandAction::Scale) ? 0.1f : 10.f;
    cmd.toVal += step;
    updateEditorPanel();
}

// Move X/Y callbacks
void CustomTransitionEditorPopup::onFromXDown(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().fromX -= 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onFromXUp(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().fromX += 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onFromYDown(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().fromY -= 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onFromYUp(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().fromY += 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onToXDown(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().toX -= 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onToXUp(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().toX += 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onToYDown(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().toY -= 20.f; updateEditorPanel();
}
void CustomTransitionEditorPopup::onToYUp(CCObject*) {
    if (m_selectedIdx < 0) return; selectedCmd().toY += 20.f; updateEditorPanel();
}

void CustomTransitionEditorPopup::onIntensityDown(CCObject*) {
    if (m_selectedIdx < 0) return;
    selectedCmd().intensity = std::max(0.5f, selectedCmd().intensity - 0.5f);
    updateEditorPanel();
}
void CustomTransitionEditorPopup::onIntensityUp(CCObject*) {
    if (m_selectedIdx < 0) return;
    selectedCmd().intensity = std::min(50.f, selectedCmd().intensity + 0.5f);
    updateEditorPanel();
}

// ════════════════════════════════════════════════════════════
// Image selection
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::onSelectImage(CCObject*) {
    if (m_selectedIdx < 0) {
        m_statusLabel->setString("Select an Image command first");
        return;
    }

    auto& cmd = selectedCmd();
    if (cmd.action != CommandAction::Image) {
        m_statusLabel->setString("Change action to Image first");
        return;
    }

    // Open file dialog for image selection
    WeakRef<CustomTransitionEditorPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt) return;
        auto popup = self.lock();
        if (!popup) return;
        if (popup->m_selectedIdx >= 0 && popup->m_selectedIdx < static_cast<int>(popup->m_commands.size())) {
            popup->m_commands[popup->m_selectedIdx].imagePath = pathOpt->string();
            popup->m_statusLabel->setString("Image set!");
            popup->updateEditorPanel();
        }
    });
}

// ════════════════════════════════════════════════════════════
// Preview
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::onPreviewTransition(CCObject*) {
    if (m_commands.empty()) {
        m_statusLabel->setString("Add commands first!");
        return;
    }

    auto director = CCDirector::sharedDirector();
    auto winSize = director->getWinSize();

    // Build temp destination scene
    auto destScene = CCScene::create();
    auto bg = CCLayerGradient::create(
        {40, 160, 80, 255}, {80, 40, 160, 255}, {0.5f, 1.0f});
    destScene->addChild(bg);

    auto title = CCLabelBMFont::create("Custom Transition Preview", "goldFont.fnt");
    title->setPosition({winSize.width / 2, winSize.height / 2 + 20});
    title->setScale(0.6f);
    destScene->addChild(title);

    auto sub = CCLabelBMFont::create("Returning automatically...", "chatFont.fnt");
    sub->setPosition({winSize.width / 2, winSize.height / 2 - 20});
    sub->setScale(0.5f);
    destScene->addChild(sub);

    // Use a sanitized copy for preview so invalid edits never crash the editor.
    auto previewCommands = m_commands;
    validateAndSanitizeForSave(previewCommands);

    // Calculate total duration from commands
    float totalDur = 0.f;
    for (auto const& cmd : previewCommands) totalDur += cmd.duration + cmd.delay;

    // Add auto-return
    class ReturnNode : public CCNode {
    public:
        static ReturnNode* create(float d) {
            auto r = new ReturnNode();
            if (r && r->init()) { r->autorelease(); r->scheduleOnce(schedule_selector(ReturnNode::doReturn), d); return r; }
            CC_SAFE_DELETE(r); return nullptr;
        }
        void doReturn(float) {
            auto ms = CCScene::create();
            ms->addChild(MenuLayer::create());
            bool w = TransitionManager::get().isEnabled();
            TransitionManager::get().setEnabled(false);
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.3f, ms));
            TransitionManager::get().setEnabled(w);
        }
    };

    auto rn = ReturnNode::create(totalDur + 1.5f);
    if (rn) destScene->addChild(rn);

    // Create custom transition from current commands
    auto fromScene = director->getRunningScene();
    CustomTransitionScene* transScene = nullptr;
    if (fromScene && destScene && !previewCommands.empty()) {
        transScene = CustomTransitionScene::create(fromScene, destScene, previewCommands, false);
    }
    if (!transScene) {
        log::warn("[CustomTransitionEditorPopup] Preview failed: create returned nullptr");
    }

    // Keep the popup alive locally so onClose can't destroy it mid-function.
    [[maybe_unused]] Ref<CustomTransitionEditorPopup> safeSelf = this;
    this->onClose(nullptr);

    bool wasEnabled = TransitionManager::get().isEnabled();
    TransitionManager::get().setEnabled(false);
    director->replaceScene(transScene ? static_cast<CCScene*>(transScene) : destScene);
    TransitionManager::get().setEnabled(wasEnabled);
}

// ════════════════════════════════════════════════════════════
// Save
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::onSave(CCObject*) {
    if (!m_config) return;

    auto safeCommands = m_commands;
    int fixes = validateAndSanitizeForSave(safeCommands);

    m_config->commands = safeCommands;
    m_config->type = TransitionType::Custom;

    auto& tm = TransitionManager::get();
    if (m_isGlobal) {
        tm.setGlobalConfig(*m_config);
    } else {
        tm.setLevelEntryConfig(*m_config);
    }
    tm.saveConfig();

    if (fixes > 0) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Saved with %d safety fixes", fixes);
        m_statusLabel->setString(msg);
        PaimonNotify::create(msg, NotificationIcon::Warning)->show();
    } else {
        m_statusLabel->setString("Saved!");
        PaimonNotify::create("Custom transition saved!", NotificationIcon::Success)->show();
    }

    // Mirror sanitized values in editor state so UI matches persisted config.
    m_commands = safeCommands;
    refreshDisplay();
}

// ════════════════════════════════════════════════════════════
// Presets
// ════════════════════════════════════════════════════════════

void CustomTransitionEditorPopup::onLoadPreset(CCObject*) {
    // Show preset menu using a popup
    auto popup = geode::createQuickPopup(
        "Load Preset",
        "Select a preset to load:\n\n"
        "<cy>1</c> Simple Fade\n"
        "<cy>2</c> Slide Left\n"
        "<cy>3</c> Zoom Out + Fade In\n"
        "<cy>4</c> Spin Away\n"
        "<cy>5</c> Shake + Fade\n"
        "<cy>6</c> Dramatic Cinematic\n"
        "<cy>7</c> Bounce Reveal\n"
        "<cy>8</c> Glitch Out\n"
        "<cy>9</c> Scale Swap\n"
        "<cy>10</c> Slow Dissolve",
        "Cancel", "OK",
        [self = WeakRef<CustomTransitionEditorPopup>(this)](auto*, bool btn2) {
            if (!btn2) return;
            auto popup = self.lock();
            if (!popup) return;
            // Since we can't get which number was clicked, show a second popup
            // to pick the preset number
            popup->showPresetPicker();
        }
    );
}

void CustomTransitionEditorPopup::showPresetPicker() {
    // Use a series of quick popups to let user pick: page 1 (1-5) and page 2 (6-10)
    geode::createQuickPopup(
        "Choose Preset (1-5)",
        "<cy>1</c> Simple Fade\n"
        "<cy>2</c> Slide Left\n"
        "<cy>3</c> Zoom Out + Fade In\n"
        "<cy>4</c> Spin Away\n"
        "<cy>5</c> Shake + Fade",
        "More...", "1",
        [self = WeakRef<CustomTransitionEditorPopup>(this)](auto*, bool btn2) {
            auto popup = self.lock();
            if (!popup) return;
            if (btn2) {
                popup->loadPreset(1);
                return;
            }
            // Show page 2
            geode::createQuickPopup(
                "Choose Preset (2-5)",
                "<cy>2</c> Slide Left\n"
                "<cy>3</c> Zoom Out + Fade In\n"
                "<cy>4</c> Spin Away\n"
                "<cy>5</c> Shake + Fade",
                "More...", "2",
                [self](auto*, bool btn2) {
                    auto popup = self.lock();
                    if (!popup) return;
                    if (btn2) { popup->loadPreset(2); return; }
                    geode::createQuickPopup(
                        "Choose Preset (3-5)",
                        "<cy>3</c> Zoom Out + Fade In\n"
                        "<cy>4</c> Spin Away\n"
                        "<cy>5</c> Shake + Fade",
                        "More...", "3",
                        [self](auto*, bool btn2) {
                            auto popup = self.lock();
                            if (!popup) return;
                            if (btn2) { popup->loadPreset(3); return; }
                            geode::createQuickPopup(
                                "Choose Preset (4-10)",
                                "<cy>4</c> Spin Away\n"
                                "<cy>5</c> Shake + Fade\n"
                                "<cy>6</c> Dramatic Cinematic\n"
                                "<cy>7</c> Bounce Reveal",
                                "More...", "4",
                                [self](auto*, bool btn2) {
                                    auto popup = self.lock();
                                    if (!popup) return;
                                    if (btn2) { popup->loadPreset(4); return; }
                                    geode::createQuickPopup(
                                        "Choose Preset (5-10)",
                                        "<cy>5</c> Shake + Fade\n"
                                        "<cy>6</c> Dramatic Cinematic\n"
                                        "<cy>7</c> Bounce Reveal\n"
                                        "<cy>8</c> Glitch Out",
                                        "More...", "5",
                                        [self](auto*, bool btn2) {
                                            auto popup = self.lock();
                                            if (!popup) return;
                                            if (btn2) { popup->loadPreset(5); return; }
                                            geode::createQuickPopup(
                                                "Choose Preset (6-10)",
                                                "<cy>6</c> Dramatic Cinematic\n"
                                                "<cy>7</c> Bounce Reveal\n"
                                                "<cy>8</c> Glitch Out\n"
                                                "<cy>9</c> Scale Swap",
                                                "10: Dissolve", "6",
                                                [self](auto*, bool btn2) {
                                                    auto popup = self.lock();
                                                    if (!popup) return;
                                                    if (btn2) { popup->loadPreset(6); return; }
                                                    // Show remaining
                                                    geode::createQuickPopup(
                                                        "Choose Preset (7-10)",
                                                        "<cy>7</c> Bounce Reveal\n"
                                                        "<cy>8</c> Glitch Out\n"
                                                        "<cy>9</c> Scale Swap\n"
                                                        "<cy>10</c> Slow Dissolve",
                                                        "Cancel", "7",
                                                        [self](auto*, bool btn2) {
                                                            auto popup = self.lock();
                                                            if (!popup) return;
                                                            if (btn2) popup->loadPreset(7);
                                                        }
                                                    );
                                                }
                                            );
                                        }
                                    );
                                }
                            );
                        }
                    );
                }
            );
        }
    );
}

void CustomTransitionEditorPopup::loadPreset(int presetId) {
    m_commands.clear();

    auto ws = cocos2d::CCDirector::sharedDirector()->getWinSize();
    float w = ws.width;
    float h = ws.height;
    float cx = w / 2.f;
    float cy = h / 2.f;

    std::string name;

    switch (presetId) {
        case 1: // Simple Fade
            name = "Simple Fade";
            m_commands.push_back({CommandAction::FadeOut, "from", 0.3f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.3f, 0,0,0,0, 0.f, 255.f});
            break;

        case 2: // Slide Left
            name = "Slide Left";
            m_commands.push_back({CommandAction::FadeIn, "to", 0.01f, 0,0,0,0, 0.f, 255.f});
            m_commands.push_back({CommandAction::Move, "from", 0.5f, cx, cy, -w/2, cy});
            m_commands.push_back({CommandAction::Move, "to", 0.5f, w + w/2, cy, cx, cy});
            break;

        case 3: { // Zoom Out + Fade In
            name = "Zoom Out + Fade In";
            m_commands.push_back({CommandAction::Scale, "from", 0.4f, 0,0,0,0, 1.f, 0.3f});
            m_commands.push_back({CommandAction::FadeOut, "from", 0.3f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.4f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        case 4: { // Spin Away
            name = "Spin Away";
            m_commands.push_back({CommandAction::Rotate, "from", 0.6f, 0,0,0,0, 0.f, 360.f});
            m_commands.push_back({CommandAction::Scale, "from", 0.6f, 0,0,0,0, 1.f, 0.01f});
            m_commands.push_back({CommandAction::FadeOut, "from", 0.5f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.3f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        case 5: { // Shake + Fade
            name = "Shake + Fade";
            TransitionCommand shakeCmd;
            shakeCmd.action = CommandAction::Shake;
            shakeCmd.target = "from";
            shakeCmd.duration = 0.25f;
            shakeCmd.intensity = 15.f;
            m_commands.push_back(shakeCmd);
            m_commands.push_back({CommandAction::FadeOut, "from", 0.3f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.3f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        case 6: { // Dramatic Cinematic
            name = "Dramatic Cinematic";
            m_commands.push_back({CommandAction::EaseOut, "from", 0.5f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::Wait, "from", 0.3f});
            m_commands.push_back({CommandAction::EaseIn, "to", 0.5f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        case 7: { // Bounce Reveal
            name = "Bounce Reveal";
            m_commands.push_back({CommandAction::FadeOut, "from", 0.25f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::Bounce, "to", 0.5f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        case 8: { // Glitch Out
            name = "Glitch Out";
            TransitionCommand shake1;
            shake1.action = CommandAction::Shake;
            shake1.target = "from";
            shake1.duration = 0.15f;
            shake1.intensity = 20.f;
            m_commands.push_back(shake1);
            m_commands.push_back({CommandAction::FadeOut, "from", 0.1f, 0,0,0,0, 255.f, 100.f});
            TransitionCommand shake2;
            shake2.action = CommandAction::Shake;
            shake2.target = "from";
            shake2.duration = 0.1f;
            shake2.intensity = 8.f;
            m_commands.push_back(shake2);
            m_commands.push_back({CommandAction::FadeOut, "from", 0.1f, 0,0,0,0, 100.f, 0.f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.2f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        case 9: { // Scale Swap
            name = "Scale Swap";
            m_commands.push_back({CommandAction::Scale, "from", 0.35f, 0,0,0,0, 1.f, 0.5f});
            m_commands.push_back({CommandAction::FadeOut, "from", 0.2f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::Scale, "to", 0.01f, 0,0,0,0, 0.5f, 0.5f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.01f, 0,0,0,0, 0.f, 255.f});
            m_commands.push_back({CommandAction::Scale, "to", 0.35f, 0,0,0,0, 0.5f, 1.f});
            break;
        }

        case 10: { // Slow Dissolve
            name = "Slow Dissolve";
            m_commands.push_back({CommandAction::EaseOut, "from", 0.8f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::EaseIn, "to", 0.8f, 0,0,0,0, 0.f, 255.f});
            break;
        }

        default:
            name = "Simple Fade";
            m_commands.push_back({CommandAction::FadeOut, "from", 0.3f, 0,0,0,0, 255.f, 0.f});
            m_commands.push_back({CommandAction::FadeIn, "to", 0.3f, 0,0,0,0, 0.f, 255.f});
            break;
    }

    selectCommand(0);
    char msg[64];
    snprintf(msg, sizeof(msg), "Preset: %s", name.c_str());
    m_statusLabel->setString(msg);
    PaimonNotify::create(msg, NotificationIcon::Success)->show();
}

void CustomTransitionEditorPopup::updatePreviewArea() {
    // Mini preview shows timeline state
    if (m_commands.empty()) return;

    // Visualize by adjusting box opacities based on command sequence
    bool hasFadeOut = false, hasFadeIn = false;
    for (auto const& cmd : m_commands) {
        if (cmd.action == CommandAction::FadeOut && cmd.target == "from") hasFadeOut = true;
        if (cmd.action == CommandAction::FadeIn && cmd.target == "to") hasFadeIn = true;
    }

    m_previewFrom->setOpacity(hasFadeOut ? 180 : 255);
    m_previewTo->setOpacity(hasFadeIn ? 255 : 100);
}
