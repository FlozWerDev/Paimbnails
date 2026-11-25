#include "../../../utils/FilesystemPath.hpp"
#include "CustomTransitionLayer.hpp"
#include "CustomTransitionScene.hpp"
#include "TransitionObjectVisual.hpp"
#include "../services/TransitionEditing.hpp"
#include "../services/TransitionPreview.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../features/cursor/services/CursorManager.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>
#include <matjson.hpp>

using namespace geode::prelude;
using namespace paimon::transitions;
namespace ui = paimon::ui;
namespace kit = paimon::configkit;
namespace {
constexpr float timelineHeight = 96.f;
constexpr float trackHeight = 12.f;
constexpr float trackLabelWidth = 108.f;
constexpr int visibleTracks = 4;
constexpr float panelVirtualWidth = 280.f;

CCLabelBMFont* text(CCNode* parent, std::string const& value, CCPoint position, float scale,
    ccColor3B color = {235, 239, 250}, CCPoint anchor = {.0f, .5f}) {
    auto* label = CCLabelBMFont::create(value.c_str(), "chatFont.fnt");
    label->setScale(scale); label->setColor(color); label->setAnchorPoint(anchor);
    label->setPosition(position); parent->addChild(label);
    return label;
}
void fill(CCNode* parent, CCPoint position, CCSize size, ccColor4B color, int z = 0) {
    if (size.width <= 0.f || size.height <= 0.f) return;
    auto* layer = CCLayerColor::create(color, size.width, size.height);
    layer->setPosition(position); parent->addChild(layer, z);
}
CCMenuItemSpriteExtra* button(CCMenu* menu, char const* title, CCPoint position,
    float width, std::function<void()> callback, ui::Btn skin = ui::Btn::Gray) {
    auto* item = ui::makeButton(title, std::move(callback), skin, width, .47f, "bigFont.fnt");
    item->setPosition(position); menu->addChild(item);
    return item;
}
ccColor3B trackColor(TransitionCommand const& command) {
    if (command.action == CommandAction::Image) return {245, 194, 88};
    if (command.action == CommandAction::FollowCursor) return {110, 235, 180};
    if (command.targetObj >= 0) return {212, 145, 246};
    return command.target == "to" ? ccColor3B{105, 219, 157} : ccColor3B{99, 172, 245};
}
}

std::vector<CommandAction> const& CustomTransitionLayer::allActions() {
    static const std::vector<CommandAction> actions{CommandAction::Move, CommandAction::FadeOut,
        CommandAction::Rotate, CommandAction::FollowCursor, CommandAction::Scale, CommandAction::Color,
        CommandAction::Shake, CommandAction::Skew, CommandAction::FadeIn, CommandAction::Overlay,
        CommandAction::Wait, CommandAction::EaseIn, CommandAction::EaseOut, CommandAction::Bounce};
    return actions;
}
std::string CustomTransitionLayer::actionDisplayName(CommandAction action) {
    switch (action) {
        case CommandAction::Move: return "Move";
        case CommandAction::FadeOut: case CommandAction::FadeIn: return "Alpha";
        case CommandAction::Rotate: return "Rotate";
        case CommandAction::FollowCursor: return "Follow Cursor";
        case CommandAction::Scale: return "Scale";
        case CommandAction::Color: return "Color";
        case CommandAction::Shake: return "Shake";
        case CommandAction::Skew: return "Skew";
        case CommandAction::Overlay: return "Color Wash";
        case CommandAction::Wait: return "Espera";
        case CommandAction::Image: return "Objeto";
        case CommandAction::EaseIn: return "Alpha Ease In";
        case CommandAction::EaseOut: return "Alpha Ease Out";
        case CommandAction::Bounce: return "Alpha Bounce";
        case CommandAction::Spawn: return "Paralelo";
    }
    return "Trigger";
}
CCScene* CustomTransitionLayer::scene(TransitionConfig config, bool global, std::function<void(TransitionConfig)> save) {
    auto* scene = CCScene::create();
    auto* layer = create(std::move(config), global, std::move(save));
    if (!layer) return nullptr;
    scene->addChild(layer); return scene;
}
CustomTransitionLayer* CustomTransitionLayer::create(TransitionConfig config, bool global, std::function<void(TransitionConfig)> save) {
    auto* layer = new CustomTransitionLayer();
    if (layer->init(std::move(config), global, std::move(save))) { layer->autorelease(); return layer; }
    delete layer; return nullptr;
}
bool CustomTransitionLayer::init(TransitionConfig config, bool, std::function<void(TransitionConfig)> save) {
    if (!CCLayer::init()) return false;
    bool hadCommands = !config.commands.empty();
    m_config = std::move(config); m_save = std::move(save);
    TransitionManager::sanitizeConfig(m_config);
    m_commands = m_config.commands;
    if (!hadCommands && m_config.type == TransitionType::Custom) m_commands.clear();
    if (m_config.type == TransitionType::Stinger) {
        m_commands.clear();
        TransitionCommand image;
        image.action = CommandAction::Image; image.startTime = 0.f;
        image.duration = m_config.duration; image.imagePath = m_config.mediaPath;
        image.fromX = image.fromY = .5f; image.fromVal = image.toVal = 255.f;
        image.objectName = "Stinger"; image.stretchMedia = true;
        if (!image.imagePath.empty()) m_commands.push_back(image);
        m_config.sceneCut = true;
    } else if (m_commands.empty()) {
        m_config.sceneCut = true;
        m_config.duration = std::max(1.f, m_config.duration);
        TransitionCommand wait; wait.action = CommandAction::Wait; wait.startTime = 0.f;
        wait.duration = m_config.duration;
        m_commands.push_back(wait);
    }
    if (m_commands.empty()) { TransitionCommand wait; wait.startTime = 0.f; wait.duration = m_config.duration; m_commands.push_back(wait); }
    makeTransitionTimelineAbsolute(m_commands);
    m_selected = m_commands.empty() ? -1 : 0;
    if (m_selected >= 0) {
        auto const& command = m_commands[m_selected];
        m_targetObject = command.action == CommandAction::Image ? m_selected : command.targetObj;
        m_targetScene = command.target;
    }
    m_win = CCDirector::get()->getWinSize();
    m_panelWidth = std::clamp(m_win.width * .35f, 180.f, 260.f);
    m_mainTop = m_win.height - 70.f;
    setTouchEnabled(true); setTouchMode(kCCTouchesOneByOne); setKeypadEnabled(true);
    buildChrome(); refreshDocument(); rebuildPanel(); warmObjects();
    status("Guias: A/B visibles y medios semitransparentes. Play o Final = resultado real.");
    scheduleUpdate(); return true;
}
void CustomTransitionLayer::keyBackClicked() { m_playing = false; CCDirector::get()->popScene(); }
void CustomTransitionLayer::registerWithTouchDispatcher() {
    auto* dispatcher = CCDirector::get()->getTouchDispatcher();
    // canvas gestures precede the controls' delegates. ccTouchBegan only claims
    // the timeline and the stage, leaving menus, inputs and popups their clicks.
    dispatcher->addTargetedDelegate(this, dispatcher->getTargetPrio() - 10, true);
}
void CustomTransitionLayer::status(std::string const& message) {
    if (m_status) { m_status->setString(message.c_str()); m_status->limitLabelWidth(m_win.width - 24.f, .34f, .22f); }
}
float CustomTransitionLayer::totalDuration() const { return std::max(.05f, m_evaluator.timeline().duration); }
float CustomTransitionLayer::timeX(float seconds) const {
    return trackLabelWidth + seconds / m_viewDuration * (m_timelineWidth - trackLabelWidth - 8.f);
}
float CustomTransitionLayer::timeAt(float x) const {
    return std::clamp((x - trackLabelWidth) / (m_timelineWidth - trackLabelWidth - 8.f), 0.f, 1.f) * m_viewDuration;
}
float CustomTransitionLayer::trackY(int row) const { return 4.f + (visibleTracks - 1 - row) * trackHeight; }
std::string CustomTransitionLayer::commandName(int index) const {
    if (index < 0 || index >= static_cast<int>(m_commands.size())) return "Sin seleccion";
    auto const& c = m_commands[index];
    if (c.action == CommandAction::Image) return c.objectName.empty() ? fmt::format("Objeto {}", index + 1) : c.objectName;
    std::string target = c.target == "to" ? "B" : c.target == "both" ? "A+B" : "A";
    if (c.targetObj >= 0 && c.targetObj < static_cast<int>(m_commands.size())) {
        auto const& object = m_commands[c.targetObj];
        target = object.objectName.empty() ? fmt::format("Obj {}", c.targetObj + 1) : object.objectName;
    }
    return actionDisplayName(c.action) + " / " + target;
}

void CustomTransitionLayer::buildChrome() {
    fill(this, {}, m_win, {17, 21, 33, 255}, -10);
    fill(this, {0.f, m_win.height - 56.f}, {m_win.width, 56.f}, {26, 32, 49, 255});
    WeakRef<CustomTransitionLayer> self = this;
    auto* menu = CCMenu::create(); menu->setPosition({0, 0}); addChild(menu, 20);
    auto* back = ui::makeFrameButton("GJ_arrow_02_001.png", .5f, [self] { if (auto p = self.lock()) p->keyBackClicked(); });
    back->setPosition({22.f, m_win.height - 19.f}); menu->addChild(back);
    text(this, "TRANSITION STUDIO", {43.f, m_win.height - 18.f}, .54f, ui::palette::gold);
    auto* name = TextInput::create(std::max(100.f, m_win.width - 342.f), "Nombre de la transicion");
    name->setScale(.75f); name->setMaxCharCount(48); name->setString(m_config.customName);
    name->setPosition({(m_win.width + 150.f) / 2.f, m_win.height - 18.f});
    name->setCallback([self](std::string const& value) { if (auto p = self.lock()) p->m_config.customName = value; });
    addChild(name, 20); m_name = name;
    button(menu, "Aplicar", {m_win.width - 38.f, m_win.height - 18.f}, 62.f,
        [self] { if (auto p = self.lock()) p->save(); }, ui::Btn::Green);
    auto* play = button(menu, "Play", {34.f, m_win.height - 43.f}, 48.f, [self] {
        if (auto p = self.lock()) {
            if (p->m_playTime >= p->totalDuration()) p->m_playTime = 0.f;
            p->m_sceneGuides = false; p->m_playing = !p->m_playing;
        }
    }, ui::Btn::Cyan);
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(play->getNormalImage())) m_playLabel = sprite->m_label;
    button(menu, "Inicio", {88.f, m_win.height - 43.f}, 48.f, [self] {
        if (auto p = self.lock()) { p->m_playing = false; p->m_sceneGuides = true; p->m_playTime = 0.f; p->evaluate(); }
    });
    button(menu, "Vista real", {153.f, m_win.height - 43.f}, 72.f, [self] { if (auto p = self.lock()) p->previewReal(); });
    button(menu, "+ Imagen", {235.f, m_win.height - 43.f}, 74.f, [self] { if (auto p = self.lock()) p->addObject(false); }, ui::Btn::Cyan);
    button(menu, "+ Bloque", {316.f, m_win.height - 43.f}, 70.f, [self] { if (auto p = self.lock()) p->addObject(true); });
    m_clock = text(this, "", {m_win.width - 12.f, m_win.height - 43.f}, .42f, ui::palette::gold, {1.f, .5f});
    m_status = text(this, "", {12.f, m_win.height - 62.f}, .34f, ui::palette::muted);

    float workspaceWidth = m_win.width - m_panelWidth - 30.f;
    float workspaceHeight = m_mainTop - m_mainBottom - 20.f;
    float scale = std::min(workspaceWidth / m_win.width, workspaceHeight / m_win.height);
    m_stageSize = CCSize{m_win.width * scale, m_win.height * scale};
    auto* stencil = CCDrawNode::create();
    CCPoint vertices[]{{0, 0}, {m_stageSize.width, 0}, {m_stageSize.width, m_stageSize.height}, {0, m_stageSize.height}};
    stencil->drawPolygon(vertices, 4, {1, 1, 1, 1}, 0.f, {1, 1, 1, 1});
    auto* clip = CCClippingNode::create(stencil);
    clip->setContentSize(m_stageSize);
    clip->setPosition({10.f + (workspaceWidth - m_stageSize.width) / 2.f, m_mainBottom + (workspaceHeight - m_stageSize.height) / 2.f});
    m_stage = clip; addChild(clip);
    fill(m_stage, {}, m_stageSize, {4, 7, 14, 255}, -1);
    auto scenePane = [&](ccColor4B color, char const* title) {
        auto* pane = CCLayerRGBA::create();
        pane->setContentSize(m_stageSize); pane->setAnchorPoint({.5f, .5f}); pane->ignoreAnchorPointForPosition(false);
        pane->setPosition(m_stageSize / 2.f); pane->setCascadeColorEnabled(true); pane->setCascadeOpacityEnabled(true);
        fill(pane, {}, m_stageSize, color);
        fill(pane, {}, {m_stageSize.width, m_stageSize.height * .16f}, {0, 0, 0, 55});
        auto* label = text(pane, title, {m_stageSize.width / 2.f, m_stageSize.height / 2.f}, .7f, {255, 255, 255}, {.5f, .5f});
        label->limitLabelWidth(m_stageSize.width - 12.f, .7f, .3f);
        m_stage->addChild(pane); return pane;
    };
    m_sceneA = scenePane({39, 91, 156, 255}, "ESCENA A");
    m_sceneB = scenePane({40, 137, 100, 255}, "ESCENA B");
    m_wash = CCLayerColor::create({0, 0, 0, 0}, m_stageSize.width, m_stageSize.height); m_stage->addChild(m_wash, 9);
    m_outline = CCDrawNode::create(); m_stage->addChild(m_outline, 1000);
    m_guideLabels = CCNode::create(); m_stage->addChild(m_guideLabels, 2000);
    float halfWidth = m_stageSize.width / 2.f;
    fill(m_guideLabels, {0.f, m_stageSize.height - 17.f}, {halfWidth, 17.f}, {24, 62, 110, 235});
    fill(m_guideLabels, {halfWidth, m_stageSize.height - 17.f}, {halfWidth, 17.f}, {20, 91, 60, 235});
    text(m_guideLabels, "ESCENA A (sale)", {halfWidth / 2.f, m_stageSize.height - 8.f}, .36f, {255, 255, 255}, {.5f, .5f});
    text(m_guideLabels, "ESCENA B (entra)", {halfWidth * 1.5f, m_stageSize.height - 8.f}, .36f, {255, 255, 255}, {.5f, .5f});
    m_sceneBadge = text(m_stage, "", {6.f, m_stageSize.height - 8.f}, .38f, ui::palette::gold);
    m_sceneBadge->setZOrder(2001);
    button(menu, "ESCENA A", {10.f + workspaceWidth * .17f, m_mainTop - 8.f}, workspaceWidth * .32f, [self] {
        if (auto p = self.lock()) {
            p->m_playing = false; p->m_sceneGuides = true; p->m_targetObject = -1;
            p->m_targetScene = "from"; p->m_tab = 1; p->queuePanel(); p->evaluate();
        }
    }, ui::Btn::Blue);
    button(menu, "ESCENA B", {10.f + workspaceWidth * .51f, m_mainTop - 8.f}, workspaceWidth * .32f, [self] {
        if (auto p = self.lock()) {
            p->m_playing = false; p->m_sceneGuides = true; p->m_targetObject = -1;
            p->m_targetScene = "to"; p->m_tab = 1; p->queuePanel(); p->evaluate();
        }
    }, ui::Btn::Green);
    auto* preview = button(menu, "Final", {10.f + workspaceWidth * .85f, m_mainTop - 8.f}, workspaceWidth * .28f, [self] {
        if (auto p = self.lock()) {
            p->m_playing = false; p->m_sceneGuides = !p->m_sceneGuides; p->evaluate();
            p->status(p->m_sceneGuides ? "Guias: ambas escenas visibles; medios semitransparentes."
                : "Vista final: arrastra la regla o pulsa Play para probar la transicion.");
        }
    });
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(preview->getNormalImage())) m_previewLabel = sprite->m_label;
    fill(this, {m_win.width - m_panelWidth - 10.f, m_mainBottom}, {m_panelWidth, m_mainTop - m_mainBottom}, {25, 31, 46, 255});

    m_timelineWidth = m_win.width - 20.f;
    m_timeline = CCNode::create(); m_timeline->setPosition({10.f, 6.f}); addChild(m_timeline, 3);
    auto* pager = CCMenu::create(); pager->setPosition({0, 0}); m_timeline->addChild(pager, 30);
    button(pager, "<", {m_timelineWidth - 45.f, 87.f}, 22.f, [self] {
        if (auto p = self.lock()) { p->m_trackOffset = std::max(0, p->m_trackOffset - visibleTracks); p->refreshTimeline(); }
    });
    button(pager, ">", {m_timelineWidth - 16.f, 87.f}, 22.f, [self] {
        if (auto p = self.lock()) { if (p->m_trackOffset + visibleTracks < static_cast<int>(p->m_commands.size())) p->m_trackOffset += visibleTracks; p->refreshTimeline(); }
    });
    m_playhead = CCLayerColor::create({255, 114, 115, 255}, 1.5f, 75.f); m_timeline->addChild(m_playhead, 25);
}

void CustomTransitionLayer::refreshDocument() {
    TransitionManager::sanitizeCommands(m_commands);
    m_evaluator.configure(m_commands, m_config.duration);
    if (m_drag == Drag::None) m_viewDuration = std::max(.5f, totalDuration());
    m_playTime = std::clamp(m_playTime, 0.f, totalDuration());
    refreshObjects(); refreshTimeline(); evaluate();
}
void CustomTransitionLayer::refreshObjects() {
    for (auto const& view : m_objects) view.sprite->removeFromParent();
    m_objects.clear();
    for (int i = 0; i < static_cast<int>(m_commands.size()); ++i) {
        auto const& command = m_commands[i];
        if (command.action != CommandAction::Image) continue;
        ObjectView view; view.command = i;
        if (!command.solidObject) view.media = findTransitionMedia(command.imagePath);
        view.sprite = view.media ? CCSprite::create() : createSolidTransitionSprite();
        if (!view.sprite) continue;
        if (view.media) {
            view.media->apply(view.sprite, 0.f);
            view.baseScale = std::max(m_stageSize.width / view.media->width, m_stageSize.height / view.media->height) * command.objScale;
        } else {
            CCSize size = command.solidObject ? m_stageSize : CCSize{40.f, 40.f};
            view.sprite->setTextureRect({0.f, 0.f, size.width, size.height});
            view.baseScale = command.solidObject ? command.objScale : 1.f;
            if (!command.solidObject) text(view.sprite, "IMG?", size / 2.f, .42f, {255, 255, 255}, {.5f, .5f});
        }
        m_stage->addChild(view.sprite, 10 + i); m_objects.push_back(std::move(view));
    }
}
void CustomTransitionLayer::warmObjects() {
    std::set<std::string> paths;
    for (auto const& command : m_commands)
        if (command.action == CommandAction::Image && !command.solidObject && !command.imagePath.empty()) paths.insert(command.imagePath);
    WeakRef<CustomTransitionLayer> self = this;
    for (auto const& path : paths) prepareTransitionMedia(path, [self](auto media, auto error) {
        if (auto p = self.lock()) { if (!media) p->status(error); p->refreshObjects(); p->evaluate(); }
    });
}
void CustomTransitionLayer::refreshTimeline() {
    std::vector<CCNode*> old;
    for (auto* child : CCArrayExt<CCNode*>(m_timeline->getChildren())) if (child->getZOrder() < 25) old.push_back(child);
    for (auto* child : old) child->removeFromParent();
    fill(m_timeline, {}, {m_timelineWidth, timelineHeight}, {25, 31, 47, 255}, -1);
    auto* title = text(m_timeline, "LINEA DE TIEMPO  |  arrastra = inicio / borde = duracion", {6.f, 87.f}, .32f, ui::palette::muted);
    title->limitLabelWidth(m_timelineWidth - 80.f, .32f, .23f);
    for (int i = 0; i <= 8; ++i) {
        float seconds = m_viewDuration * static_cast<float>(i) / 8.f;
        float x = timeX(seconds);
        text(m_timeline, fmt::format("{:.2f}", seconds), {x, 74.f}, .27f, ui::palette::dim, {.5f, .5f});
        fill(m_timeline, {x, 2.f}, {1.f, 65.f}, {90, 107, 140, 45});
    }
    text(m_timeline, "ESCENAS A / B", {6.f, 60.f}, .31f, ui::palette::gold);
    float end = timeX(totalDuration()), cut = timeX(totalDuration() * m_config.cutPoint);
    fill(m_timeline, {timeX(0), 55.f}, {std::max(0.f, cut - timeX(0)), 11.f}, {61, 127, 205, 210});
    fill(m_timeline, {cut, 55.f}, {std::max(0.f, end - cut), 11.f}, {58, 173, 124, 210});
    if (m_config.sceneCut) {
        fill(m_timeline, {cut - 1.f, 53.f}, {2.f, 15.f}, {255, 214, 100, 255});
        text(m_timeline, "A", {timeX(0) + 4.f, 60.f}, .3f);
        if (end - cut > 10.f) text(m_timeline, "B", {cut + 4.f, 60.f}, .3f);
    }
    m_trackOffset = std::clamp(m_trackOffset, 0, std::max(0, static_cast<int>(m_commands.size()) - 1));
    for (int row = 0; row < visibleTracks; ++row) {
        int index = m_trackOffset + row;
        if (index >= static_cast<int>(m_commands.size())) break;
        auto const& command = m_commands[index];
        float y = trackY(row);
        auto color = trackColor(command);
        if (index == m_selected) fill(m_timeline, {2.f, y}, {m_timelineWidth - 4.f, trackHeight}, {72, 84, 109, 140});
        auto* label = text(m_timeline, fmt::format("{} {}", index + 1, commandName(index)), {6.f, y + 6.f}, .3f, color);
        label->limitLabelWidth(trackLabelWidth - 12.f, .3f, .22f);
        float x = timeX(std::max(0.f, command.startTime));
        float right = std::min(timeX(std::max(0.f, command.startTime) + command.duration), m_timelineWidth - 8.f);
        float width = std::max(3.f, right - x);
        fill(m_timeline, {x, y + 1.f}, {width, trackHeight - 2.f}, {color.r, color.g, color.b, static_cast<GLubyte>(command.enabled ? 220 : 60)});
        if (width > 7.f) fill(m_timeline, {x + width - 3.f, y + 2.f}, {2.f, trackHeight - 4.f}, {255, 255, 255, 180});
    }
}
void CustomTransitionLayer::evaluate() {
#if defined(GEODE_IS_MOBILE)
    auto ptr = CursorManager::get().pointerPos();
    auto mouse = (ptr.x >= 0.f && ptr.y >= 0.f)
        ? m_stage->convertToNodeSpace(ptr)
        : ccp(m_stageSize.width * 0.5f, m_stageSize.height * 0.5f);
#else
    auto mouse = m_stage->convertToNodeSpace(geode::cocos::getMousePos());
#endif
    float scale = m_stageSize.width / m_win.width;
    auto frame = m_evaluator.sample(m_playTime, {m_win.width, m_win.height},
        {std::clamp(mouse.x / scale, 0.f, m_win.width), std::clamp(mouse.y / scale, 0.f, m_win.height)},
        m_config.sceneCut ? m_config.cutPoint : -1.f);
    TransitionPreview preview({m_win.width, m_win.height}, {m_stageSize.width, m_stageSize.height}, m_sceneGuides);
    auto applyScene = [&preview](CCLayerRGBA* node, TransitionVisual const& visual, bool sceneB) {
        auto pane = preview.scene(visual, sceneB);
        applyTransitionVisual(node, pane.visual);
        node->setScaleX(pane.visual.scale * pane.widthScale);
    };
    applyScene(m_sceneA, frame.sceneA, false);
    applyScene(m_sceneB, frame.sceneB, true);
    m_guideLabels->setVisible(m_sceneGuides);
    m_sceneBadge->setVisible(!m_sceneGuides);
    bool isSceneB = frame.sceneB.opacity >= frame.sceneA.opacity;
    m_sceneBadge->setString(isSceneB ? "ESCENA B - VISTA FINAL" : "ESCENA A - VISTA FINAL");
    m_wash->setColor({static_cast<GLubyte>(frame.wash.r), static_cast<GLubyte>(frame.wash.g), static_cast<GLubyte>(frame.wash.b)});
    m_wash->setOpacity(static_cast<GLubyte>(std::clamp(frame.wash.opacity, 0.f, m_sceneGuides ? 110.f : 255.f)));
    m_outline->clear();
    for (auto const& view : m_objects) {
        auto const& command = m_commands[view.command];
        auto visual = preview.object(frame.objects[view.command]);
        if (view.media && visual.visible) {
            float time = visual.mediaElapsed;
            if (command.stretchMedia) time *= view.media->duration() / command.duration;
            view.media->apply(view.sprite, time);
        }
        applyTransitionVisual(view.sprite, visual, view.baseScale);
        if (view.command == m_selected && visual.visible) {
            auto rect = view.sprite->boundingBox();
            CCPoint vertices[]{{rect.getMinX(), rect.getMinY()}, {rect.getMaxX(), rect.getMinY()},
                {rect.getMaxX(), rect.getMaxY()}, {rect.getMinX(), rect.getMaxY()}};
            m_outline->drawPolygon(vertices, 4, {0, 0, 0, 0}, 1.f, {1.f, .82f, .36f, 1.f});
        }
    }
    m_playhead->setPosition({timeX(m_playTime), 1.f});
    m_clock->setString(fmt::format("{:.2f} / {:.2f}s", m_playTime, totalDuration()).c_str());
    if (m_playLabel) m_playLabel->setString(m_playing ? "Pausa" : "Play");
    if (m_previewLabel) m_previewLabel->setString(m_sceneGuides ? "Final" : "Guias");
}
void CustomTransitionLayer::update(float dt) {
    if (m_playing) {
        m_playTime = std::min(totalDuration(), m_playTime + bounded(dt, 0.f, 0.f, 30.f));
        if (m_playTime >= totalDuration()) { m_playing = false; m_sceneGuides = true; }
    }
    evaluate();
}
void CustomTransitionLayer::queuePanel() {
    if (m_panelQueued) return;
    m_panelQueued = true;
    WeakRef<CustomTransitionLayer> self = this;
    Loader::get()->queueInMainThread([self] { if (auto p = self.lock()) { p->m_panelQueued = false; p->rebuildPanel(); } });
}
void CustomTransitionLayer::selectCommand(int index) {
    m_selected = index >= 0 && index < static_cast<int>(m_commands.size()) ? index : -1;
    if (m_selected >= 0) {
        auto const& command = m_commands[m_selected];
        m_tab = command.action == CommandAction::Image ? 0 : 1;
        m_targetObject = command.action == CommandAction::Image ? m_selected : command.targetObj;
        m_targetScene = command.target;
        if (m_selected < m_trackOffset || m_selected >= m_trackOffset + visibleTracks) m_trackOffset = (m_selected / visibleTracks) * visibleTracks;
    }
    refreshTimeline(); evaluate(); queuePanel();
}

void CustomTransitionLayer::addObject(bool solid) {
    if (m_importing || m_commands.size() >= 256) { status("Espera a que termine la importacion (maximo 256 pistas)."); return; }
    TransitionCommand command;
    command.action = CommandAction::Image; command.startTime = std::min(m_playTime, 29.95f);
    command.duration = std::min(30.f - command.startTime, std::max(.1f, totalDuration() - m_playTime));
    command.fromX = command.fromY = .5f; command.fromVal = command.toVal = 255.f;
    command.solidObject = solid; command.objScale = solid ? .3f : 1.f;
    command.r = 105; command.g = 190; command.b = 245;
    command.objectName = solid ? "Bloque" : "Imagen";
    m_commands.push_back(command);
    refreshDocument(); selectCommand(static_cast<int>(m_commands.size()) - 1);
    if (!solid) importForObject(m_selected);
}
CustomTransitionLayer::TriggerTarget CustomTransitionLayer::newTriggerTarget() const {
    if (m_tab == 0 && m_selected >= 0 && m_selected < static_cast<int>(m_commands.size()) &&
        m_commands[m_selected].action == CommandAction::Image) return {m_selected, m_commands[m_selected].target};
    return {m_targetObject, m_targetScene};
}
void CustomTransitionLayer::addTrigger(CommandAction action, TriggerTarget target) {
    if (m_importing || m_commands.size() >= 256) return;
    TransitionCommand command;
    command.action = action; command.startTime = std::min(m_playTime, 29.95f); command.duration = std::min(.4f, 30.f - command.startTime);
    command.target = target.scene; command.targetObj = target.object;
    command.fromVal = 255.f; command.toVal = 0.f;
    if (action == CommandAction::Rotate) { command.fromVal = 0.f; command.toVal = 360.f; }
    if (action == CommandAction::Scale) { command.fromVal = 1.f; command.toVal = .5f; }
    if (action == CommandAction::FadeIn) { command.fromVal = 0.f; command.toVal = 255.f; }
    if (action == CommandAction::Move) {
        command.easing = TransitionEasing::EaseInOut;
        command.fromX = m_win.width / 2.f; command.fromY = m_win.height / 2.f;
        command.toX = m_win.width; command.toY = command.fromY;
        if (command.targetObj >= 0 && command.targetObj < static_cast<int>(m_commands.size())) {
            auto const& object = m_commands[command.targetObj];
            auto frame = m_evaluator.sample(m_playTime, {m_win.width, m_win.height}, {});
            auto const& state = frame.objects[command.targetObj];
            command.fromX = state.position.x / m_win.width; command.fromY = state.position.y / m_win.height;
            command.toX = command.fromX + .25f; command.toY = command.fromY;
            command.duration = std::min(.4f, std::max(.05f, object.startTime + object.duration - m_playTime));
        }
    }
    if (action == CommandAction::FollowCursor) command.duration = std::min(30.f - command.startTime, std::max(.1f, totalDuration() - m_playTime));
    if (action == CommandAction::Color) command.r = command.g = command.b = 255;
    m_commands.push_back(command); refreshDocument(); selectCommand(static_cast<int>(m_commands.size()) - 1);
}
void CustomTransitionLayer::importForObject(int index) {
    if (m_importing || index < 0 || index >= static_cast<int>(m_commands.size())) return;
    m_importing = true; status("Elige imagen, GIF o video...");
    WeakRef<CustomTransitionLayer> self = this;
    pt::pickMedia([self, index](Result<std::optional<std::filesystem::path>> result) {
        auto p = self.lock(); if (!p) return;
        if (!result) { p->m_importing = false; p->status("No se pudo abrir el selector."); return; }
        auto path = result.unwrap();
        if (!path) { p->m_importing = false; p->status("Importacion cancelada. Puedes elegir otro medio o borrar la pista."); return; }
        p->status("Preparando el medio...");
        auto name = paimon::paths::toUtf8(path->stem());
        prepareTransitionMedia(paimon::paths::toUtf8(*path), [self, index, name](auto media, auto error) {
            auto owner = self.lock(); if (!owner) return;
            owner->m_importing = false;
            if (!media) { owner->status(error); return; }
            if (index >= static_cast<int>(owner->m_commands.size()) || owner->m_commands[index].action != CommandAction::Image) return;
            auto& command = owner->m_commands[index];
            command.imagePath = media->manifest; command.objectName = name; command.solidObject = false;
            // a still image keeps the user's clip duration. Animation defaults to its source length.
            if (media->endsMs.size() > 1) command.duration = std::min(30.f - command.startTime, media->duration());
            owner->refreshDocument(); owner->queuePanel(); owner->status("Medio listo. Arrastralo y anade triggers a su pista.");
        });
    });
}
void CustomTransitionLayer::save() {
    if (m_importing) { status("Espera a que termine la importacion."); return; }
    if (totalDuration() > 30.f) { status("La transicion supera 30 segundos. Reduce el inicio o la duracion."); return; }
    for (auto const& command : m_commands) if (command.enabled && command.action == CommandAction::Image && !command.solidObject && !findTransitionMedia(command.imagePath)) {
        status("Falta cargar una imagen. Importa el medio antes de aplicar."); warmObjects(); return;
    }
    bool anyEnabled = std::any_of(m_commands.begin(), m_commands.end(), [](auto const& c) { return c.enabled; });
    if (!anyEnabled) { status("Activa al menos una pista antes de aplicar."); return; }
    m_config.type = TransitionType::Custom; m_config.commands = m_commands; m_config.duration = totalDuration(); m_config.scriptPath.clear();
    if (m_save) m_save(m_config);
    PaimonNotify::show("Aplicado. Pulsa Save en Transitions para guardar.", NotificationIcon::Success);
    keyBackClicked();
}
void CustomTransitionLayer::exportProject() {
    auto root = matjson::parse(TransitionManager::commandsToJson(m_commands));
    if (!root) return;
    auto value = root.unwrap(); value.set("name", m_config.customName);
    value.set("duration", static_cast<double>(totalDuration()));
    value.set("cut_point", static_cast<double>(m_config.cutPoint)); value.set("scene_cut", m_config.sceneCut);
    clipboard::write(value.dump(matjson::TAB_INDENTATION)); status("Proyecto copiado como JSON.");
}
void CustomTransitionLayer::importProject() {
    if (m_importing) return;
    auto json = clipboard::read();
    std::vector<TransitionCommand> commands;
    if (!TransitionManager::commandsFromJson(json, commands)) { status("El portapapeles no contiene una transicion valida."); return; }
    makeTransitionTimelineAbsolute(commands); m_commands = std::move(commands);
    if (auto root = matjson::parse(json); root && root.unwrap().isObject()) {
        auto const& value = root.unwrap();
        m_config.customName = value["name"].asString().unwrapOr(m_config.customName);
        m_config.duration = bounded(static_cast<float>(value["duration"].asDouble().unwrapOr(1.0)), 1.f, .05f, 30.f);
        m_config.cutPoint = bounded(static_cast<float>(value["cut_point"].asDouble().unwrapOr(.5)), .5f, 0.f, 1.f);
        m_config.sceneCut = value["scene_cut"].asBool().unwrapOr(false);
    }
    if (m_name) m_name->setString(m_config.customName);
    m_playing = false; m_playTime = 0.f; refreshDocument(); selectCommand(0); warmObjects(); status("Proyecto importado.");
}
void CustomTransitionLayer::preset(TransitionType type) {
    if (m_importing) return;
    m_commands = TransitionManager::get().buildPreviewCommands(type, std::min(3.f, totalDuration()));
    if (m_commands.empty()) {
        TransitionCommand fade;
        fade.action = CommandAction::FadeOut; fade.duration = .4f; fade.fromVal = 255.f; fade.toVal = 0.f;
        m_commands.push_back(fade);
        fade.action = CommandAction::FadeIn; fade.target = "to"; fade.fromVal = 0.f; fade.toVal = 255.f;
        m_commands.push_back(fade);
    }
    makeTransitionTimelineAbsolute(m_commands); m_config.sceneCut = false;
    m_playing = false; m_playTime = 0.f; refreshDocument(); selectCommand(0); status("Preset cargado. Cada bloque tiene su propio inicio.");
}
void CustomTransitionLayer::rebuildPanel() {
    if (m_panel) { m_panel->removeFromParent(); m_panel = nullptr; }
    constexpr float width = panelVirtualWidth;
    constexpr float inner = width - 20.f;
    WeakRef<CustomTransitionLayer> self = this;
    std::vector<CCNode*> items;
    items.push_back(kit::makeTabBar(width, {"Objeto", "Triggers", "Proyecto"}, m_tab, [self](int tab) {
        if (auto p = self.lock()) { p->m_tab = tab; p->queuePanel(); }
    }));
    auto hint = [&](std::string const& message) { items.push_back(kit::makeHint(width, message.c_str())); };
    auto row = [&](char const* title, char const* description, char const* caption, std::function<void()> callback, ui::Btn skin = ui::Btn::Gray) {
        items.push_back(kit::makeButtonRow(width, title, description, caption, std::move(callback), skin));
    };
    // the selected index is captured so a queued UI refresh cannot edit another track.
    int selected = m_selected;
    bool valid = selected >= 0 && selected < static_cast<int>(m_commands.size());
    auto change = [self, selected](std::function<void(TransitionCommand&)> edit) {
        auto p = self.lock(); if (!p || selected < 0 || selected >= static_cast<int>(p->m_commands.size())) return;
        edit(p->m_commands[selected]); p->refreshDocument();
    };
    if (valid && m_tab != 2) {
        auto* timing = CCNode::create(); timing->setContentSize({width, 43.f});
        auto field = [&](char const* title, float x, float value, bool duration) {
            text(timing, title, {x, 35.f}, .34f, ui::palette::gold);
            auto* input = TextInput::create(120.f, "0.00", "chatFont.fnt");
            input->setFilter("0123456789."); input->setMaxCharCount(7);
            input->setString(fmt::format("{:.3f}", value));
            input->setPosition({x + 60.f, 15.f}); timing->addChild(input);
            input->setCallback([self, selected, duration](std::string const& text) {
                float seconds = 0.f;
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
                if (error != std::errc{} || end != text.data() + text.size() || !finite(seconds)) return;
                auto p = self.lock(); if (!p || selected < 0 || selected >= static_cast<int>(p->m_commands.size())) return;
                auto& command = p->m_commands[selected];
                if (duration) command.duration = std::clamp(seconds, .01f, 30.f);
                else command.startTime = std::clamp(seconds, 0.f, 29.99f);
                p->refreshDocument();
            });
        };
        field("INICIO (s)", 6.f, m_commands[selected].startTime, false);
        field("DURACION (s)", 148.f, m_commands[selected].duration, true);
        items.push_back(timing);
    }
    auto slider = [&](char const* title, char const* description, double value, double low, double high,
        std::function<void(TransitionCommand&, float)> edit, char const* suffix = "") {
        items.push_back(kit::makeSliderRow(width, title, description, value, low, high,
            [suffix](double v) { return fmt::format("{:.2f}{}", v, suffix); },
            [change, edit](double v) { change([edit, v](auto& c) { edit(c, static_cast<float>(v)); }); }));
    };
    auto quickTriggers = [&] {
        auto* strip = CCNode::create(); strip->setContentSize({width, 36.f});
        auto target = newTriggerTarget();
        std::string name = target.object >= 0 ? commandName(target.object) :
            target.scene == "to" ? "Escena B" : target.scene == "both" ? "Ambas escenas" : "Escena A";
        auto* caption = text(strip, "TRIGGERS PARA: " + name, {6.f, 30.f}, .34f, ui::palette::gold);
        caption->limitLabelWidth(width - 12.f, .34f, .24f);
        auto* menu = CCMenu::create(); menu->setPosition({0, 0}); strip->addChild(menu);
        std::array<std::pair<char const*, CommandAction>, 4> actions{{{"Move", CommandAction::Move}, {"Alpha", CommandAction::FadeOut},
            {"Rotate", CommandAction::Rotate}, {"Cursor", CommandAction::FollowCursor}}};
        for (int i = 0; i < 4; ++i) button(menu, actions[i].first, {35.f + i * 70.f, 10.f}, 62.f,
            [self, action = actions[i].second, target] { if (auto p = self.lock()) p->addTrigger(action, target); }, ui::Btn::Cyan);
        items.push_back(strip);
    };
    if (m_tab == 0) {
        std::vector<std::string> names;
        std::vector<int> indices;
        int chosen = 0;
        for (int i = 0; i < static_cast<int>(m_commands.size()); ++i) if (m_commands[i].action == CommandAction::Image) {
            if (i == selected) chosen = static_cast<int>(indices.size());
            indices.push_back(i); names.push_back(commandName(i));
        }
        if (!indices.empty()) items.push_back(kit::makeSelectRow(width, "Objetos", "Tambien puedes elegirlos en el lienzo o su pista.", names, chosen,
            [self, indices](int i) { if (auto p = self.lock(); p && i >= 0 && i < static_cast<int>(indices.size())) p->selectCommand(indices[i]); }));
        if (!valid || m_commands[selected].action != CommandAction::Image) {
            hint("Usa + Imagen para PNG, JPG, GIF o video. + Bloque crea un objeto de color. Selecciona una pista para editarla.");
        } else {
            auto const& c = m_commands[selected];
            quickTriggers();
            auto* nameRow = CCNode::create(); nameRow->setContentSize({width, 34.f});
            auto* name = TextInput::create(inner, "Nombre del objeto"); name->setMaxCharCount(48); name->setString(c.objectName);
            name->setPosition({width / 2.f, 17.f}); nameRow->addChild(name);
            name->setCallback([self, selected](std::string const& value) {
                if (auto p = self.lock(); p && selected < static_cast<int>(p->m_commands.size())) { p->m_commands[selected].objectName = value; p->refreshTimeline(); }
            }); items.push_back(nameRow);
            slider("Posicion X", "Porcentaje horizontal. Tambien puedes arrastrar.", c.fromX * 100.f, 0, 100, [](auto& c, float v) { c.fromX = v / 100.f; }, "%");
            slider("Posicion Y", "0 = abajo; 100 = arriba.", c.fromY * 100.f, 0, 100, [](auto& c, float v) { c.fromY = v / 100.f; }, "%");
            slider("Tamano", "1 = cubre el lienzo; menor = objeto pequeno.", c.objScale, .05, 4.0, [](auto& c, float v) { c.objScale = v; }, "x");
            slider("Alpha base", "0 = invisible; 255 = opaco. Los triggers lo animan.", c.fromVal, 0, 255, [](auto& c, float v) { c.fromVal = c.toVal = v; });
            if (c.solidObject) items.push_back(kit::makeColorRow(width, "Color del bloque", "Color del objeto sin un trigger de tinte.",
                {static_cast<GLubyte>(c.r), static_cast<GLubyte>(c.g), static_cast<GLubyte>(c.b)},
                [change](ccColor3B color) { change([color](auto& c) { c.r = color.r; c.g = color.g; c.b = color.b; }); }));
            row("Medio", c.solidObject ? "Convertir el bloque en una imagen o video." : "Cambiar la imagen, GIF o video de este objeto.", "Importar",
                [self, selected] { if (auto p = self.lock()) p->importForObject(selected); }, ui::Btn::Cyan);
            if (!c.solidObject) items.push_back(kit::makeToggleRow(width, "Ajustar video al clip", "Estira la animacion a la duracion del objeto.", c.stretchMedia,
                [change](bool v) { change([v](auto& c) { c.stretchMedia = v; }); }));
        }
    } else if (m_tab == 1) {
        std::vector<std::string> names{"Escena A", "Escena B", "Ambas escenas"};
        std::vector<int> objects;
        int target = m_targetScene == "to" ? 1 : m_targetScene == "both" ? 2 : 0;
        for (int i = 0; i < static_cast<int>(m_commands.size()); ++i) if (m_commands[i].action == CommandAction::Image) {
            if (m_targetObject == i) target = static_cast<int>(names.size());
            names.push_back(commandName(i)); objects.push_back(i);
        }
        items.push_back(kit::makeSelectRow(width, "Destino del trigger", "Elige una escena o un objeto importado.", names, target,
            [self, objects](int index) {
                if (auto p = self.lock()) {
                    p->m_targetObject = index >= 3 && index - 3 < static_cast<int>(objects.size()) ? objects[index - 3] : -1;
                    p->m_targetScene = index == 1 ? "to" : index == 2 ? "both" : "from";
                    if (p->m_selected >= 0 && p->m_commands[p->m_selected].action != CommandAction::Image) {
                        auto& c = p->m_commands[p->m_selected]; bool changedUnits = (c.targetObj >= 0) != (p->m_targetObject >= 0);
                        c.targetObj = p->m_targetObject; c.target = p->m_targetScene;
                        if (changedUnits && c.action == CommandAction::Move) {
                            float x = c.targetObj >= 0 ? 1.f / p->m_win.width : p->m_win.width;
                            float y = c.targetObj >= 0 ? 1.f / p->m_win.height : p->m_win.height;
                            c.fromX *= x; c.toX *= x; c.fromY *= y; c.toY *= y;
                        }
                        p->refreshDocument(); p->queuePanel();
                    }
                }
            }));
        quickTriggers();
        if (valid && m_commands[selected].action != CommandAction::Image) {
            auto const& c = m_commands[selected];
            std::vector<std::string> names; int chosen = 0;
            for (int i = 0; i < static_cast<int>(allActions().size()); ++i) { names.push_back(actionDisplayName(allActions()[i])); if (allActions()[i] == c.action) chosen = i; }
            items.push_back(kit::makeSelectRow(width, "Tipo de trigger", "Propiedad que anima esta pista.", names, chosen,
                [self, change](int i) { if (i < 0 || i >= static_cast<int>(allActions().size())) return;
                    change([i](auto& c) { c.action = allActions()[i];
                        if (c.action == CommandAction::Scale) { c.fromVal = 1.f; c.toVal = .5f; }
                        if (c.action == CommandAction::Rotate) { c.fromVal = 0.f; c.toVal = 360.f; }
                    }); if (auto p = self.lock()) p->queuePanel(); }));
            if (c.action == CommandAction::Move) {
                float w = c.targetObj >= 0 ? 1.f : m_win.width, h = c.targetObj >= 0 ? 1.f : m_win.height;
                slider("Desde X", "Porcentaje de la pantalla.", c.fromX / w * 100, -100, 200, [w](auto& c, float v) { c.fromX = v * w / 100.f; }, "%");
                slider("Desde Y", "Porcentaje de la pantalla.", c.fromY / h * 100, -100, 200, [h](auto& c, float v) { c.fromY = v * h / 100.f; }, "%");
                slider("Hasta X", "Destino horizontal.", c.toX / w * 100, -100, 200, [w](auto& c, float v) { c.toX = v * w / 100.f; }, "%");
                slider("Hasta Y", "Destino vertical.", c.toY / h * 100, -100, 200, [h](auto& c, float v) { c.toY = v * h / 100.f; }, "%");
            } else if (c.action == CommandAction::FollowCursor) {
                hint("El objeto sigue al cursor solo entre el inicio y el final de este trigger. Al terminar conserva la ultima posicion.");
            } else if (c.action != CommandAction::Wait && c.action != CommandAction::Color && c.action != CommandAction::Shake) {
                bool scale = c.action == CommandAction::Scale;
                bool angle = c.action == CommandAction::Rotate || c.action == CommandAction::Skew;
                double low = scale ? .01 : angle ? -720 : 0, high = scale ? 5 : angle ? 720 : 255;
                slider(angle ? "Grados inicio" : scale ? "Escala inicio" : "Alpha inicio", "Valor al comenzar el trigger.", c.fromVal, low, high, [](auto& c, float v) { c.fromVal = v; });
                slider(angle ? "Grados fin" : scale ? "Escala fin" : "Alpha fin", "Valor al terminar el trigger.", c.toVal, low, high, [](auto& c, float v) { c.toVal = v; });
            }
            if (c.action == CommandAction::Shake) slider("Intensidad", "Fuerza del movimiento.", c.intensity, .5, 50, [](auto& c, float v) { c.intensity = v; });
            if (c.action == CommandAction::Color || c.action == CommandAction::Overlay) items.push_back(kit::makeColorRow(width, "Color", "Tinte del trigger.",
                {static_cast<GLubyte>(c.r), static_cast<GLubyte>(c.g), static_cast<GLubyte>(c.b)},
                [change](ccColor3B color) { change([color](auto& c) { c.r = color.r; c.g = color.g; c.b = color.b; }); }));
            std::vector<std::string> easings; int easing = 0;
            for (int i = 0; i < static_cast<int>(TransitionManager::allEasings().size()); ++i) { auto e = TransitionManager::allEasings()[i]; easings.push_back(TransitionManager::easingDisplayName(e)); if (e == c.easing) easing = i; }
            items.push_back(kit::makeSelectRow(width, "Easing", "Curva del movimiento o del efecto.", easings, easing,
                [change](int i) { if (i >= 0 && i < static_cast<int>(TransitionManager::allEasings().size())) change([i](auto& c) { c.easing = TransitionManager::allEasings()[i]; }); }));
        }
    } else {
        items.push_back(kit::makeToggleRow(width, "Corte de escena A / B", "Cambia la escena al pasar por la marca amarilla.", m_config.sceneCut,
            [self](bool enabled) { if (auto p = self.lock()) { p->m_config.sceneCut = enabled; p->refreshDocument(); } }));
        items.push_back(kit::makeSliderRow(width, "Punto de corte", "Tambien puedes arrastrar la marca A/B en la pista.", m_config.cutPoint * 100.f, 0, 100,
            [](double v) { return fmt::format("{:.1f}%", v); }, [self](double value) { if (auto p = self.lock()) { p->m_config.cutPoint = static_cast<float>(value / 100.0); p->refreshTimeline(); p->evaluate(); } }));
        items.push_back(kit::makeSliderRow(width, "Duracion del proyecto", "El final incluye tambien todos los clips activos (max. 30s).", m_config.duration, .1, 30,
            [](double v) { return fmt::format("{:.2f}s", v); }, [self](double v) { if (auto p = self.lock()) { p->m_config.duration = static_cast<float>(v); p->refreshDocument(); } }));
        row("Exportar proyecto", "Copia objetos, triggers, tiempos y corte en JSON.", "Copiar", [self] { if (auto p = self.lock()) p->exportProject(); }, ui::Btn::Green);
        row("Importar proyecto", "Reemplaza el proyecto con el JSON del portapapeles.", "Pegar", [self] { if (auto p = self.lock()) p->importProject(); }, ui::Btn::Cyan);
        row("Preset Fade", "Reemplaza las pistas con un fade editable.", "Cargar", [self] { if (auto p = self.lock()) p->preset(TransitionType::Fade); });
        row("Preset Spin", "Reemplaza las pistas con giro y fade.", "Cargar", [self] { if (auto p = self.lock()) p->preset(TransitionType::SpinCW); });
        row("Preset Glitch", "Reemplaza las pistas con un efecto glitch.", "Cargar", [self] { if (auto p = self.lock()) p->preset(TransitionType::Glitch); });
    }
    if (valid && m_tab != 2) {
        auto const& c = m_commands[selected];
        hint(fmt::format("PISTA {}: {}", selected + 1, commandName(selected)));
        slider("Inicio", "Segundos desde el inicio de la transicion.", c.startTime, 0, 29.95, [](auto& c, float v) { c.startTime = v; }, "s");
        slider("Duracion", "Tambien: arrastra el borde derecho del bloque.", c.duration, .05, 30, [](auto& c, float v) { c.duration = v; }, "s");
        items.push_back(kit::makeToggleRow(width, "Pista activa", "Los objetos desactivados tampoco reciben triggers.", c.enabled,
            [change](bool enabled) { change([enabled](auto& c) { c.enabled = enabled; }); }));
        row("Duplicar pista", "Mantiene el mismo inicio para editar en paralelo.", "Copiar", [self, selected] {
            if (auto p = self.lock(); p && !p->m_importing && p->m_commands.size() < 256) {
                auto copy = p->m_commands[selected]; insertTransitionCommand(p->m_commands, selected + 1, copy);
                p->refreshDocument(); p->selectCommand(selected + 1);
            }
        });
        row("Subir pista", "Cambia el orden y mantiene los destinos de los triggers.", "Subir", [self, selected] {
            if (auto p = self.lock(); p && !p->m_importing && selected > 0) { moveTransitionCommand(p->m_commands, selected, selected - 1); p->refreshDocument(); p->selectCommand(selected - 1); }
        });
        row("Bajar pista", "Tambien cambia el orden de superposicion de objetos.", "Bajar", [self, selected] {
            if (auto p = self.lock(); p && !p->m_importing && selected + 1 < static_cast<int>(p->m_commands.size())) { moveTransitionCommand(p->m_commands, selected, selected + 1); p->refreshDocument(); p->selectCommand(selected + 1); }
        });
        row("Borrar pista", "Borrar un objeto tambien borra sus triggers.", "Borrar", [self, selected] {
            if (auto p = self.lock(); p && !p->m_importing) {
                eraseTransitionCommand(p->m_commands, selected);
                if (p->m_commands.empty()) { TransitionCommand wait; wait.startTime = 0.f; wait.duration = p->m_config.duration; p->m_commands.push_back(wait); }
                p->m_targetObject = -1; p->refreshDocument(); p->selectCommand(std::min(selected, static_cast<int>(p->m_commands.size()) - 1));
            }
        }, ui::Btn::Red);
    }
    float scale = m_panelWidth / panelVirtualWidth;
    m_panel = kit::makeScrollStack({width, (m_mainTop - m_mainBottom) / scale}, items, 6.f);
    layoutPropertyPanel(); addChild(m_panel, 4);
}

void CustomTransitionLayer::layoutPropertyPanel() {
    // cCLayer's default ignored center anchor shifts its origin when scaled.
    m_panel->setAnchorPoint({0.f, 0.f});
    m_panel->ignoreAnchorPointForPosition(false);
    m_panel->setScale(m_panelWidth / panelVirtualWidth);
    m_panel->setPosition({m_win.width - m_panelWidth - 10.f, m_mainBottom});
}

bool CustomTransitionLayer::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto point = touch->getLocation();
    auto local = m_timeline->convertToNodeSpace(point);
    if (local.x >= 0.f && local.x <= m_timelineWidth && local.y >= 0.f && local.y <= timelineHeight) {
        if (local.y > 81.f) return false; // title and track paging buttons
        m_playing = false; m_dragRange = m_viewDuration;
        if (local.y >= 68.f) {
            m_drag = Drag::Scrub; m_playTime = timeAt(local.x); evaluate(); return true;
        }
        if (local.y >= 53.f) {
            if (m_config.sceneCut && local.x >= trackLabelWidth) {
                m_drag = Drag::Cut; m_config.cutPoint = std::clamp(timeAt(local.x) / totalDuration(), 0.f, 1.f);
                refreshTimeline(); evaluate();
            } else { m_tab = 2; queuePanel(); }
            return true;
        }
        for (int row = 0; row < visibleTracks; ++row) {
            int index = m_trackOffset + row;
            if (index >= static_cast<int>(m_commands.size()) || local.y < trackY(row) || local.y >= trackY(row) + trackHeight) continue;
            selectCommand(index);
            auto const& command = m_commands[index];
            float x = timeX(command.startTime), end = timeX(command.startTime + command.duration);
            if (local.x < x || local.x > end || local.x < trackLabelWidth) return true;
            m_drag = local.x >= end - 5.f ? Drag::Resize : Drag::Clip;
            m_dragX = point.x; m_dragStart = command.startTime; m_dragDuration = command.duration;
            return true;
        }
        return false;
    }
    auto stagePoint = m_stage->convertToNodeSpace(point);
    if (!CCRect{0.f, 0.f, m_stageSize.width, m_stageSize.height}.containsPoint(stagePoint)) return false;
    for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it) {
        auto* sprite = it->sprite;
        if (!sprite->isVisible()) continue;
        auto size = sprite->getContentSize();
        if (CCRect{0, 0, size.width, size.height}.containsPoint(sprite->convertToNodeSpace(point))) {
            m_playing = false; m_drag = Drag::Object; m_dragPoint = stagePoint; selectCommand(it->command); return true;
        }
    }
    return false;
}
void CustomTransitionLayer::ccTouchMoved(CCTouch* touch, CCEvent*) {
    auto point = touch->getLocation();
    if (m_drag == Drag::Scrub) { m_playTime = timeAt(m_timeline->convertToNodeSpace(point).x); evaluate(); return; }
    if (m_drag == Drag::Cut) {
        m_config.cutPoint = std::clamp(timeAt(m_timeline->convertToNodeSpace(point).x) / totalDuration(), 0.f, 1.f);
        refreshTimeline(); evaluate(); return;
    }
    if (m_selected < 0 || m_selected >= static_cast<int>(m_commands.size())) return;
    if (m_drag == Drag::Clip || m_drag == Drag::Resize) {
        float delta = (point.x - m_dragX) / (m_timelineWidth - trackLabelWidth - 8.f) * m_dragRange;
        auto& command = m_commands[m_selected];
        if (m_drag == Drag::Resize) command.duration = std::clamp(m_dragDuration + delta, .05f, std::max(.05f, 30.f - command.startTime));
        else command.startTime = std::clamp(m_dragStart + delta, 0.f, std::max(0.f, 30.f - command.duration));
        refreshDocument(); return;
    }
    if (m_drag == Drag::Object) {
        auto local = m_stage->convertToNodeSpace(point);
        float dx = (local.x - m_dragPoint.x) / m_stageSize.width, dy = (local.y - m_dragPoint.y) / m_stageSize.height;
        auto& command = m_commands[m_selected];
        float x = std::clamp(command.fromX + dx, 0.f, 1.f), y = std::clamp(command.fromY + dy, 0.f, 1.f);
        dx = x - command.fromX; dy = y - command.fromY; command.fromX = x; command.fromY = y;
        for (auto& trigger : m_commands) if (trigger.targetObj == m_selected && trigger.action == CommandAction::Move) {
            trigger.fromX += dx; trigger.toX += dx; trigger.fromY += dy; trigger.toY += dy;
        }
        m_dragPoint = local; refreshDocument();
    }
}
void CustomTransitionLayer::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_drag == Drag::None) return;
    m_drag = Drag::None; m_viewDuration = std::max(.5f, totalDuration()); refreshTimeline(); evaluate(); queuePanel();
}
void CustomTransitionLayer::ccTouchCancelled(CCTouch* touch, CCEvent* event) { ccTouchEnded(touch, event); }
void CustomTransitionLayer::previewReal() {
    if (m_importing) { status("Espera a que termine la importacion."); return; }
    m_playing = false;
    auto* director = CCDirector::get();
    auto* destination = CCScene::create();
    auto* background = CCLayerGradient::create({35, 150, 110, 255}, {25, 65, 100, 255}); destination->addChild(background);
    text(destination, "ESCENA B", m_win / 2.f, 1.f, {255, 255, 255}, {.5f, .5f});
    class ReturnToStudio : public CCNode {
    public:
        static ReturnToStudio* create() {
            auto* node = new ReturnToStudio(); if (node->init()) { node->autorelease(); return node; } delete node; return nullptr;
        }
        void onEnterTransitionDidFinish() override { CCNode::onEnterTransitionDidFinish(); scheduleOnce(schedule_selector(ReturnToStudio::returnToStudio), 1.f); }
        void onExit() override { unscheduleAllSelectors(); CCNode::onExit(); }
        void returnToStudio(float) { if (CCDirector::get()->getRunningScene() == getParent()) CCDirector::get()->popScene(); }
    };
    if (auto* back = ReturnToStudio::create()) destination->addChild(back);
    auto* transition = CustomTransitionScene::create(director->getRunningScene(), destination, m_commands, true,
        m_config.sceneCut ? m_config.cutPoint : -1.f, m_config.duration);
    if (!transition) { status("Medio pendiente o duracion mayor a 30s. Revisa las pistas."); warmObjects(); return; }
    director->pushScene(transition);
}
