#include "GifToSheetPopup.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/general.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

using namespace geode::prelude;

namespace paimon::dev {

namespace {

constexpr float kPopupW = 380.f;
constexpr float kPopupH = 250.f;
// stb writes the whole sheet in one image; keep it within common gpu limits.
constexpr long long kMaxSheetSide = 16384;
// 4096x4096 rgba = 64 mib; the side cap alone still allows a 1 gib sheet.
constexpr long long kMaxSheetPixels = 4096LL * 4096;

std::string sheetJson(GIFDecoder::GIFData const& gif, int cols, int rows) {
    std::string delays;
    for (auto const& f : gif.frames) {
        if (!delays.empty()) delays += ",";
        delays += std::to_string(f.delayMs);
    }
    return fmt::format(
        R"({{"frameW":{},"frameH":{},"cols":{},"rows":{},"count":{},"delaysMs":[{}]}})",
        gif.width, gif.height, cols, rows, gif.frames.size(), delays
    );
}

} // namespace

GifToSheetPopup* GifToSheetPopup::create() {
    if (!paimon::modules::isEnabled("paimbnails.devtools.menu")) return nullptr;

    auto* ret = new GifToSheetPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

GifToSheetPopup::~GifToSheetPopup() {
    paimon::ui::detachGeodeTextInput(m_colsInput);
}

bool GifToSheetPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    this->setID("gif-to-sheet-popup"_spr);
    this->setTitle("GIF a Sheet");
    this->addInfoButton("GIF a Sheet",
        "Convierte un <cy>GIF animado</c> en un <cg>spritesheet PNG</c> y un archivo "
        "<co>JSON</c> con el grid y los delays. Ajusta <cy>Columnas</c> para cambiar la "
        "forma del sheet; deja <cg>auto</c> para una rejilla casi cuadrada.");

    WeakRef<GifToSheetPopup> self = this;

    auto desc = paimon::ui::makeText(
        "Convierte un GIF animado en spritesheet PNG + JSON",
        kPopupW - 40.f, 0.4f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
    desc->setPosition({kPopupW / 2.f, kPopupH - 40.f});
    desc->setID("description-label"_spr);
    m_mainLayer->addChild(desc);

    auto* previewPanel = paimon::ui::makePanel({112.f, 128.f}, "Preview");
    previewPanel->setPosition({18.f, 70.f});
    m_mainLayer->addChild(previewPanel);

    auto* previewBox = paimon::ui::makeInset({92.f, 86.f}, 220);
    previewBox->setPosition({10.f, 10.f});
    previewPanel->addChild(previewBox);

    m_previewHint = CCLabelBMFont::create("Sin GIF", "bigFont.fnt");
    m_previewHint->setScale(0.32f);
    m_previewHint->setColor(paimon::ui::palette::dim);
    m_previewHint->setPosition({74.f, 123.f});
    m_previewHint->setID("preview-hint"_spr);
    m_mainLayer->addChild(m_previewHint, 2);

    auto* settingsPanel = paimon::ui::makePanel({220.f, 128.f}, "Ajustes");
    settingsPanel->setPosition({142.f, 70.f});
    m_mainLayer->addChild(settingsPanel);

    const float innerTop = 128.f - paimon::ui::kPanelHeader;

    m_fileLabel = CCLabelBMFont::create("Ningun archivo seleccionado", "goldFont.fnt");
    m_fileLabel->setAnchorPoint({0.f, 0.5f});
    m_fileLabel->setScale(0.42f);
    m_fileLabel->setPosition({12.f, innerTop - 10.f});
    m_fileLabel->setID("file-label"_spr);
    settingsPanel->addChild(m_fileLabel);

    m_infoLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_infoLabel->setAnchorPoint({0.f, 1.f});
    m_infoLabel->setScale(0.3f);
    m_infoLabel->setColor(paimon::ui::palette::info);
    m_infoLabel->setPosition({12.f, innerTop - 26.f});
    m_infoLabel->setID("info-label"_spr);
    settingsPanel->addChild(m_infoLabel);

    auto colsLabel = paimon::ui::makeLabel("Columnas:", 90.f, 0.4f);
    colsLabel->setAnchorPoint({0.f, 0.5f});
    colsLabel->setPosition({12.f, 18.f});
    colsLabel->setID("cols-label"_spr);
    settingsPanel->addChild(colsLabel);

    m_colsInput = TextInput::create(64.f, "auto");
    m_colsInput->setCommonFilter(CommonFilter::Uint);
    m_colsInput->setMaxCharCount(3);
    m_colsInput->setScale(0.75f);
    m_colsInput->setPosition({120.f, 18.f});
    m_colsInput->setCallback([self](std::string const&) {
        if (auto* popup = self.lock().data()) popup->refreshInfo();
    });
    m_colsInput->setID("cols-input"_spr);
    settingsPanel->addChild(m_colsInput);

    auto* menu = CCMenu::create();
    menu->setID("actions-menu"_spr);
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 3);

    auto* pickBtn = paimon::ui::makeButton("Elegir GIF",
        [self] { if (auto* popup = self.lock().data()) popup->onPickGif(); },
        paimon::ui::Btn::Cyan, 0.f, 0.7f);
    pickBtn->setID("pick-button"_spr);
    pickBtn->setPosition({kPopupW / 2.f - 78.f, 42.f});
    menu->addChild(pickBtn);

    auto* exportBtn = paimon::ui::makeButton("Exportar",
        [self] { if (auto* popup = self.lock().data()) popup->onExport(); },
        paimon::ui::Btn::Green, 0.f, 0.7f);
    exportBtn->setID("export-button"_spr);
    exportBtn->setPosition({kPopupW / 2.f + 78.f, 42.f});
    menu->addChild(exportBtn);

    auto hint = paimon::ui::makeText(
        "Exporta nombre.png + nombre.json (grid, delays en ms)",
        kPopupW - 30.f, 0.35f, paimon::ui::palette::dim, kCCTextAlignmentCenter);
    hint->setPosition({kPopupW / 2.f, 16.f});
    hint->setID("hint-label"_spr);
    m_mainLayer->addChild(hint);

    refreshInfo();
    return true;
}

void GifToSheetPopup::onPickGif() {
    if (m_busyOverlay) return;
    WeakRef<GifToSheetPopup> self = this;
    pt::pickGif([self](Result<std::optional<std::filesystem::path>> res) {
        auto ref = self.lock();
        auto* popup = ref.data();
        if (!popup || !popup->getParent()) return;
        if (res.isErr()) {
            PaimonNotify::create("No se pudo abrir el dialogo de archivos.", NotificationIcon::Error)->show();
            return;
        }
        auto opt = res.unwrap();
        if (!opt) return;
        popup->loadGif(*opt);
    });
}

void GifToSheetPopup::loadGif(std::filesystem::path const& path) {
    auto dataRes = utils::file::readBinary(path);
    if (dataRes.isErr()) {
        PaimonNotify::create("No se pudo leer el archivo.", NotificationIcon::Error)->show();
        return;
    }
    auto bytes = std::make_shared<std::vector<uint8_t>>(dataRes.unwrap());
    if (!GIFDecoder::isGIF(bytes->data(), bytes->size())) {
        PaimonNotify::create("El archivo no es un GIF valido.", NotificationIcon::Warning)->show();
        return;
    }

    showBusy("Decodificando GIF");
    WeakRef<GifToSheetPopup> self = this;
    bool const started = paimon::ThreadTracker::get().spawn([self, bytes, path] {
        geode::utils::thread::setName("Paimon GIF Sheet Decode");
        std::shared_ptr<GIFDecoder::GIFData> gif;
        try {
            gif = std::make_shared<GIFDecoder::GIFData>(GIFDecoder::decode(bytes->data(), bytes->size()));
        } catch (std::exception const& e) {
            geode::log::warn("[GifToSheet] Decode failed: {}", e.what());
            Loader::get()->queueInMainThread([self] {
                auto ref = self.lock();
                if (auto* popup = ref.data()) popup->hideBusy();
                PaimonNotify::create("No se pudo decodificar el GIF.", NotificationIcon::Error)->show();
            });
            return;
        }
        Loader::get()->queueInMainThread([self, gif, path] {
            auto ref = self.lock();
            auto* popup = ref.data();
            if (!popup) return;
            popup->hideBusy();
            if (gif->frames.empty()) {
                PaimonNotify::create("No se pudo decodificar el GIF.", NotificationIcon::Error)->show();
                return;
            }
            popup->applyDecoded(path, gif);
        });
    });
    if (!started) hideBusy();
}

void GifToSheetPopup::applyDecoded(std::filesystem::path const& path, std::shared_ptr<GIFDecoder::GIFData> gif) {
    m_gifPath = path;
    m_gif = std::move(gif);

    int count = static_cast<int>(m_gif->frames.size());
    // near-square sheet by default: cols*framew ~ rows*frameh.
    double ideal = std::sqrt(
        static_cast<double>(count) * m_gif->height / std::max(1, m_gif->width));
    m_autoCols = std::clamp(static_cast<int>(std::lround(ideal)), 1, count);

    auto name = utils::string::pathToString(path.filename());
    if (name.size() > 30) name = name.substr(0, 28) + "..";
    m_fileLabel->setString(name.c_str());

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }
    auto const& first = m_gif->frames.front();
    auto* tex = new CCTexture2D();
    if (tex->initWithData(
        first.pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
        first.width, first.height,
        CCSize(static_cast<float>(first.width), static_cast<float>(first.height))
    )) {
        m_previewSprite = CCSprite::createWithTexture(tex);
        float s = std::min(92.f / first.width, 92.f / first.height);
        m_previewSprite->setScale(std::min(s, 1.f));
        m_previewSprite->setPosition({74.f, 123.f});
        m_mainLayer->addChild(m_previewSprite, 1);
        if (m_previewHint) m_previewHint->setVisible(false);
    }
    tex->release();

    refreshInfo();
}

int GifToSheetPopup::currentCols() const {
    if (!m_gif || m_gif->frames.empty()) return 1;
    int count = static_cast<int>(m_gif->frames.size());
    int cols = m_autoCols;
    if (m_colsInput && !m_colsInput->getString().empty()) {
        cols = utils::numFromString<int>(m_colsInput->getString()).unwrapOr(m_autoCols);
    }
    return std::clamp(cols, 1, count);
}

void GifToSheetPopup::refreshInfo() {
    if (!m_infoLabel) return;
    if (!m_gif || m_gif->frames.empty()) {
        m_infoLabel->setString("Elige un GIF para empezar");
        return;
    }
    int count = static_cast<int>(m_gif->frames.size());
    int cols = currentCols();
    int rows = (count + cols - 1) / cols;
    m_infoLabel->setString(fmt::format(
        "{} frames de {}x{} px\nGrid {}x{} -> sheet {}x{} px",
        count, m_gif->width, m_gif->height,
        cols, rows,
        static_cast<long long>(cols) * m_gif->width,
        static_cast<long long>(rows) * m_gif->height
    ).c_str());
}

void GifToSheetPopup::onExport() {
    if (m_busyOverlay) return;
    if (!m_gif || m_gif->frames.empty()) {
        PaimonNotify::create("Primero elige un GIF.", NotificationIcon::Warning)->show();
        return;
    }
    int cols = currentCols();
    int rows = (static_cast<int>(m_gif->frames.size()) + cols - 1) / cols;
    long long sheetW = static_cast<long long>(cols) * m_gif->width;
    long long sheetH = static_cast<long long>(rows) * m_gif->height;
    if (sheetW > kMaxSheetSide || sheetH > kMaxSheetSide || sheetW * sheetH > kMaxSheetPixels) {
        PaimonNotify::create(
            "El sheet resultante es demasiado grande. Ajusta las columnas.",
            NotificationIcon::Warning
        )->show();
        return;
    }

    auto defaultName = utils::string::pathToString(m_gifPath.stem()) + "_sheet.png";
    WeakRef<GifToSheetPopup> self = this;
    pt::saveImage(defaultName, [self](Result<std::optional<std::filesystem::path>> res) {
        auto ref = self.lock();
        auto* popup = ref.data();
        if (!popup || !popup->getParent()) return;
        if (res.isErr()) {
            PaimonNotify::create("No se pudo abrir el dialogo de guardado.", NotificationIcon::Error)->show();
            return;
        }
        auto opt = res.unwrap();
        if (!opt) return;
        popup->exportTo(*opt);
    });
}

void GifToSheetPopup::exportTo(std::filesystem::path pngPath) {
    if (pngPath.extension() != ".png") pngPath.replace_extension(".png");
    auto jsonPath = pngPath;
    jsonPath.replace_extension(".json");

    showBusy("Exportando sheet");
    auto gif = m_gif;
    int cols = currentCols();

    WeakRef<GifToSheetPopup> self = this;
    bool const started = paimon::ThreadTracker::get().spawn([self, gif, cols, pngPath, jsonPath] {
        geode::utils::thread::setName("Paimon GIF Sheet Export");
        int count = static_cast<int>(gif->frames.size());
        int rows = (count + cols - 1) / cols;
        int w = gif->width;
        int h = gif->height;
        size_t sheetW = static_cast<size_t>(cols) * w;
        size_t sheetH = static_cast<size_t>(rows) * h;

        std::vector<uint8_t> sheet;
        try {
            sheet.assign(sheetW * sheetH * 4, 0);
        } catch (std::bad_alloc const&) {
            Loader::get()->queueInMainThread([self] {
                auto ref = self.lock();
                if (auto* popup = ref.data()) popup->hideBusy();
                PaimonNotify::create("El sheet no cabe en memoria.", NotificationIcon::Error)->show();
            });
            return;
        }
        size_t frameRowBytes = static_cast<size_t>(w) * 4;
        for (int i = 0; i < count; ++i) {
            auto const& frame = gif->frames[static_cast<size_t>(i)];
            if (frame.pixels.size() < frameRowBytes * h) continue;
            size_t dstX = static_cast<size_t>(i % cols) * w;
            size_t dstY = static_cast<size_t>(i / cols) * h;
            for (int y = 0; y < h; ++y) {
                std::memcpy(
                    &sheet[((dstY + y) * sheetW + dstX) * 4],
                    &frame.pixels[static_cast<size_t>(y) * frameRowBytes],
                    frameRowBytes
                );
            }
        }

        bool ok = ImageConverter::saveRGBAToPNG(
            sheet.data(),
            static_cast<uint32_t>(sheetW), static_cast<uint32_t>(sheetH),
            pngPath
        );
        if (ok) {
            auto jsonRes = utils::file::writeString(jsonPath, sheetJson(*gif, cols, rows));
            ok = jsonRes.isOk();
        }

        Loader::get()->queueInMainThread([self, ok, pngPath] {
            auto ref = self.lock();
            auto* popup = ref.data();
            if (popup) popup->hideBusy();
            if (ok) {
                PaimonNotify::create(
                    fmt::format("Sheet exportado: {}", utils::string::pathToString(pngPath.filename())),
                    NotificationIcon::Success
                )->show();
            } else {
                PaimonNotify::create("Fallo la exportacion del sheet.", NotificationIcon::Error)->show();
            }
        });
    });
    if (!started) hideBusy();
}

void GifToSheetPopup::showBusy(std::string const& text) {
    if (m_busyOverlay) return;
    m_busyOverlay = PaimonLoadingOverlay::create(text, 30.f);
    if (m_busyOverlay) m_busyOverlay->showLocal(m_mainLayer, 300);
}

void GifToSheetPopup::hideBusy() {
    if (!m_busyOverlay) return;
    m_busyOverlay->dismiss();
    m_busyOverlay = nullptr;
}

} // namespace paimon::dev
