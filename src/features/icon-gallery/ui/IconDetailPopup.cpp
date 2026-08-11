#include "IconDetailPopup.hpp"

#include "../services/GalleryInstaller.hpp"
#include "../services/GalleryStore.hpp"
#include "../../icon-maker/ui/IconMakerKit.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/TextArea.hpp>

#include <algorithm>
#include <ctime>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_gallery {

namespace {

constexpr float kWidth = 360.f;
constexpr float kHeight = 220.f;
constexpr float kPreviewBox = 100.f;

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

std::string formatDate(std::int64_t unixMs) {
    if (unixMs <= 0) return "-";
    auto secs = static_cast<std::time_t>(unixMs / 1000);
    std::tm tmv{};
#ifdef GEODE_IS_WINDOWS
    gmtime_s(&tmv, &secs);
#else
    gmtime_r(&secs, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m/%Y", &tmv);
    return buf;
}

// Fila "Etiqueta: valor" para la columna de datos.
CCNode* makeInfoRow(float width, std::string const& label, std::string const& value) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, 13.f});

    auto* key = CCLabelBMFont::create(label.c_str(), "chatFont.fnt");
    if (key) {
        key->setAnchorPoint({0.f, 0.5f});
        key->setScale(0.38f);
        key->setColor({150, 178, 218});
        key->setPosition({0.f, 6.5f});
        row->addChild(key);
    }

    float const keyW = key ? key->getContentWidth() * key->getScale() + 5.f : 0.f;
    auto* val = CCLabelBMFont::create(value.c_str(), "chatFont.fnt");
    if (val) {
        val->setAnchorPoint({0.f, 0.5f});
        val->setColor({255, 255, 255});
        val->limitLabelWidth(std::max(20.f, width - keyW), 0.42f, 0.22f);
        val->setPosition({keyW, 6.5f});
        row->addChild(val);
    }
    return row;
}

}  // anonymous namespace

IconDetailPopup* IconDetailPopup::create(std::string slug, std::function<void()> onChanged) {
    auto* popup = new IconDetailPopup();
    if (popup->init(std::move(slug), std::move(onChanged))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool IconDetailPopup::init(std::string slug, std::function<void()> onChanged) {
    if (!Popup::init(kWidth, kHeight)) return false;

    m_slug = std::move(slug);
    m_onChanged = std::move(onChanged);

    auto const* icon = GalleryStore::get().find(m_slug);
    this->setTitle(icon ? icon->displayName().c_str() : m_slug.c_str());
    this->setID("icon-detail-popup"_spr);
    paimon::markDynamicPopup(this);

    buildStatic();

    // Si la ficha se abrio antes de que la rejilla bajara este icono, se pide
    // ahora y se vigila hasta que llegue.
    if (!icon || !icon->metaLoaded) {
        GalleryStore::get().requestIcon(m_slug);
        this->schedule(schedule_selector(IconDetailPopup::awaitMeta), 0.25f);
    }

    refresh();
    return true;
}

void IconDetailPopup::awaitMeta(float) {
    auto& store = GalleryStore::get();
    auto const* icon = store.find(m_slug);
    if (!icon) {
        this->unschedule(schedule_selector(IconDetailPopup::awaitMeta));
        return;
    }
    // Sigue en curso: nada que repintar todavia.
    if (!icon->metaLoaded && store.isLoading(m_slug)) return;

    this->unschedule(schedule_selector(IconDetailPopup::awaitMeta));
    if (!icon->metaLoaded) {
        // Se agoto sin datos (la descarga fallo): decirlo en vez de dejar el
        // "Cargando..." colgado.
        if (m_status) m_status->setString(tr("icon-gallery.status.failed").c_str());
    }
    refresh();
}

void IconDetailPopup::buildStatic() {
    auto const content = m_mainLayer->getContentSize();

    float const previewCX = 16.f + kPreviewBox / 2.f;
    float const previewCY = content.height - 32.f - kPreviewBox / 2.f;

    if (auto* well = paimon::SpriteHelper::createColorPanel(
            kPreviewBox, kPreviewBox, {0, 0, 0}, 120, 8.f)) {
        well->setAnchorPoint({0.5f, 0.5f});
        well->setPosition({previewCX, previewCY});
        m_mainLayer->addChild(well);
    }

    m_previewBox = CCNode::create();
    m_previewBox->setContentSize({kPreviewBox, kPreviewBox});
    m_previewBox->setAnchorPoint({0.5f, 0.5f});
    m_previewBox->setPosition({previewCX, previewCY});
    m_mainLayer->addChild(m_previewBox, 2);

    float const infoX = 16.f + kPreviewBox + 14.f;
    float const infoW = content.width - infoX - 16.f;

    m_infoHost = CCNode::create();
    m_infoHost->setAnchorPoint({0.f, 1.f});
    m_infoHost->setContentSize({infoW, kPreviewBox + 10.f});
    m_infoHost->setPosition({infoX, content.height - 30.f});
    m_mainLayer->addChild(m_infoHost, 2);

    m_actionHost = CCNode::create();
    m_actionHost->setAnchorPoint({0.5f, 0.5f});
    m_actionHost->setContentSize({content.width - 32.f, 32.f});
    m_actionHost->setPosition({content.width / 2.f, 30.f});
    m_mainLayer->addChild(m_actionHost, 3);

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_status) {
        m_status->setAnchorPoint({0.5f, 0.5f});
        m_status->setScale(0.38f);
        m_status->setColor({170, 195, 230});
        m_status->setPosition({content.width / 2.f, 11.f});
        m_mainLayer->addChild(m_status, 3);
    }
}

void IconDetailPopup::refresh() {
    auto& store = GalleryStore::get();
    auto const* icon = store.find(m_slug);
    if (!icon || !m_infoHost || !m_previewBox) return;

    if (!m_preview) {
        if (auto* texture = store.previewTexture(m_slug)) {
            if (auto* sprite = CCSprite::createWithTexture(texture)) {
                auto const size = sprite->getContentSize();
                if (size.width > 0.f && size.height > 0.f) {
                    float const targetScale = std::min(kPreviewBox / size.width,
                                                      kPreviewBox / size.height) * 0.90f;
                    sprite->setPosition({kPreviewBox / 2.f, kPreviewBox / 2.f});
                    m_previewBox->addChild(sprite, 1);
                    m_preview = sprite;

                    // Entrada animada suave con rebote + flotación sutil idílica.
                    sprite->setScale(0.f);
                    sprite->runAction(CCEaseBackOut::create(CCScaleTo::create(0.24f, targetScale)));
                    auto* floatUp = CCMoveBy::create(1.3f, {0.f, 2.5f});
                    auto* floatDown = floatUp->reverse();
                    auto* floatSeq = CCSequence::create(floatUp, floatDown, nullptr);
                    sprite->runAction(CCRepeatForever::create(floatSeq));
                }
            }
        }
    }
    if (!m_preview && !m_spinner && store.isLoading(m_slug)) {
        m_spinner = LoadingSpinner::create(26.f);
        if (m_spinner) {
            m_spinner->setPosition({kPreviewBox / 2.f, kPreviewBox / 2.f});
            m_previewBox->addChild(m_spinner, 0);
        }
    } else if (m_preview && m_spinner) {
        m_spinner->removeFromParent();
        m_spinner = nullptr;
    }

    m_infoHost->removeAllChildren();
    float const infoW = m_infoHost->getContentSize().width;
    float y = m_infoHost->getContentSize().height;

    auto addRow = [&](std::string const& label, std::string const& value) {
        if (value.empty()) return;
        auto* row = makeInfoRow(infoW, label, value);
        y -= 15.f;
        row->setPosition({0.f, y});
        m_infoHost->addChild(row);
    };

    if (!icon->metaLoaded) {
        auto* wait = CCLabelBMFont::create(tr("icon-gallery.loading").c_str(), "chatFont.fnt");
        if (wait) {
            wait->setAnchorPoint({0.f, 1.f});
            wait->setScale(0.42f);
            wait->setColor({170, 195, 230});
            wait->setPosition({0.f, y - 4.f});
            m_infoHost->addChild(wait);
        }
    } else {
        addRow(tr("icon-gallery.field.author"), icon->author);
        addRow(tr("icon-gallery.field.type"), iconTypeLabel(icon->type));
        addRow(tr("icon-gallery.field.date"), formatDate(icon->createdAtMs));

        if (icon->isCollab && !icon->collabWith.empty()) {
            std::string names;
            for (auto const& who : icon->collabWith) {
                if (!names.empty()) names += ", ";
                names += who;
            }
            addRow(tr("icon-gallery.field.collab"), names);
        }

        // Colores sugeridos por el autor: tres muestras en fila.
        if (icon->hasColors) {
            y -= 18.f;
            auto* label = CCLabelBMFont::create(
                tr("icon-gallery.field.colors").c_str(), "chatFont.fnt");
            if (label) {
                label->setAnchorPoint({0.f, 0.5f});
                label->setScale(0.38f);
                label->setColor({150, 178, 218});
                label->setPosition({0.f, y + 6.f});
                m_infoHost->addChild(label);
            }
            float x = label ? label->getContentWidth() * label->getScale() + 6.f : 0.f;
            for (auto color : {icon->color1, icon->color2, icon->colorGlow}) {
                if (auto* swatch = paimon::SpriteHelper::createColorPanel(
                        14.f, 14.f, color, 255, 3.f)) {
                    swatch->setAnchorPoint({0.f, 0.5f});
                    swatch->setPosition({x, y + 6.f});
                    m_infoHost->addChild(swatch);
                    x += 18.f;
                }
            }
        }

        if (!icon->description.empty()) {
            y -= 6.f;
            float const descH = std::max(16.f, y - 2.f);
            auto* desc = SimpleTextArea::create(icon->description, "chatFont.fnt", 0.38f, infoW);
            if (desc) {
                desc->setAnchorPoint({0.f, 1.f});
                desc->setColor({205, 222, 245, 255});
                desc->setPosition({0.f, y});
                desc->setMaxLines(static_cast<int>(descH / 11.f));
                m_infoHost->addChild(desc);
            }
        }
    }

    rebuildActions();
}

void IconDetailPopup::rebuildActions() {
    if (!m_actionHost) return;
    m_actionHost->removeAllChildren();

    auto& store = GalleryStore::get();
    auto const* icon = store.find(m_slug);
    if (!icon) return;

    auto* menu = CCMenu::create();
    menu->setPosition({m_actionHost->getContentSize().width / 2.f, 16.f});
    m_actionHost->addChild(menu);

    struct Action {
        std::string label;
        char const* sprite;
        std::function<void()> run;
    };
    std::vector<Action> actions;

    bool const installed = store.isInstalled(m_slug);
    bool const ready = icon->metaLoaded && !m_busy;

    if (!installed) {
        actions.push_back({tr("icon-gallery.action.download"), "GJ_button_01.png",
                           [this] { onDownload(); }});
    } else {
        if (GalleryInstaller::moreIconsAvailable()) {
            bool const equipped = GalleryInstaller::isEquipped(m_slug, icon->type);
            actions.push_back({
                equipped ? tr("icon-gallery.action.equipped")
                         : tr("icon-gallery.action.equip"),
                equipped ? "GJ_button_02.png" : "GJ_button_01.png",
                [this] { onEquip(); }});
        }
        actions.push_back({tr("icon-gallery.action.remove"), "GJ_button_06.png",
                           [this] { onRemove(); }});
    }

    // Los botones se reparten centrados.
    float const gap = 10.f;
    std::vector<CCMenuItemSpriteExtra*> made;
    float total = 0.f;
    for (auto const& action : actions) {
        auto* spr = ButtonSprite::create(action.label.c_str(), "bigFont.fnt",
                                         action.sprite, 0.55f);
        if (!spr) continue;
        auto run = action.run;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [run](CCMenuItemSpriteExtra*) { if (run) run(); });
        btn->setEnabled(ready);
        if (!ready) spr->setOpacity(110);
        made.push_back(btn);
        total += btn->getScaledContentSize().width;
    }
    if (made.empty()) return;
    total += gap * static_cast<float>(made.size() - 1);

    float x = -total / 2.f;
    for (auto* btn : made) {
        float const w = btn->getScaledContentSize().width;
        btn->setPosition({x + w / 2.f, 0.f});
        menu->addChild(btn);
        x += w + gap;
    }
}

void IconDetailPopup::setBusy(bool busy, char const* message) {
    m_busy = busy;
    if (m_status) m_status->setString(message ? message : "");
    rebuildActions();
}

void IconDetailPopup::notifyChanged() {
    if (m_onChanged) m_onChanged();
}

void IconDetailPopup::onDownload() {
    if (m_busy) return;
    setBusy(true, tr("icon-gallery.status.downloading").c_str());

    Ref<IconDetailPopup> self = this;
    GalleryStore::get().fetchPackage(m_slug, [self](Result<GalleryPackage> res) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (!self->getParent()) return;

        if (!res) {
            self->setBusy(false, "");
            PaimonNotify::show(
                fmt::format("{}: {}", tr("icon-gallery.status.failed"), res.unwrapErr()),
                NotificationIcon::Error);
            return;
        }

        auto pkg = std::move(res.unwrap());

        if (!GalleryInstaller::moreIconsAvailable()) {
            // Sin More Icons no hay donde registrarlo: se guardan los archivos
            // y se explica que falta, en vez de fallar en silencio.
            auto saved = GalleryInstaller::saveOnly(pkg);
            self->setBusy(false, "");
            if (!saved) {
                PaimonNotify::show(saved.unwrapErr(), NotificationIcon::Error);
                return;
            }
            FLAlertLayer::create(
                tr("icon-gallery.moreicons.title").c_str(),
                tr("icon-gallery.moreicons.body").c_str(),
                "OK")->show();
            return;
        }

        auto installed = GalleryInstaller::install(pkg);
        self->setBusy(false, "");
        if (!installed) {
            PaimonNotify::show(installed.unwrapErr(), NotificationIcon::Error);
            return;
        }
        PaimonNotify::show(tr("icon-gallery.status.installed"), NotificationIcon::Success);
        self->notifyChanged();
        self->refresh();
    });
}

void IconDetailPopup::onEquip() {
    auto const* icon = GalleryStore::get().find(m_slug);
    if (!icon) return;

    if (!GalleryInstaller::equip(m_slug, icon->type)) {
        PaimonNotify::show(tr("icon-gallery.status.equip_failed"), NotificationIcon::Error);
        return;
    }
    PaimonNotify::show(tr("icon-gallery.status.equipped"), NotificationIcon::Success);
    notifyChanged();
    rebuildActions();
}

void IconDetailPopup::onRemove() {
    auto const* icon = GalleryStore::get().find(m_slug);
    if (!icon) return;

    Ref<IconDetailPopup> self = this;
    auto slug = m_slug;
    auto type = icon->type;
    geode::createQuickPopup(
        tr("icon-gallery.remove.title").c_str(),
        tr("icon-gallery.remove.body").c_str(),
        tr("icon-gallery.cancel").c_str(), tr("icon-gallery.action.remove").c_str(),
        [self, slug, type](auto*, bool confirmed) {
            if (!confirmed) return;
            if (paimon::isRuntimeShuttingDown() || !self) return;

            auto res = GalleryInstaller::uninstall(slug, type);
            if (!res) {
                PaimonNotify::show(res.unwrapErr(), NotificationIcon::Error);
                return;
            }
            PaimonNotify::show(tr("icon-gallery.status.removed"), NotificationIcon::Success);
            if (!self->getParent()) return;
            self->notifyChanged();
            self->refresh();
        });
}

}  // namespace paimon::icon_gallery
