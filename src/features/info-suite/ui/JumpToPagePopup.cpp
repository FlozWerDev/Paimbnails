#include "JumpToPagePopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/utils/general.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr float kPopupW = 300.f;
constexpr float kPopupH = 190.f;

// the server tops out well below this; the cap only exists so a pasted number
// cannot overflow the int sent to loadpage().
constexpr int kHardMaxPage = 1000000;

} // namespace

JumpToPagePopup* JumpToPagePopup::create(int currentPage, int pageCount,
                                         std::function<void(int)> onJump) {
    auto ret = new JumpToPagePopup();
    if (ret && ret->init(currentPage, pageCount, std::move(onJump))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool JumpToPagePopup::init(int currentPage, int pageCount, std::function<void(int)> onJump) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_onJump = std::move(onJump);
    m_pageCount = std::max(0, pageCount);
    m_page = std::clamp(currentPage < 1 ? 1 : currentPage, 1,
                        m_pageCount > 0 ? m_pageCount : kHardMaxPage);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle("Ir a la pagina");
    this->addInfoButton("Ir a la pagina",
        "Escribe el <cy>numero de pagina</c> o arrastra la barra para moverte "
        "rapido por la lista. <cg>Primera</c> y <cg>Ultima</c> saltan a los extremos.");

    bool const hasSlider = m_pageCount > 1;
    float const panelH = hasSlider ? 108.f : 78.f;
    CCSize const panelSize{content.width - 40.f, panelH};
    auto panel = paimon::ui::makePanel(panelSize, "Pagina");
    panel->setPosition({cx - panelSize.width / 2.f, 48.f});
    m_mainLayer->addChild(panel);

    float const panelCx = panelSize.width / 2.f;
    float inputY = panelSize.height - paimon::ui::kPanelHeader - 16.f;

    m_input = TextInput::create(120.f, "Pagina", "bigFont.fnt");
    m_input->setPosition({panelCx, inputY});
    m_input->setCommonFilter(CommonFilter::Uint);
    m_input->setMaxCharCount(7);
    m_input->setString(std::to_string(m_page));
    m_input->setCallback(paimon::ui::safeTextInputCallback<JumpToPagePopup>(
        WeakRef<JumpToPagePopup>(this), &JumpToPagePopup::onInputChanged));
    panel->addChild(m_input);

    m_rangeLabel = paimon::ui::makeText("", panelSize.width - 24.f, 0.5f,
        paimon::ui::palette::muted, kCCTextAlignmentCenter);
    m_rangeLabel->setPosition({panelCx, inputY - 24.f});
    panel->addChild(m_rangeLabel);

    // a scrubber only makes sense once we know how many pages there are.
    if (hasSlider) {
        m_slider = Slider::create(this, menu_selector(JumpToPagePopup::onSlider), 0.85f);
        m_slider->setPosition({panelCx, inputY - 50.f});
        panel->addChild(m_slider);
    }

    auto menu = CCMenu::create();
    menu->setPosition({cx, 26.f});
    menu->setContentSize({kPopupW - 40.f, 34.f});
    menu->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu);

    menu->addChild(paimon::ui::makeButton("Primera",
        [this] { this->onFirst(nullptr); }, paimon::ui::Btn::Gray, 0.f, 0.6f, "bigFont.fnt"));

    if (m_pageCount > 0) {
        menu->addChild(paimon::ui::makeButton("Ultima",
            [this] { this->onLast(nullptr); }, paimon::ui::Btn::Gray, 0.f, 0.6f, "bigFont.fnt"));
    }

    menu->addChild(paimon::ui::makeButton("Ir",
        [this] { this->onConfirm(nullptr); }, paimon::ui::Btn::Green, 0.f, 0.7f));

    menu->updateLayout();

    setPage(m_page, true, false);
    return true;
}

void JumpToPagePopup::setPage(int page, bool syncSlider, bool syncInput) {
    int maxPage = m_pageCount > 0 ? m_pageCount : kHardMaxPage;
    m_page = std::clamp(page, 1, maxPage);

    m_syncing = true;
    if (syncInput && m_input) m_input->setString(std::to_string(m_page));
    if (syncSlider && m_slider && m_pageCount > 1) {
        float ratio = static_cast<float>(m_page - 1) / static_cast<float>(m_pageCount - 1);
        m_slider->setValue(ratio);
        m_slider->updateBar();
    }
    m_syncing = false;

    refreshLabels();
}

void JumpToPagePopup::refreshLabels() {
    if (!m_rangeLabel) return;
    m_rangeLabel->setString(m_pageCount > 0
        ? fmt::format("Pagina {} de {}", m_page, m_pageCount).c_str()
        : fmt::format("Pagina {}  (total desconocido)", m_page).c_str());
}

void JumpToPagePopup::onSlider(CCObject* sender) {
    if (m_syncing || m_pageCount <= 1) return;

    auto* thumb = typeinfo_cast<SliderThumb*>(sender);
    if (!thumb) return;

    float ratio = std::clamp(thumb->getValue(), 0.f, 1.f);
    int page = 1 + static_cast<int>(std::lround(ratio * (m_pageCount - 1)));
    setPage(page, false, true);
}

void JumpToPagePopup::onInputChanged(std::string const& text) {
    if (m_syncing) return;
    if (text.empty()) return;

    auto parsed = geode::utils::numFromString<int>(text);
    if (!parsed.isOk()) return;
    setPage(parsed.unwrap(), true, false);
}

void JumpToPagePopup::onFirst(CCObject*) {
    setPage(1, true, true);
}

void JumpToPagePopup::onLast(CCObject*) {
    if (m_pageCount <= 0) return;
    setPage(m_pageCount, true, true);
}

void JumpToPagePopup::onConfirm(CCObject*) {
    auto callback = m_onJump;
    int page = m_page;
    this->onClose(nullptr);
    if (callback) callback(page);
}

void JumpToPagePopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_input);
    m_input = nullptr;
    Popup::onClose(sender);
}

} // namespace paimon::info
