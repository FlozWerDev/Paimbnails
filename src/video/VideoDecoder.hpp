#pragma once

#include <cstdint>
#include <cstring>
#include <cfloat>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#elif defined(__ANDROID__)
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace paimon {

enum class VideoColorMatrix { Auto, BT601, BT709 };

struct VideoFrame {
    uint8_t* planeY   = nullptr;
    uint8_t* planeCb  = nullptr;
    uint8_t* planeCr  = nullptr;
    int      strideY  = 0;
    int      strideCb = 0;
    int      strideCr = 0;
    int      width    = 0;
    int      height   = 0;
    double   pts      = 0.0;
    std::atomic<bool> ready{false};

    // 32-byte aligned for simd.
    static size_t alignedSize(int w, int h) {
        int alignedStride = ((w + 31) / 32) * 32;
        return static_cast<size_t>(alignedStride) * h;
    }

    static int alignedStride(int w) {
        return ((w + 31) / 32) * 32;
    }

    static uint8_t* allocAligned(size_t size) {
        size = ((size + 31) / 32) * 32;
#ifdef _WIN32
        return static_cast<uint8_t*>(_aligned_malloc(size, 32));
#elif defined(__ANDROID__)
        return static_cast<uint8_t*>(memalign(32, size));
#else
        return static_cast<uint8_t*>(std::aligned_alloc(32, size));
#endif
    }

    static void freeAligned(uint8_t* ptr) {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    void clear() {
        planeY = planeCb = planeCr = nullptr;
        strideY = strideCb = strideCr = 0;
        width = height = 0;
        pts = 0.0;
        ready.store(false, std::memory_order_release);
    }
};

class IVideoDecoder {
public:
    using Frame = VideoFrame;

    virtual ~IVideoDecoder() = default;

    virtual bool open(const std::string& path) = 0;
    virtual void startDecoding() = 0;
    virtual void stopDecoding() = 0;
    virtual void seekTo(double seconds) = 0;
    virtual double getDuration() const = 0;
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    virtual bool isFinished() const = 0;

    // coded size before backend downscale; picks bt.709 vs bt.601.
    virtual int getNativeWidth() const { return getWidth(); }
    virtual int getNativeHeight() const { return getHeight(); }

    // content color description; auto keeps the size-based bt.709 heuristic.
    virtual VideoColorMatrix getColorMatrix() const { return VideoColorMatrix::Auto; }
    virtual bool isFullRange() const { return false; }
    // clockwise display rotation in degrees (0/90/180/270).
    virtual int getRotationDegrees() const { return 0; }

    virtual bool skipFrame() = 0;

    virtual double peekNextPTS() const = 0;
    virtual double peekSecondPTS() const { return DBL_MAX; }

    // borrowed until releaseframe(); no seek/stop calls in between.
    virtual const Frame* peekFrame() { return nullptr; }

    virtual void releaseFrame() {}

    // pts restarts at 0, false = must seek.
    virtual bool setLooping(bool) { return false; }

    static std::unique_ptr<IVideoDecoder> create(const std::string& path);
};

class VideoRingBuffer {
public:
    using Frame = VideoFrame;

    VideoRingBuffer() = default;

    ~VideoRingBuffer() { freeSlots(); }

    static int computeSlotCount(int w, int h) {
        size_t slotBytes = Frame::alignedSize(w, h)
                         + 2 * Frame::alignedSize((w + 1) / 2, (h + 1) / 2);
        if (slotBytes > 8 * 1024 * 1024)  return 3;
        if (slotBytes > 2 * 1024 * 1024)  return 5;
        return 8;
    }

    bool init(int w, int h) {
        freeSlots();
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
        m_width = w;
        m_height = h;
        m_capacity = computeSlotCount(w, h);
        int uvH = (h + 1) / 2;
        int uvW = (w + 1) / 2;

        int alignedStrideY  = Frame::alignedStride(w);
        int alignedStrideUV = Frame::alignedStride(uvW);

        m_slots.reset(new (std::nothrow) Frame[m_capacity]);
        if (!m_slots) {
            freeSlots();
            return false;
        }
        for (int i = 0; i < m_capacity; ++i) {
            m_slots[i].planeY  = Frame::allocAligned(Frame::alignedSize(w, h));
            m_slots[i].planeCb = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
            m_slots[i].planeCr = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
            if (!m_slots[i].planeY || !m_slots[i].planeCb || !m_slots[i].planeCr) {
                freeSlots();
                return false;
            }
            m_slots[i].strideY  = alignedStrideY;
            m_slots[i].strideCb = alignedStrideUV;
            m_slots[i].strideCr = alignedStrideUV;
            m_slots[i].width    = w;
            m_slots[i].height   = h;
            m_slots[i].ready.store(false, std::memory_order_release);
        }
        return true;
    }

    Frame* nextWrite() {
        if (!m_slots) return nullptr;
        int w = m_writeIdx.load(std::memory_order_relaxed);
        if ((w + 1) % m_capacity == m_readIdx.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[w];
    }

    void commitWrite() {
        if (!m_slots) return;
        auto idx = m_writeIdx.load(std::memory_order_relaxed);
        m_slots[idx].ready.store(true, std::memory_order_release);
        m_writeIdx.store((idx + 1) % m_capacity, std::memory_order_release);
        m_readableCv.notify_one();
    }

    Frame* nextRead() {
        if (!m_slots) return nullptr;
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return nullptr;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[r];
    }

    const Frame* peekRead() const {
        if (!m_slots) return nullptr;
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return nullptr;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[r];
    }

    void commitRead() {
        if (!m_slots) return;
        auto idx = m_readIdx.load(std::memory_order_relaxed);
        m_slots[idx].ready.store(false, std::memory_order_release);
        m_readIdx.store((idx + 1) % m_capacity, std::memory_order_release);
        m_writableCv.notify_one();
    }

    bool skipRead() {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return false;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return false;
        m_slots[r].ready.store(false, std::memory_order_release);
        m_readIdx.store((r + 1) % m_capacity, std::memory_order_release);
        m_writableCv.notify_one();
        return true;
    }

    double peekNextPTS() const {
        int r = m_readIdx.load(std::memory_order_acquire);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return DBL_MAX;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return DBL_MAX;
        return m_slots[r].pts;
    }

    // second readable pts or dbl_max if none.
    double peekSecondPTS() const {
        int r = m_readIdx.load(std::memory_order_acquire);
        int w = m_writeIdx.load(std::memory_order_acquire);
        if (r == w) return DBL_MAX;
        int next = (r + 1) % m_capacity;
        if (next == w) return DBL_MAX;
        if (!m_slots[next].ready.load(std::memory_order_acquire)) return DBL_MAX;
        return m_slots[next].pts;
    }

    bool isEmpty() const {
        return m_readIdx.load(std::memory_order_acquire) ==
               m_writeIdx.load(std::memory_order_acquire);
    }

    bool isFull() const {
        if (!m_slots) return true;
        int next = (m_writeIdx.load(std::memory_order_relaxed) + 1) % m_capacity;
        return next == m_readIdx.load(std::memory_order_acquire);
    }

    // re-check isfull()/nextwrite() after waiting.
    bool waitForWritable(int timeoutMs, const std::atomic<bool>* aliveFlag = nullptr) {
        if (!isFull()) return true;
        std::unique_lock<std::mutex> lk(m_writableMtx);
        m_writableCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            if (aliveFlag && !aliveFlag->load(std::memory_order_relaxed)) return true;
            return !isFull();
        });
        return !isFull();
    }

    void wakeAll() {
        m_readableCv.notify_all();
        m_writableCv.notify_all();
    }

    int getWidth()  const { return m_width; }
    int getHeight() const { return m_height; }
    int getCapacity() const { return m_capacity; }

private:
    void freeSlots() {
        for (int i = 0; m_slots && i < m_capacity; ++i) {
            Frame::freeAligned(m_slots[i].planeY);
            Frame::freeAligned(m_slots[i].planeCb);
            Frame::freeAligned(m_slots[i].planeCr);
            m_slots[i].planeY = m_slots[i].planeCb = m_slots[i].planeCr = nullptr;
            m_slots[i].ready.store(false, std::memory_order_release);
        }
        m_slots.reset();
        m_capacity = 0;
        m_width = m_height = 0;
        m_writeIdx.store(0, std::memory_order_relaxed);
        m_readIdx.store(0, std::memory_order_relaxed);
    }

    std::unique_ptr<Frame[]> m_slots;
    int m_capacity = 0;
    std::atomic<int> m_writeIdx{0};
    std::atomic<int> m_readIdx{0};
    int m_width  = 0;
    int m_height = 0;

    // waiters are notified without locking the ring's fast path.
    mutable std::mutex m_readableMtx;
    mutable std::mutex m_writableMtx;
    mutable std::condition_variable m_readableCv;
    mutable std::condition_variable m_writableCv;
};

}
