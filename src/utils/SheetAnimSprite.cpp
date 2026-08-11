#include "SheetAnimSprite.hpp"
#include "SpriteHelper.hpp"

using namespace cocos2d;

namespace {

// Delays from assets load/paim_PaimonSheet.json (original paimon.gif).
constexpr int kPaimonFrameW = 200;
constexpr int kPaimonFrameH = 129;
constexpr int kPaimonCols = 7;
constexpr int kPaimonCount = 42;
constexpr int kPaimonDelaysMs[kPaimonCount] = {
    80, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
    280, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40,
    40, 40, 40, 40, 40, 80,
};

std::vector<float> delaysFromMs(int const* ms, int count) {
    std::vector<float> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        float sec = (ms[i] > 0 ? ms[i] : 40) / 1000.f;
        out.push_back(sec);
    }
    return out;
}

} // namespace

SheetAnimSprite* SheetAnimSprite::create(
    char const* path,
    int frameW,
    int frameH,
    int cols,
    int frameCount,
    std::vector<float> delaysSec
) {
    auto* ret = new SheetAnimSprite();
    if (ret && ret->initSheet(path, frameW, frameH, cols, frameCount, std::move(delaysSec))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

SheetAnimSprite* SheetAnimSprite::createPaimonMascot() {
    return create(
        "paim_PaimonSheet.png"_spr,
        kPaimonFrameW,
        kPaimonFrameH,
        kPaimonCols,
        kPaimonCount,
        delaysFromMs(kPaimonDelaysMs, kPaimonCount)
    );
}

bool SheetAnimSprite::initSheet(
    char const* path,
    int frameW,
    int frameH,
    int cols,
    int frameCount,
    std::vector<float> delaysSec
) {
    if (!path || frameW <= 0 || frameH <= 0 || cols <= 0 || frameCount <= 0) {
        return false;
    }
    if (static_cast<int>(delaysSec.size()) != frameCount) {
        return false;
    }
    if (!CCSprite::initWithFile(path)) {
        return false;
    }
    if (!paimon::SpriteHelper::isValidSprite(this)) {
        return false;
    }

    // setTextureRect works in points, not source pixels: Geode ships -hd/low
    // variants of the sheet, so derive the frame size from whichever texture
    // actually loaded instead of the original pixel dimensions.
    int rows = (frameCount + cols - 1) / cols;
    auto sheetSize = this->getContentSize();
    m_frameW = sheetSize.width / static_cast<float>(cols);
    m_frameH = sheetSize.height / static_cast<float>(rows);
    m_cols = cols;
    m_count = frameCount;
    m_delays = std::move(delaysSec);
    m_index = 0;
    m_timer = 0.f;
    m_playing = true;
    m_loop = true;

    applyFrame(0);
    return true;
}

void SheetAnimSprite::applyFrame(int index) {
    if (index < 0 || index >= m_count) return;
    int col = index % m_cols;
    int row = index / m_cols;
    this->setTextureRect(CCRect(
        col * m_frameW,
        row * m_frameH,
        m_frameW,
        m_frameH
    ));
}

void SheetAnimSprite::play() {
    m_playing = true;
    this->scheduleUpdate();
}

void SheetAnimSprite::pauseAnim() {
    m_playing = false;
    this->unscheduleUpdate();
}

void SheetAnimSprite::onEnter() {
    CCSprite::onEnter();
    if (m_playing && m_count > 1) {
        this->scheduleUpdate();
    }
}

void SheetAnimSprite::onExit() {
    this->unscheduleUpdate();
    CCSprite::onExit();
}

void SheetAnimSprite::update(float dt) {
    if (!m_playing || m_count <= 1) return;

    m_timer += dt;
    int guard = m_count + 1;
    while (guard-- > 0 && m_timer >= m_delays[static_cast<size_t>(m_index)]) {
        m_timer -= m_delays[static_cast<size_t>(m_index)];
        int next = m_index + 1;
        if (next >= m_count) {
            if (!m_loop) {
                m_playing = false;
                this->unscheduleUpdate();
                return;
            }
            next = 0;
        }
        m_index = next;
        applyFrame(m_index);
    }
}
