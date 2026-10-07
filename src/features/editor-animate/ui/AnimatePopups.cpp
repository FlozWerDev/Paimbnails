#include "AnimatePopups.hpp"

#include "AnimateWidgets.hpp"
#include "../AnimateText.hpp"
#include "../services/AnimateCompiler.hpp"
#include "../services/AnimatePlanner.hpp"
#include "../services/AnimateSession.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/general.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>

using namespace geode::prelude;

namespace paimon::animate {

namespace {

namespace w = widgets;
namespace kit = paimon::configkit;

constexpr char const* kNameChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.!?#()";

AnimateSession& session() {
    return AnimateSession::get();
}

int childPriority() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

std::string trimmed(std::string text) {
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

void later(std::function<void()> fn) {
    Loader::get()->queueInMainThread([fn = std::move(fn)] {
        if (paimon::isRuntimeShuttingDown()) return;
        fn();
    });
}

// nested popups share z 105 otherwise and the newest can draw underneath.
void showOnTop(FLAlertLayer* alert) {
    if (!alert) return;
    int top = 105;
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            top = std::max(top, child->getZOrder());
        }
    }
    alert->m_ZOrder = top + 1;
    alert->show();
    if (auto* parent = alert->getParent()) parent->reorderChild(alert, top + 1);
}

// a rebuild under another popup registers its menus at that popup's forced priority.
bool isTopPopup(CCNode* popup) {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene || !popup) return false;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (child == popup || !typeinfo_cast<FLAlertLayer*>(child)) continue;
        if (child->getZOrder() >= popup->getZOrder()) return false;
    }
    return true;
}

bool hasPopup(std::string const& id) {
    auto* scene = CCDirector::get()->getRunningScene();
    return scene && scene->getChildByID(id);
}

std::string statusText(Clip const& clip) {
    if (!clip.compiled.present()) return tr("Not baked", "Sin hornear");
    return clip.compiled.stale ? tr("Outdated", "Desactualizado") : tr("Baked", "Horneado");
}

ccColor3B statusColor(Clip const& clip) {
    if (!clip.compiled.present()) return ui::palette::dim;
    return clip.compiled.stale ? ui::palette::warning : ui::palette::success;
}

std::string clipStats(Clip const& clip) {
    auto const total = sequenceDuration(buildSequence(clip));
    return fmt::format("{} {}  |  {}  |  {:g} fps  |  {}", clip.frames.size(),
        tr("frames", "frames"), w::seconds(total), clip.settings.fps, w::modeName(clip.settings.mode));
}

cocos2d::CCPoint viewCenter() {
    auto const win = CCDirector::get()->getWinSize();
    auto* editor = session().editor();
    if (!editor || !editor->m_objectLayer) return {0.f, 0.f};
    return editor->m_objectLayer->convertToNodeSpace(win / 2.f);
}

struct Row {
    CCNode* node = nullptr;
    NineSlice* inset = nullptr;
    CCMenu* menu = nullptr;
};

Row makeRow(float width, float height, char const* title, char const* desc, GLubyte alpha = 55) {
    Row row;
    row.node = CCNode::create();
    row.node->setAnchorPoint({0.f, 0.f});
    row.node->setContentSize({width, height});
    row.inset = ui::makeInset({width, height}, alpha);
    row.node->addChild(row.inset, -1);
    if (title) {
        auto* label = ui::makeLabel(title, width * 0.4f, 0.36f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({10.f, desc ? height / 2.f + 6.f : height / 2.f});
        row.node->addChild(label);
    }
    if (desc) {
        auto* sub = ui::makeText(desc, width * 0.42f, 0.42f);
        sub->setAnchorPoint({0.f, 0.5f});
        sub->setPosition({10.f, height / 2.f - 7.f});
        row.node->addChild(sub);
    }
    row.menu = CCMenu::create();
    row.menu->setPosition({0.f, 0.f});
    row.menu->setTouchPriority(childPriority());
    row.node->addChild(row.menu, 5);
    return row;
}


class PromptPopup : public PaimonPopup {
public:
    using Callback = std::function<void(std::string const&)>;

    static PromptPopup* create(std::string const& title, std::string const& hint,
        std::string const& initial, std::string const& filter, int maxChars, Callback cb) {
        auto* ret = new PromptPopup();
        if (ret->init(title, hint, initial, filter, maxChars, std::move(cb))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    bool init(std::string const& title, std::string const& hint, std::string const& initial,
        std::string const& filter, int maxChars, Callback cb) {
        if (!PaimonPopup::init(330.f, 168.f)) return false;
        m_onConfirm = std::move(cb);
        setTitle(title.c_str());
        setID("animate-prompt"_spr);

        auto const size = m_mainLayer->getContentSize();
        auto* text = ui::makeText(hint.c_str(), size.width - 40.f, 0.5f, ui::palette::muted,
            kCCTextAlignmentCenter);
        text->setPosition({size.width / 2.f, size.height - 52.f});
        m_mainLayer->addChild(text);

        auto* inset = ui::makeInset({size.width - 40.f, 40.f}, 90);
        inset->setPosition({20.f, size.height / 2.f - 30.f});
        m_mainLayer->addChild(inset);

        m_input = TextInput::create(size.width - 56.f, tr("Type here", "Escribe aqui"));
        m_input->setPosition({size.width / 2.f, size.height / 2.f - 10.f});
        m_input->setMaxCharCount(static_cast<std::size_t>(std::max(1, maxChars)));
        if (!filter.empty()) m_input->setFilter(filter);
        m_input->setString(initial);
        m_mainLayer->addChild(m_input, 1);

        WeakRef<PromptPopup> self = this;
        auto* ok = ui::makeButton(tr("Accept", "Aceptar"), [self] {
            auto popup = self.lock();
            if (!popup) return;
            auto const value = trimmed(popup->m_input ? popup->m_input->getString() : "");
            if (value.empty()) {
                w::notifyError(tr("Write something first.", "Primero escribe algo."));
                return;
            }
            auto callback = popup->m_onConfirm;
            popup->onClose(nullptr);
            later([callback, value] {
                if (callback) callback(value);
            });
        }, ui::Btn::Green, 0.f, 0.7f);
        ok->setPosition({size.width / 2.f, 24.f});
        m_buttonMenu->addChild(ok);
        return true;
    }

    void onClose(CCObject* sender) override {
        paimon::ui::detachGeodeTextInput(m_input);
        m_input = nullptr;
        Popup::onClose(sender);
    }

    TextInput* m_input = nullptr;
    Callback m_onConfirm;
};


class CenterPopup : public PaimonPopup {
public:
    static CenterPopup* create(CenterTab tab) {
        auto* ret = new CenterPopup();
        if (ret->init(tab)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    static constexpr float kW = 450.f;
    static constexpr float kH = 300.f;

    bool init(CenterTab tab) {
        if (!PaimonPopup::init(kW, kH)) return false;
        m_tab = static_cast<int>(tab);
        setID("animate-center"_spr);
        setTitle(tr("Animation Center", "Centro de Animacion"));
        addInfoButton("Paimon Animate", tr(
            "<cy>1.</c> Make an animation: a <cg>group of frames</c>.\n"
            "<cy>2.</c> Draw on the current frame; new objects join it on their own.\n"
            "<cy>3.</c> Add or duplicate frames and scrub them with <cy>,</c> and <cy>.</c>\n"
            "<cy>4.</c> Pick the <cg>FPS</c>, the mode and how it starts here.\n"
            "<cy>5.</c> <cp>Bake</c> writes native Alpha, Spawn and Stop triggers, so the level "
            "plays without the mod.",
            "<cy>1.</c> Crea una animacion: un <cg>grupo de frames</c>.\n"
            "<cy>2.</c> Dibuja en el frame actual; los objetos nuevos se suman solos.\n"
            "<cy>3.</c> Agrega o duplica frames y recorrelos con <cy>,</c> y <cy>.</c>\n"
            "<cy>4.</c> Elige los <cg>FPS</c>, el modo y como arranca aca.\n"
            "<cy>5.</c> <cp>Hornear</c> escribe triggers nativos Alpha, Spawn y Stop, asi el nivel "
            "funciona sin el mod."));

        auto* bakeButton = ui::makeButton(tr("Bake", "Hornear"), [this] {
            bake(session().activeIndex());
            scheduleRebuild();
        }, ui::Btn::Green, 120.f, 0.6f);
        bakeButton->setPosition({kW / 2.f - 70.f, 20.f});
        m_buttonMenu->addChild(bakeButton);

        auto* groupsButton = ui::makeButton(tr("Frame groups", "Grupos de frames"),
            [] { openClips(); }, ui::Btn::Blue, 120.f, 0.6f);
        groupsButton->setPosition({kW / 2.f + 70.f, 20.f});
        m_buttonMenu->addChild(groupsButton);

        rebuild();
        schedule(schedule_selector(CenterPopup::watch), 0.2f);
        return true;
    }

    void watch(float) {
        auto* clip = session().activeClip();
        int const id = clip ? clip->id : 0;
        if (id == m_clipId && session().doc().clips.size() == m_clipCount) return;
        if (isTopPopup(this)) rebuild();
    }

    void scheduleRebuild() {
        WeakRef<CenterPopup> self = this;
        later([self] {
            if (auto popup = self.lock(); popup && popup->getParent()) popup->rebuild();
        });
    }

    void edit(std::function<void(Clip&)> fn, bool relayout = false) {
        auto* clip = session().findClip(m_clipId);
        if (!clip) return;
        fn(*clip);
        sanitize(*clip);
        session().changed(clip);
        if (relayout) scheduleRebuild();
        else buildSummary();
    }

    void pref(std::function<void(ViewPrefs&)> fn, bool relayout = false) {
        fn(session().prefs());
        session().prefsChanged();
        if (relayout) scheduleRebuild();
    }

    void buildSummary() {
        if (m_summary) m_summary->removeFromParent();
        m_summary = CCNode::create();
        m_mainLayer->addChild(m_summary, 2);
        float const width = kW - 28.f;
        auto* inset = ui::makeInset({width, 34.f}, 100);
        inset->setPosition({14.f, kH - 72.f});
        m_summary->addChild(inset);

        auto* clip = session().findClip(m_clipId);
        float const y = kH - 55.f;
        auto* swatch = w::fitted(w::icon(nullptr, {"GJ_colorBtn_001.png"}), 18.f);
        swatch->setColor(clip ? w::clipColor(clip->color) : ui::palette::dim);
        swatch->setPosition({30.f, y});
        m_summary->addChild(swatch);

        auto* name = ui::makeLabel(clip ? clip->name.c_str() : tr("No animation", "Sin animacion"),
            170.f, 0.42f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({46.f, y + 6.f});
        m_summary->addChild(name);

        auto* stats = ui::makeText(clip ? clipStats(*clip).c_str()
            : tr("Create one from the Frame groups button.", "Crea una con el boton Grupos de frames."),
            270.f, 0.42f);
        stats->setAnchorPoint({0.f, 0.5f});
        stats->setPosition({46.f, y - 7.f});
        m_summary->addChild(stats);

        if (clip) {
            auto* status = CCLabelBMFont::create(statusText(*clip).c_str(), "goldFont.fnt");
            status->limitLabelWidth(90.f, 0.5f, 0.2f);
            status->setColor(statusColor(*clip));
            status->setAnchorPoint({1.f, 0.5f});
            status->setPosition({kW - 24.f, y});
            m_summary->addChild(status);
        }
    }

    CCNode* presetRow(float width, std::vector<int> values, int current, std::function<void(int)> pick) {
        auto row = makeRow(width, 32.f, tr("Presets", "Rapidos"), nullptr);
        float const area = width - 86.f;
        float const step = area / static_cast<float>(values.size());
        auto shared = std::make_shared<std::function<void(int)>>(std::move(pick));
        for (std::size_t i = 0; i < values.size(); ++i) {
            int const value = values[i];
            auto* button = ui::makeButton(fmt::format("{}", value).c_str(), [shared, value] {
                (*shared)(value);
            }, value == current ? ui::Btn::Green : ui::Btn::Gray, step - 4.f, 0.5f, "bigFont.fnt");
            button->setPosition({80.f + step * (static_cast<float>(i) + 0.5f), 16.f});
            row.menu->addChild(button);
        }
        return row.node;
    }

    std::vector<CCNode*> playbackItems(Clip const& clip, float width, float inner) {
        auto const& s = clip.settings;
        auto const steps = buildSequence(clip);
        std::vector<CCNode*> items;

        std::string const timing = fmt::format("{} {} {}  |  {} {}  |  {} {}",
            tr("One frame lasts", "Un frame dura"), w::seconds(1.f / s.fps), tr("(x hold)", "(x hold)"),
            tr("steps:", "pasos:"), steps.size(), tr("total:", "total:"), w::seconds(sequenceDuration(steps)));
        items.push_back(kit::makeCard(width, tr("Speed", "Velocidad"), {40, 200, 255}, {
            kit::makeSliderRow(inner, tr("Frames per second", "Frames por segundo"),
                tr("How many frames play every second. Hold stretches a single frame.",
                   "Cuantos frames se ven por segundo. Hold estira un frame solo."),
                s.fps, kMinFps, kMaxFps,
                [](double v) { return fmt::format("{} fps", std::lround(v)); },
                [this](double v) {
                    edit([v](Clip& c) { c.settings.fps = static_cast<float>(std::lround(v)); });
                }),
            presetRow(inner, {6, 8, 10, 12, 15, 24, 30, 60}, static_cast<int>(std::lround(s.fps)),
                [this](int fps) { edit([fps](Clip& c) { c.settings.fps = static_cast<float>(fps); }, true); }),
            kit::makeHint(inner, timing.c_str()),
        }));

        std::vector<std::string> modes;
        for (int i = 0; i < 5; ++i) modes.push_back(w::modeName(static_cast<PlayMode>(i)));
        std::vector<CCNode*> playback = {
            kit::makeSelectRow(inner, tr("Mode", "Modo"),
                tr("Loop forever, play once, bounce, repeat N times or loop backwards.",
                   "Bucle infinito, una vez, ida y vuelta, N veces o bucle al reves."),
                modes, static_cast<int>(s.mode),
                [this](int i) { edit([i](Clip& c) { c.settings.mode = static_cast<PlayMode>(i); }, true); }),
        };
        if (s.mode == PlayMode::Repeat) {
            playback.push_back(kit::makeSliderRow(inner, tr("Repeats", "Repeticiones"), nullptr,
                s.repeats, 1.0, 20.0, [](double v) { return fmt::format("x{}", std::lround(v)); },
                [this](double v) {
                    edit([v](Clip& c) { c.settings.repeats = static_cast<int>(std::lround(v)); });
                }));
        }
        if (!loops(s.mode)) {
            playback.push_back(kit::makeSelectRow(inner, tr("When it ends", "Al terminar"), nullptr,
                {w::endName(EndAction::HoldLast), w::endName(EndAction::HideAll), w::endName(EndAction::ShowFirst)},
                static_cast<int>(s.end),
                [this](int i) { edit([i](Clip& c) { c.settings.end = static_cast<EndAction>(i); }); }));
        }
        playback.push_back(kit::makeSelectRow(inner, tr("Frame switch", "Cambio de frame"),
            tr("Alpha can crossfade and keeps collisions; Toggle fully disables the frame.",
               "Alpha puede fundir y mantiene colisiones; Toggle apaga el frame entero."),
            {tr("Alpha trigger", "Trigger Alpha"), tr("Toggle trigger", "Trigger Toggle")},
            static_cast<int>(s.visibility),
            [this](int i) { edit([i](Clip& c) { c.settings.visibility = static_cast<Visibility>(i); }, true); }));
        if (s.visibility == Visibility::Alpha) {
            playback.push_back(kit::makeSliderRow(inner, tr("Crossfade", "Fundido"),
                tr("Fade time between frames. 0 cuts instantly.", "Tiempo de fundido entre frames. 0 corta al instante."),
                s.fade, 0.0, 1.0, [](double v) { return fmt::format("{:.2f}s", v); },
                [this](double v) { edit([v](Clip& c) { c.settings.fade = static_cast<float>(v); }); }));
        }
        items.push_back(kit::makeCard(width, tr("Playback", "Reproduccion"), {120, 230, 90}, playback));

        auto retime = [this](std::function<void(Frame&)> fn) {
            edit([fn](Clip& c) {
                for (auto& frame : c.frames) fn(frame);
            }, true);
        };
        items.push_back(kit::makeCard(width, tr("Retiming", "Tiempos"), {255, 210, 90}, {
            kit::makeButtonRow(inner, tr("Reset holds", "Reiniciar holds"),
                tr("Every frame back to a single beat.", "Todos los frames vuelven a un solo tiempo."),
                tr("Reset", "Reiniciar"), [retime] { retime([](Frame& f) { f.hold = 1; }); }, ui::Btn::Gray),
            kit::makeButtonRow(inner, tr("Slower x2", "Mas lento x2"), nullptr, tr("Double", "Doblar"),
                [retime] { retime([](Frame& f) { f.hold = std::min(kMaxHold, f.hold * 2); }); }, ui::Btn::Cyan),
            kit::makeButtonRow(inner, tr("Faster x2", "Mas rapido x2"), nullptr, tr("Halve", "Mitad"),
                [retime] { retime([](Frame& f) { f.hold = std::max(1, f.hold / 2); }); }, ui::Btn::Cyan),
            kit::makeButtonRow(inner, tr("Reverse frame order", "Invertir orden"), nullptr, tr("Reverse", "Invertir"),
                [this] {
                    session().reverseFrames();
                    scheduleRebuild();
                }, ui::Btn::Pink),
            kit::makeButtonRow(inner, tr("Stop skipping frames", "Dejar de omitir"), nullptr, tr("Unskip", "Mostrar"),
                [retime] { retime([](Frame& f) { f.skip = false; }); }, ui::Btn::Gray),
        }));
        return items;
    }

    std::vector<CCNode*> startItems(Clip const& clip, float width, float inner) {
        auto const& s = clip.settings;
        std::vector<CCNode*> items;

        std::vector<CCNode*> start = {
            kit::makeSelectRow(inner, tr("Starts", "Arranca"),
                tr("With the level, when something spawns its start group, or at an X position.",
                   "Con el nivel, cuando algo hace spawn de su grupo inicio, o en una posicion X."),
                {tr("With the level", "Con el nivel"), tr("From the start group", "Con el grupo inicio"),
                 tr("At an X position", "En una posicion X")},
                static_cast<int>(s.start),
                [this](int i) { edit([i](Clip& c) { c.settings.start = static_cast<StartMode>(i); }, true); }),
            kit::makeSliderRow(inner, tr("Start delay", "Retraso inicial"), nullptr, s.startDelay, 0.0, 10.0,
                [](double v) { return fmt::format("{:.2f}s", v); },
                [this](double v) { edit([v](Clip& c) { c.settings.startDelay = static_cast<float>(v); }); }),
        };
        if (s.start == StartMode::Position) {
            std::string const at = s.startX > 0.f
                ? fmt::format("X = {:.0f}", s.startX) : tr("X = left edge of the drawing", "X = borde izquierdo del dibujo");
            start.push_back(kit::makeButtonRow(inner, at.c_str(),
                tr("The camera center becomes the trigger position.", "El centro de la camara pasa a ser la posicion."),
                tr("Use camera", "Usar camara"), [this] {
                    float const x = viewCenter().x;
                    edit([x](Clip& c) { c.settings.startX = std::max(0.f, x); }, true);
                }, ui::Btn::Cyan));
        }
        start.push_back(kit::makeSelectRow(inner, tr("Before it starts", "Antes de arrancar"), nullptr,
            {tr("Show first frame", "Mostrar el primero"), tr("Hide everything", "Ocultar todo"),
             tr("Show every frame", "Mostrar todos")},
            static_cast<int>(s.before),
            [this](int i) { edit([i](Clip& c) { c.settings.before = static_cast<BeforeStart>(i); }); }));
        items.push_back(kit::makeCard(width, tr("Start", "Inicio"), {205, 160, 255}, start));

        std::vector<CCNode*> control = {
            kit::makeToggleRow(inner, tr("Stop, pause and resume groups", "Grupos de stop, pausa y reanudar"),
                tr("Spawn them from any trigger to control the animation in game.",
                   "Haz spawn de ellos desde cualquier trigger para controlar la animacion."),
                s.controls,
                [this](bool v) { edit([v](Clip& c) { c.settings.controls = v; }, true); }),
        };
        if (s.controls) {
            control.push_back(kit::makeSelectRow(inner, tr("When stopped", "Al detenerse"), nullptr,
                {w::endName(EndAction::HoldLast), w::endName(EndAction::HideAll), w::endName(EndAction::ShowFirst)},
                static_cast<int>(s.stopAction),
                [this](int i) { edit([i](Clip& c) { c.settings.stopAction = static_cast<EndAction>(i); }); }));
        }
        items.push_back(kit::makeCard(width, tr("Controls", "Controles"), {110, 220, 255}, control));

        items.push_back(kit::makeCard(width, tr("Editor", "Editor"), {255, 175, 75}, {
            kit::makeSliderRow(inner, tr("Trigger editor layer", "Capa del editor"),
                tr("Keeps baked triggers on their own layer. 0 uses the default.",
                   "Deja los triggers horneados en su propia capa. 0 usa la normal."),
                s.editorLayer, 0.0, 100.0,
                [](double v) { return std::lround(v) == 0 ? std::string("-") : fmt::format("{}", std::lround(v)); },
                [this](double v) { edit([v](Clip& c) { c.settings.editorLayer = static_cast<int>(std::lround(v)); }); }),
        }));
        return items;
    }

    std::vector<CCNode*> bakeItems(Clip const& clip, float width, float inner) {
        auto const& c = clip.compiled;
        std::vector<CCNode*> items;
        std::string const status = !c.present()
            ? std::string(tr("Not baked yet. The animation only plays inside this editor.",
                "Aun sin hornear. La animacion solo se ve en este editor."))
            : fmt::format("{} {} {} {}.{}", c.triggers, tr("triggers for", "triggers para"), c.steps,
                tr("steps", "pasos"), c.stale ? tr(" Frames changed since then: bake again.",
                    " Los frames cambiaron desde entonces: vuelve a hornear.") : "");

        items.push_back(kit::makeCard(width, tr("Bake", "Hornear"), {255, 120, 220}, {
            kit::makeHint(inner, status.c_str()),
            kit::makeButtonRow(inner, tr("Bake this animation", "Hornear esta animacion"),
                tr("Replaces its previous triggers; your own triggers are never touched.",
                   "Reemplaza sus triggers anteriores; los tuyos nunca se tocan."),
                tr("Bake", "Hornear"), [this] {
                    bake(session().activeIndex());
                    scheduleRebuild();
                }, ui::Btn::Green),
            kit::makeButtonRow(inner, tr("Remove baked triggers", "Quitar triggers horneados"), nullptr,
                tr("Remove", "Quitar"), [this] {
                    auto* clip = session().findClip(m_clipId);
                    if (!clip) return;
                    auto const removed = removeCompiled(session().editor(), *clip);
                    session().changed();
                    w::notifyOk(fmt::format("{} {}", removed, tr("triggers removed", "triggers quitados")));
                    scheduleRebuild();
                }, ui::Btn::Red),
            kit::makeToggleRow(inner, tr("Auto bake", "Hornear solo"),
                tr("Bakes again on save and playtest when frames changed.",
                   "Vuelve a hornear al guardar y al probar si cambiaron los frames."),
                clip.settings.compileOnSave,
                [this](bool v) { edit([v](Clip& c) { c.settings.compileOnSave = v; }); }),
            kit::makeButtonRow(inner, tr("Bake every animation", "Hornear todas"), nullptr,
                tr("Bake all", "Todas"), [this] {
                    bakeAll();
                    scheduleRebuild();
                }, ui::Btn::Cyan),
        }));

        if (c.marker > 0) {
            auto copyRow = [inner](char const* title, char const* desc, int group) {
                std::string const text = fmt::format("{}  {}", title, group);
                return kit::makeButtonRow(inner, text.c_str(), desc, tr("Copy", "Copiar"), [group] {
                    geode::utils::clipboard::write(fmt::format("{}", group));
                    w::notifyOk(fmt::format("{} {}", tr("Copied", "Copiado"), group));
                }, ui::Btn::Gray);
            };
            std::vector<CCNode*> ids = {
                copyRow(tr("Start group", "Grupo inicio"),
                    tr("Spawn it to play from frame 1. To restart, spawn Stop first.",
                       "Spawn para reproducir desde el frame 1. Para reiniciar, primero Stop."),
                    c.start),
            };
            if (c.stop > 0) {
                ids.push_back(copyRow(tr("Stop group", "Grupo stop"), nullptr, c.stop));
                ids.push_back(copyRow(tr("Pause group", "Grupo pausa"), nullptr, c.pause));
                ids.push_back(copyRow(tr("Resume group", "Grupo reanudar"), nullptr, c.resume));
            }
            ids.push_back(copyRow(tr("Marker group", "Grupo marca"),
                tr("Every generated trigger carries it.", "Todos los triggers generados lo llevan."), c.marker));
            items.push_back(kit::makeCard(width, tr("Group ids", "Ids de grupo"), {120, 240, 110}, ids));
        }

        std::string frames;
        for (int group : frameGroups(clip)) {
            if (!frames.empty()) frames += ", ";
            frames += fmt::format("{}", group);
            if (frames.size() > 220) {
                frames += "...";
                break;
            }
        }
        if (!frames.empty()) {
            std::string const text = fmt::format("{} {}", tr("Frame groups:", "Grupos de los frames:"), frames);
            items.push_back(kit::makeHint(width, text.c_str()));
        }
        return items;
    }

    std::vector<CCNode*> viewItems(float width, float inner) {
        auto const& p = session().prefs();
        std::vector<CCNode*> items;
        auto count = [](double v) { return fmt::format("{}", std::lround(v)); };
        auto percent = [](double v) { return fmt::format("{}%", std::lround(v / 2.55)); };

        items.push_back(kit::makeCard(width, tr("Onion skin", "Papel cebolla"), {90, 170, 255}, {
            kit::makeToggleRow(inner, tr("Onion skin", "Papel cebolla"),
                tr("Shows nearby frames faded behind the one you draw.",
                   "Muestra los frames vecinos tenues detras del que dibujas."),
                p.onion, [this](bool v) { pref([v](ViewPrefs& p) { p.onion = v; }); }),
            kit::makeSliderRow(inner, tr("Frames before", "Frames antes"), nullptr, p.onionBefore, 0.0, 5.0, count,
                [this](double v) { pref([v](ViewPrefs& p) { p.onionBefore = static_cast<int>(std::lround(v)); }); }),
            kit::makeSliderRow(inner, tr("Frames after", "Frames despues"), nullptr, p.onionAfter, 0.0, 5.0, count,
                [this](double v) { pref([v](ViewPrefs& p) { p.onionAfter = static_cast<int>(std::lround(v)); }); }),
            kit::makeSliderRow(inner, tr("Onion opacity", "Opacidad cebolla"), nullptr, p.onionOpacity, 10.0, 230.0,
                percent,
                [this](double v) { pref([v](ViewPrefs& p) { p.onionOpacity = static_cast<int>(std::lround(v)); }); }),
        }));

        items.push_back(kit::makeCard(width, tr("Other frames", "Otros frames"), {205, 160, 255}, {
            kit::makeSelectRow(inner, tr("While editing", "Mientras editas"), nullptr,
                {tr("Hidden", "Ocultos"), tr("Dimmed (this animation)", "Atenuados (esta animacion)"),
                 tr("Visible", "Visibles")},
                static_cast<int>(p.ghosts),
                [this](int i) { pref([i](ViewPrefs& p) { p.ghosts = static_cast<Ghosts>(i); }); }),
            kit::makeSliderRow(inner, tr("Dim opacity", "Opacidad atenuada"), nullptr, p.dimOpacity, 10.0, 230.0,
                percent,
                [this](double v) { pref([v](ViewPrefs& p) { p.dimOpacity = static_cast<int>(std::lround(v)); }); }),
            kit::makeToggleRow(inner, tr("Lock other frames", "Bloquear otros frames"),
                tr("Only the current frame can be selected.", "Solo se puede seleccionar el frame actual."),
                p.lockOthers, [this](bool v) { pref([v](ViewPrefs& p) { p.lockOthers = v; }); }),
        }));

        items.push_back(kit::makeCard(width, tr("Workflow", "Flujo de trabajo"), {120, 230, 90}, {
            kit::makeToggleRow(inner, tr("Draw into the current frame", "Dibujar en el frame actual"),
                tr("New and pasted objects join the frame you are on.",
                   "Los objetos nuevos y pegados entran al frame en el que estas."),
                p.autoAssign, [this](bool v) { pref([v](ViewPrefs& p) { p.autoAssign = v; }); }),
            kit::makeToggleRow(inner, tr("Loop the preview", "Repetir la vista previa"),
                tr("The editor preview loops even clips that play once.",
                   "La vista previa repite incluso las que van una vez."),
                p.previewLoop, [this](bool v) { pref([v](ViewPrefs& p) { p.previewLoop = v; }); }),
            kit::makeToggleRow(inner, tr("Follow the playhead", "Seguir el cabezal"),
                tr("Playing moves the current frame along.", "Reproducir mueve el frame actual."),
                p.followPlayhead, [this](bool v) { pref([v](ViewPrefs& p) { p.followPlayhead = v; }); }),
        }));
        return items;
    }

    void rebuild() {
        float offset = 0.f;
        bool const keep = m_scroll && m_shownTab == m_tab;
        if (m_scroll) {
            offset = m_scroll->m_contentLayer->getPositionY() -
                (m_scroll->getContentSize().height - m_scroll->m_contentLayer->getContentSize().height);
            m_scroll->removeFromParent();
            m_scroll = nullptr;
        }
        if (m_tabs) m_tabs->removeFromParent();

        auto* clip = session().activeClip();
        m_clipId = clip ? clip->id : 0;
        m_clipCount = session().doc().clips.size();
        buildSummary();

        float const width = kW - 28.f;
        float const inner = kit::cardInnerWidth(width);
        m_tabs = kit::makeTabBar(width,
            {tr("Playback", "Reproduccion"), tr("Start", "Inicio"), tr("Bake", "Hornear"), tr("View", "Vista")},
            m_tab, [this](int i) {
                m_tab = i;
                scheduleRebuild();
            });
        m_tabs->setPosition({14.f, kH - 102.f});
        m_mainLayer->addChild(m_tabs, 2);

        std::vector<CCNode*> items;
        if (m_tab == static_cast<int>(CenterTab::View)) {
            items = viewItems(width, inner);
        } else if (!clip) {
            items.push_back(kit::makeHint(width, tr(
                "There is no animation yet. Select the objects of your first frame and create one.",
                "Aun no hay animaciones. Selecciona los objetos del primer frame y crea una.")));
            items.push_back(kit::makeButtonRow(width, tr("New animation", "Nueva animacion"), nullptr,
                tr("Create", "Crear"), [] { createWithPrompt(false); }, ui::Btn::Green));
            items.push_back(kit::makeButtonRow(width, tr("From selection", "Desde la seleccion"), nullptr,
                tr("Create", "Crear"), [] { createWithPrompt(true); }, ui::Btn::Blue));
        } else if (m_tab == static_cast<int>(CenterTab::Playback)) {
            items = playbackItems(*clip, width, inner);
        } else if (m_tab == static_cast<int>(CenterTab::Start)) {
            items = startItems(*clip, width, inner);
        } else {
            items = bakeItems(*clip, width, inner);
        }

        float const height = kH - 102.f - 6.f - 40.f;
        m_scroll = kit::makeScrollStack({width, height}, items, 6.f);
        m_scroll->setPosition({14.f, 40.f});
        m_mainLayer->addChild(m_scroll, 1);
        if (keep) {
            float const minY = height - m_scroll->m_contentLayer->getContentSize().height;
            m_scroll->m_contentLayer->setPositionY(std::clamp(minY + offset, std::min(minY, 0.f), 0.f));
        }
        m_shownTab = m_tab;
    }

    int m_tab = 0;
    int m_shownTab = -1;
    int m_clipId = 0;
    std::size_t m_clipCount = 0;
    geode::ScrollLayer* m_scroll = nullptr;
    CCNode* m_tabs = nullptr;
    CCNode* m_summary = nullptr;
};


class ClipsPopup : public PaimonPopup {
public:
    static ClipsPopup* create() {
        auto* ret = new ClipsPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    static constexpr float kW = 440.f;
    static constexpr float kH = 290.f;
    static constexpr float kRowH = 40.f;

    bool init() override {
        if (!PaimonPopup::init(kW, kH)) return false;
        setID("animate-clips"_spr);
        setTitle(tr("Frame Groups", "Grupos de Frames"));
        addInfoButton(tr("Frame Groups", "Grupos de Frames"), tr(
            "Every group is one animation with its own frames, speed and triggers.\n"
            "<cg>Eye</c> hides it while editing, <co>lock</c> protects it from selection, the arrows "
            "change the order and the color tags it in the timeline.",
            "Cada grupo es una animacion con sus propios frames, velocidad y triggers.\n"
            "El <cg>ojo</c> la oculta mientras editas, el <co>candado</c> la protege de la seleccion, las "
            "flechas cambian el orden y el color la marca en la linea de tiempo."));

        struct Action {
            char const* text;
            ui::Btn skin;
            std::function<void()> run;
        };
        std::vector<Action> actions = {
            {tr("New", "Nueva"), ui::Btn::Green, [] { createWithPrompt(false); }},
            {tr("From selection", "Desde seleccion"), ui::Btn::Blue, [] { createWithPrompt(true); }},
            {tr("Import groups", "Importar grupos"), ui::Btn::Cyan, [] { importWithPrompt(); }},
            {tr("Settings", "Ajustes"), ui::Btn::Gray, [] { openCenter(CenterTab::Playback); }},
        };
        float const step = (kW - 40.f) / static_cast<float>(actions.size());
        std::vector<CCMenuItemSpriteExtra*> buttons;
        for (std::size_t i = 0; i < actions.size(); ++i) {
            auto* button = ui::makeButton(actions[i].text, actions[i].run, actions[i].skin, step - 8.f, 0.55f);
            button->setPosition({20.f + step * (static_cast<float>(i) + 0.5f), 22.f});
            m_buttonMenu->addChild(button);
            buttons.push_back(button);
        }
        ui::matchButtonLabels(buttons);

        rebuild();
        schedule(schedule_selector(ClipsPopup::watch), 0.15f);
        return true;
    }

    void watch(float) {
        if (session().revision() != m_revision && isTopPopup(this)) rebuild();
    }

    CCNode* clipRow(int index, float width) {
        auto& s = session();
        auto const& clip = s.doc().clips[static_cast<std::size_t>(index)];
        bool const active = index == s.activeIndex();
        auto const tint = w::clipColor(clip.color);

        auto row = makeRow(width, kRowH, nullptr, nullptr, active ? 150 : 70);
        if (active) row.inset->setColor(w::scaled(tint, 0.4f));

        auto* swatch = w::fitted(w::icon(nullptr, {"GJ_colorBtn_001.png"}), 20.f);
        swatch->setColor(tint);
        auto* colorButton = CCMenuItemExt::createSpriteExtra(swatch, [index](auto*) {
            session().cycleColor(index);
        });
        colorButton->setPosition({18.f, kRowH / 2.f});
        row.menu->addChild(colorButton);

        auto* info = CCNode::create();
        info->setContentSize({190.f, 30.f});
        info->setAnchorPoint({0.5f, 0.5f});
        auto* name = ui::makeLabel(clip.name.c_str(), 180.f, 0.42f, active ? ui::palette::gold : ui::palette::text);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({0.f, 22.f});
        info->addChild(name);
        std::string const sub = fmt::format("{}  |  {}", clipStats(clip), statusText(clip));
        auto* stats = ui::makeText(sub.c_str(), 300.f, 0.4f);
        stats->setAnchorPoint({0.f, 0.5f});
        stats->limitLabelWidth(190.f, 0.4f, 0.2f);
        stats->setPosition({0.f, 8.f});
        info->addChild(stats);
        auto* infoButton = CCMenuItemExt::createSpriteExtra(info, [index](auto*) {
            session().selectClip(index);
            if (!session().isOpen()) session().setOpen(true);
        });
        infoButton->m_scaleMultiplier = 1.02f;
        infoButton->setPosition({34.f + 95.f, kRowH / 2.f});
        row.menu->addChild(infoButton);

        struct Tool {
            CCSprite* top;
            EditorBaseColor color;
            std::function<void()> run;
            bool enabled = true;
        };
        int const count = static_cast<int>(s.doc().clips.size());
        std::vector<Tool> tools = {
            {w::icon(clip.hidden ? "paim_anim_eyeOff.png" : "paim_anim_eye.png", {"GJ_infoIcon_001.png"}),
                clip.hidden ? EditorBaseColor::DarkGray : EditorBaseColor::Cyan,
                [index] { session().toggleHidden(index); }},
            {w::icon(clip.locked ? "paim_anim_lock.png" : "paim_anim_unlock.png", {"GJ_lock_001.png"}),
                clip.locked ? EditorBaseColor::Orange : EditorBaseColor::DarkGray,
                [index] { session().toggleLocked(index); }},
            {w::icon(nullptr, {"edit_upBtn_001.png"}), EditorBaseColor::LightBlue,
                [index] { session().moveClip(index, -1); }, index > 0},
            {w::icon(nullptr, {"edit_downBtn_001.png"}), EditorBaseColor::LightBlue,
                [index] { session().moveClip(index, 1); }, index + 1 < count},
            {w::icon("paim_anim_tag.png", {"GJ_editBtn_001.png"}), EditorBaseColor::Orange, [index] {
                auto* clip = session().clipAt(index);
                if (!clip) return;
                int const id = clip->id;
                askText(tr("Rename", "Renombrar"), tr("New name for this group of frames.",
                    "Nuevo nombre para este grupo de frames."), clip->name, kNameChars, 32,
                    [id](std::string const& name) {
                        int const at = session().indexOf(id);
                        if (at >= 0) session().renameClip(at, name);
                    });
            }},
            {w::icon("paim_anim_frameDup.png", {"GJ_duplicateBtn_001.png"}), EditorBaseColor::Green, [index] {
                auto result = session().duplicateClip(index);
                if (result.isErr()) w::notifyError(result.unwrapErr());
            }},
            {w::icon("paim_anim_frameDel.png", {"GJ_deleteIcon_001.png"}), EditorBaseColor::Salmon, [index] {
                auto* clip = session().clipAt(index);
                if (!clip) return;
                int const id = clip->id;
                confirm(tr("Delete group", "Borrar grupo"),
                    fmt::format("{} <cy>{}</c>? {}", tr("Delete", "Borrar"), clip->name,
                        tr("Its objects stay in the level; only the frame list and its baked triggers go.",
                           "Sus objetos quedan en el nivel; solo se van la lista de frames y sus triggers.")),
                    tr("Delete", "Borrar"), [id] {
                        int const at = session().indexOf(id);
                        if (at < 0) return;
                        auto result = session().deleteClip(at, false);
                        if (result.isErr()) w::notifyError(result.unwrapErr());
                    });
            }},
        };
        float x = width - 14.f - static_cast<float>(tools.size() - 1) * 23.f;
        for (auto& tool : tools) {
            auto* button = w::toolButton(tool.top, tool.color, 20.f, std::move(tool.run));
            if (!button) continue;
            button->setPosition({x, kRowH / 2.f});
            button->setEnabled(tool.enabled);
            if (!tool.enabled) button->setOpacity(100);
            row.menu->addChild(button);
            x += 23.f;
        }
        return row.node;
    }

    void rebuild() {
        m_revision = session().revision();
        float offset = 0.f;
        bool const keep = m_scroll != nullptr;
        if (m_scroll) {
            offset = m_scroll->m_contentLayer->getPositionY() -
                (m_scroll->getContentSize().height - m_scroll->m_contentLayer->getContentSize().height);
            m_scroll->removeFromParent();
            m_scroll = nullptr;
        }

        float const width = kW - 28.f;
        float const height = kH - 48.f - 44.f;
        std::vector<CCNode*> rows;
        auto const& clips = session().doc().clips;
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) rows.push_back(clipRow(i, width - 6.f));
        if (rows.empty()) {
            rows.push_back(kit::makeHint(width, tr(
                "No frame groups yet. Make a new one, build it from the selected objects, or import "
                "groups you already animated by hand.",
                "Aun no hay grupos de frames. Crea uno nuevo, armalo desde los objetos seleccionados o "
                "importa grupos que ya animaste a mano.")));
        }
        m_scroll = kit::makeScrollStack({width, height}, rows, 4.f);
        m_scroll->setPosition({14.f, 44.f});
        m_mainLayer->addChild(m_scroll, 1);
        if (keep) {
            float const minY = height - m_scroll->m_contentLayer->getContentSize().height;
            m_scroll->m_contentLayer->setPositionY(std::clamp(minY + offset, std::min(minY, 0.f), 0.f));
        }
    }

    int m_revision = -1;
    geode::ScrollLayer* m_scroll = nullptr;
};


class FramePopup : public PaimonPopup {
public:
    static FramePopup* create() {
        auto* ret = new FramePopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    static constexpr float kW = 390.f;
    static constexpr float kH = 286.f;

    bool init() override {
        if (!PaimonPopup::init(kW, kH)) return false;
        setID("animate-frame"_spr);
        setTitle(tr("Frame", "Frame"));

        for (int direction : {-1, 1}) {
            auto* arrow = ui::makeFrameButton("GJ_arrow_03_001.png", 0.5f, [direction] {
                session().stopPlayback();
                session().stepFrame(direction);
            });
            if (direction > 0) static_cast<CCSprite*>(arrow->getNormalImage())->setFlipX(true);
            arrow->setPosition({kW / 2.f + static_cast<float>(direction) * 105.f, kH - 22.f});
            m_buttonMenu->addChild(arrow);
        }

        auto* labelInset = ui::makeInset({kW - 28.f, 34.f}, 60);
        labelInset->setPosition({14.f, kH - 82.f});
        m_mainLayer->addChild(labelInset);
        auto* labelTitle = ui::makeLabel(tr("Label", "Etiqueta"), 120.f, 0.38f);
        labelTitle->setAnchorPoint({0.f, 0.5f});
        labelTitle->setPosition({24.f, kH - 65.f});
        m_mainLayer->addChild(labelTitle);

        m_input = TextInput::create(220.f, tr("walk, blink, jump...", "caminar, parpadeo..."));
        m_input->setMaxCharCount(24);
        m_input->setFilter(kNameChars);
        m_input->setPosition({kW - 24.f - 110.f, kH - 65.f});
        m_input->setCallback([this](std::string const& text) {
            session().setLabel(session().currentFrame(), trimmed(text));
            m_revision = session().revision();
        });
        m_mainLayer->addChild(m_input, 2);

        m_body = CCNode::create();
        m_mainLayer->addChild(m_body, 1);

        auto* hint = ui::makeText(tr("Tap the current cell in the timeline to open this panel.",
            "Toca la celda actual de la linea de tiempo para abrir este panel."), kW - 40.f, 0.42f,
            ui::palette::dim, kCCTextAlignmentCenter);
        hint->setPosition({kW / 2.f, 16.f});
        m_mainLayer->addChild(hint);

        rebuild();
        schedule(schedule_selector(FramePopup::watch), 0.1f);
        return true;
    }

    void onClose(CCObject* sender) override {
        paimon::ui::detachGeodeTextInput(m_input);
        m_input = nullptr;
        Popup::onClose(sender);
    }

    void watch(float) {
        if (session().revision() == m_revision && session().currentFrame() == m_frame) return;
        if (isTopPopup(this)) rebuild();
    }

    void add(CCMenuItemSpriteExtra* button, CCPoint position) {
        if (!button) return;
        button->setPosition(position);
        m_buttonMenu->addChild(button);
        m_dynamic.push_back(button);
    }

    void rebuild() {
        auto& s = session();
        m_revision = s.revision();
        m_frame = s.currentFrame();
        for (auto* node : m_dynamic) node->removeFromParent();
        m_dynamic.clear();
        m_body->removeAllChildren();

        auto* clip = s.activeClip();
        auto* frame = s.frameAt(m_frame);
        if (!clip || !frame) {
            onClose(nullptr);
            return;
        }

        int const count = static_cast<int>(clip->frames.size());
        setTitle(fmt::format("{} {} / {}", tr("Frame", "Frame"), m_frame + 1, count).c_str(), "goldFont.fnt", 0.6f);
        if (m_input && m_input->getString() != frame->label) m_input->setString(frame->label);

        auto row = [&](float top, char const* title, std::string const& desc) {
            auto* inset = ui::makeInset({kW - 28.f, 36.f}, 60);
            inset->setPosition({14.f, top - 36.f});
            m_body->addChild(inset);
            auto* label = ui::makeLabel(title, 120.f, 0.38f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({24.f, top - 12.f});
            m_body->addChild(label);
            auto* sub = ui::makeText(desc.c_str(), 140.f, 0.42f);
            sub->limitLabelWidth(140.f, 0.42f, 0.2f);
            sub->setAnchorPoint({0.f, 0.5f});
            sub->setPosition({24.f, top - 26.f});
            m_body->addChild(sub);
            return top - 18.f;
        };

        float const holdY = row(kH - 88.f, tr("Hold", "Hold"),
            fmt::format("{} {}", w::seconds(frameDuration(*clip, *frame)), tr("on screen", "en pantalla")));
        auto* holdValue = CCLabelBMFont::create(fmt::format("x{}", frame->hold).c_str(), "goldFont.fnt");
        holdValue->setScale(0.6f);
        holdValue->setPosition({212.f, holdY});
        m_body->addChild(holdValue);
        int const index = m_frame;
        add(ui::makeButton("-", [index] {
            if (auto* f = session().frameAt(index)) session().setHold(index, f->hold - 1);
        }, ui::Btn::Gray, 26.f, 0.55f, "bigFont.fnt"), {182.f, holdY});
        add(ui::makeButton("+", [index] {
            if (auto* f = session().frameAt(index)) session().setHold(index, f->hold + 1);
        }, ui::Btn::Gray, 26.f, 0.55f, "bigFont.fnt"), {242.f, holdY});
        float x = 278.f;
        for (int quick : {1, 2, 3, 4}) {
            add(ui::makeButton(fmt::format("x{}", quick).c_str(), [index, quick] {
                session().setHold(index, quick);
            }, frame->hold == quick ? ui::Btn::Green : ui::Btn::Gray, 24.f, 0.5f, "bigFont.fnt"), {x, holdY});
            x += 26.f;
        }

        auto const objects = s.objectCount(frame->group);
        std::string const groupText = frame->group > 0
            ? fmt::format("{} {}  |  {} {}", tr("Group", "Grupo"), frame->group, objects, tr("objects", "objetos"))
            : std::string(tr("Empty frame", "Frame vacio"));
        float const groupY = row(kH - 128.f, tr("Content", "Contenido"), groupText);
        add(ui::makeButton(tr("Link group", "Enlazar grupo"), [index] {
            auto* f = session().frameAt(index);
            askText(tr("Link group", "Enlazar grupo"),
                tr("Use an existing group id as this frame. 0 empties the link.",
                   "Usa un id de grupo existente como este frame. 0 quita el enlace."),
                f && f->group > 0 ? fmt::format("{}", f->group) : "", "0123456789", 4,
                [index](std::string const& value) {
                    int group = 0;
                    auto const parsed = std::from_chars(value.data(), value.data() + value.size(), group);
                    if (parsed.ec != std::errc{}) return w::notifyError(tr("Not a number.", "No es un numero."));
                    auto result = session().linkGroup(index, group);
                    if (result.isErr()) w::notifyError(result.unwrapErr());
                });
        }, ui::Btn::Cyan, 96.f, 0.5f), {232.f, groupY});

        auto* skipToggle = ui::makeToggle(frame->skip, [index](bool) { session().toggleSkip(index); }, 0.55f);
        skipToggle->setPosition({kW - 86.f, groupY});
        m_buttonMenu->addChild(skipToggle);
        m_dynamic.push_back(skipToggle);
        auto* skipLabel = ui::makeLabel(tr("Skip", "Omitir"), 46.f, 0.32f);
        skipLabel->setAnchorPoint({0.f, 0.5f});
        skipLabel->setPosition({kW - 72.f, groupY});
        m_body->addChild(skipLabel);

        auto* objectsTitle = ui::makeTitle(tr("Objects", "Objetos"), 120.f, 0.45f);
        objectsTitle->setPosition({kW / 2.f, kH - 178.f});
        m_body->addChild(objectsTitle);
        float const objectsY = kH - 200.f;
        float const third = (kW - 40.f) / 3.f;
        add(ui::makeButton(tr("Select", "Seleccionar"), [] {
            auto result = session().selectFrameObjects();
            if (result.isErr()) w::notifyError(result.unwrapErr());
        }, ui::Btn::Blue, third - 8.f, 0.5f), {20.f + third * 0.5f, objectsY});
        add(ui::makeButton(tr("Assign selection", "Asignar seleccion"), [] {
            auto result = session().assignSelection();
            if (result.isErr()) return w::notifyError(result.unwrapErr());
            w::notifyOk(fmt::format("{} {}", result.unwrap(), tr("objects assigned", "objetos asignados")));
        }, ui::Btn::Cyan, third - 8.f, 0.5f), {20.f + third * 1.5f, objectsY});
        add(ui::makeButton(tr("Clear", "Vaciar"), [objects] {
            if (objects == 0) return;
            confirm(tr("Clear frame", "Vaciar frame"),
                fmt::format("{} <cr>{}</c> {}", tr("Delete the", "Borrar los"), objects,
                    tr("objects of this frame?", "objetos de este frame?")),
                tr("Clear", "Vaciar"), [] {
                    auto result = session().clearFrame();
                    if (result.isErr()) w::notifyError(result.unwrapErr());
                });
        }, ui::Btn::Red, third - 8.f, 0.5f), {20.f + third * 2.5f, objectsY});

        float const orderY = kH - 236.f;
        float const quarter = (kW - 40.f) / 4.f;
        add(ui::makeButton(tr("< Move", "< Mover"), [] { session().moveFrame(-1); },
            ui::Btn::Gray, quarter - 8.f, 0.5f), {20.f + quarter * 0.5f, orderY});
        add(ui::makeButton(tr("Move >", "Mover >"), [] { session().moveFrame(1); },
            ui::Btn::Gray, quarter - 8.f, 0.5f), {20.f + quarter * 1.5f, orderY});
        add(ui::makeButton(tr("Duplicate", "Duplicar"), [] {
            auto result = session().duplicateFrame();
            if (result.isErr()) w::notifyError(result.unwrapErr());
        }, ui::Btn::Green, quarter - 8.f, 0.5f), {20.f + quarter * 2.5f, orderY});
        add(ui::makeButton(tr("Delete", "Borrar"), [objects] {
            auto run = [] {
                auto result = session().deleteFrame(true);
                if (result.isErr()) w::notifyError(result.unwrapErr());
            };
            if (objects == 0) return run();
            confirm(tr("Delete frame", "Borrar frame"),
                fmt::format("{} <cr>{}</c> {}", tr("The frame and its", "El frame y sus"), objects,
                    tr("objects will be deleted.", "objetos se van a borrar.")),
                tr("Delete", "Borrar"), run);
        }, ui::Btn::Red, quarter - 8.f, 0.5f), {20.f + quarter * 3.5f, orderY});
    }

    TextInput* m_input = nullptr;
    CCNode* m_body = nullptr;
    std::vector<CCNode*> m_dynamic;
    int m_revision = -1;
    int m_frame = -1;
};

} // namespace

void openCenter(CenterTab tab) {
    if (!session().editor() || hasPopup("animate-center"_spr)) return;
    if (auto* popup = CenterPopup::create(tab)) showOnTop(popup);
}

void openClips() {
    if (!session().editor() || hasPopup("animate-clips"_spr)) return;
    if (auto* popup = ClipsPopup::create()) showOnTop(popup);
}

void openFrame() {
    if (!session().editor() || hasPopup("animate-frame"_spr)) return;
    if (!session().frameAt(session().currentFrame())) {
        w::notifyError(tr("Create an animation first.", "Primero crea una animacion."));
        return;
    }
    if (auto* popup = FramePopup::create()) showOnTop(popup);
}

void askText(std::string title, std::string hint, std::string initial, std::string filter, int maxChars,
    std::function<void(std::string const&)> onConfirm) {
    if (auto* popup = PromptPopup::create(title, hint, initial, filter, maxChars, std::move(onConfirm))) {
        showOnTop(popup);
    }
}

void confirm(std::string title, std::string body, std::string action, std::function<void()> onConfirm) {
    auto* alert = geode::createQuickPopup(title.c_str(), body, tr("Cancel", "Cancelar"), action.c_str(),
        [cb = std::move(onConfirm)](FLAlertLayer*, bool accepted) {
            if (accepted && cb) cb();
        }, false, true);
    showOnTop(alert);
}

void bake(int clipIndex) {
    auto& s = session();
    auto* clip = s.clipAt(clipIndex);
    if (!clip) return w::notifyError(tr("Create an animation first.", "Primero crea una animacion."));
    auto result = compileClip(s.editor(), *clip);
    s.changed();
    if (result.isErr()) return w::notifyError(result.unwrapErr());
    auto const report = result.unwrap();
    w::notifyOk(fmt::format("{}: {} triggers  |  {} {}", clip->name, report.triggers,
        tr("start group", "grupo inicio"), clip->compiled.start));
}

void bakeAll() {
    auto& s = session();
    int baked = 0;
    int failed = 0;
    for (auto& clip : s.doc().clips) {
        if (compileClip(s.editor(), clip)) ++baked;
        else ++failed;
    }
    s.changed();
    if (failed > 0) {
        w::notifyError(fmt::format("{} {}, {} {}", baked, tr("baked", "horneadas"), failed,
            tr("could not bake", "sin hornear")));
        return;
    }
    w::notifyOk(fmt::format("{} {}", baked, tr("animations baked", "animaciones horneadas")));
}

void createWithPrompt(bool fromSelection) {
    auto& s = session();
    if (!s.editor()) return;
    if (fromSelection && s.selectionCount() == 0) {
        w::notifyError(tr("Select the objects of the first frame first.",
            "Primero selecciona los objetos del primer frame."));
        return;
    }
    askText(tr("New animation", "Nueva animacion"),
        fromSelection ? tr("The selected objects become frame 1.", "Los objetos seleccionados pasan a ser el frame 1.")
                      : tr("Name this group of frames.", "Ponle nombre a este grupo de frames."),
        fmt::format("{} {}", tr("Animation", "Animacion"), s.doc().nextId), kNameChars, 32,
        [fromSelection](std::string const& name) {
            auto result = fromSelection ? session().createClipFromSelection(name) : session().createClip(name);
            if (result.isErr()) return w::notifyError(result.unwrapErr());
            session().setOpen(true);
            w::notifyOk(fmt::format("{} {}", tr("Created", "Creada"), name));
        });
}

void importWithPrompt() {
    askText(tr("Import groups", "Importar grupos"),
        tr("Existing group ids in frame order, like 10, 11, 12 or 10-24.",
           "Ids de grupos existentes en orden, como 10, 11, 12 o 10-24."),
        "", "0123456789,- ", 200, [](std::string const& value) {
            auto const groups = parseGroupInput(value);
            if (groups.empty()) return w::notifyError(tr("No valid group ids.", "No hay ids de grupo validos."));
            auto result = session().importGroups(
                fmt::format("{} {}", tr("Imported", "Importada"), session().doc().nextId), groups);
            if (result.isErr()) return w::notifyError(result.unwrapErr());
            session().setOpen(true);
            w::notifyOk(fmt::format("{} {} {}", tr("Imported", "Importados"), groups.size(), tr("frames", "frames")));
        });
}

} // namespace paimon::animate
