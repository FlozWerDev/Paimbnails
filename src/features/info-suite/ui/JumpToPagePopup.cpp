#include "JumpToPagePopup.hpp"
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
constexpr float kPopupH = 180.f;

// The server tops out well below this; the cap only exists so a pasted number
// cannot overflow the int sent to loadPage().
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
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_onJump = std::move(onJump);
    m_pageCount = std::max(0, pageCount);
    m_page = std::clamp(currentPage < 1 ? 1 : currentPage, 1,
                        m_pageCount > 0 ? m_pageCount : kHardMaxPage);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle("Ir a la pagina");

    m_input = TextInput::create(120.f, "Pagina", "bigFont.fnt");
    m_input->setPosition({cx, content.height - 62.f});
    m_input->setCommonFilter(CommonFilter::Uint);
    m_input->setMaxCharCount(7);
    m_input->setString(std::to_string(m_page));
    m_input->setCallback(paimon::ui::safeTextInputCallback<JumpToPagePopup>(
        WeakRef<JumpToPagePopup>(this), &JumpToPagePopup::onInputChanged));
    m_mainLayer->addChild(m_input);

    m_rangeLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_rangeLabel->setScale(0.5f);
    m_rangeLabel->setColor({170, 170, 170});
    m_rangeLabel->setPosition({cx, content.height - 86.f});
    m_mainLayer->addChild(m_rangeLabel);

    // A scrubber only makes sense once we know how many pages there are.
    if (m_pageCount > 1) {
        m_slider = Slider::create(this, menu_selector(JumpToPagePopup::onSlider), 0.85f);
        m_slider->setPosition({cx, content.height - 108.f});
        m_mainLayer->addChild(m_slider);
    }

    auto menu = CCMenu::create();
    menu->setPosition({cx, 34.f});
    menu->setContentSize({kPopupW - 40.f, 34.f});
    menu->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu);

    auto firstSpr = ButtonSprite::create("Primera", "bigFont.fnt", "GJ_button_04.png", 0.6f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        firstSpr, this, menu_selector(JumpToPagePopup::onFirst)));

    auto goSpr = ButtonSprite::create("Ir", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        goSpr, this, menu_selector(JumpToPagePopup::onConfirm)));

    if (m_pageCount > 0) {
        auto lastSpr = ButtonSprite::create("Ultima", "bigFont.fnt", "GJ_button_04.png", 0.6f);
        menu->addChild(CCMenuItemSpriteExtra::create(
            lastSpr, this, menu_selector(JumpToPagePopup::onLast)));
    }

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
