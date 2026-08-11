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

#ifdef _WIN32
#include <malloc.h>
#elif defined(__ANDROID__)
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace paimon {

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

    // 32-byte aligned plane allocation for SIMD copies.
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

// A decode worker that outlived its join timeout leaks its OS handles on
// purpose (releasing them would race the running thread). Android caps
// concurrent MediaCodec instances device-wide, so track the bleed.
void noteDetachedDecoder(const char* backend);
int detachedDecoderCount();

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

    // Skip the next frame without copying it.
    virtual bool skipFrame() = 0;

    virtual double peekNextPTS() const = 0;

    // Peek at the second frame; returns DBL_MAX when unavailable.
    virtual double peekSecondPTS() const { return 1e300; /* ~DBL_MAX */ }

    // Borrowed until releaseFrame(); no consuming/seek/stop calls in between.
    virtual const Frame* peekFrame() { return nullptr; }

    // Release a frame borrowed from peekFrame().
    virtual void releaseFrame() {}

    virtual bool isTerminal() const { return false; }

    // Rewind inside the decode loop at end of stream so the ring never drains.
    // PTS restarts at 0; callers must handle the backwards jump. Returns false
    // when the backend cannot do it and the caller must seek instead.
    virtual bool setLooping(bool) { return false; }

    static std::unique_ptr<IVideoDecoder> create(const std::string& path);
};

// Lock-free single-producer/single-consumer ring buffer with adaptive capacity.
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
        m_width = w;
        m_height = h;
        m_capacity = computeSlotCount(w, h);
        int uvH = (h + 1) / 2;
        int uvW = (w + 1) / 2;

        int alignedStrideY  = Frame::alignedStride(w);
        int alignedStrideCb = Frame::alignedStride(uvW);
        int alignedStrideCr = Frame::alignedStride(uvW);

        m_slots = std::make_unique<Frame[]>(m_capacity);
        for (int i = 0; i < m_capacity; ++i) {
            m_slots[i].planeY  = Frame::allocAligned(Frame::alignedSize(w, h));
            m_slots[i].planeCb = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
            m_slots[i].planeCr = Frame::allocAligned(Frame::alignedSize(uvW, uvH));
            if (!m_slots[i].planeY || !m_slots[i].planeCb || !m_slots[i].planeCr) {
                freeSlots();
                return false;
            }
            m_slots[i].strideY  = alignedStrideY;
            m_slots[i].strideCb = alignedStrideCb;
            m_slots[i].strideCr = alignedStrideCr;
            m_slots[i].width    = w;
            m_slots[i].height   = h;
            m_slots[i].ready.store(false, std::memory_order_release);
        }
        return true;
    }

    Frame* nextWrite() {
        int next = (m_writeIdx.load(std::memory_order_relaxed) + 1) % m_capacity;
        if (next == m_readIdx.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[m_writeIdx.load(std::memory_order_relaxed)];
    }

    void commitWrite() {
        auto idx = m_writeIdx.load(std::memory_order_relaxed);
        m_slots[idx].ready.store(true, std::memory_order_release);
        m_writeIdx.store((idx + 1) % m_capacity, std::memory_order_release);
        m_readableCv.notify_one();
    }

    Frame* nextRead() {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return nullptr;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[r];
    }

    const Frame* peekRead() const {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return nullptr;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[r];
    }

    void commitRead() {
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

    // Peek at the second readable PTS; returns DBL_MAX when unavailable.
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
        int next = (m_writeIdx.load(std::memory_order_relaxed) + 1) % m_capacity;
        return next == m_readIdx.load(std::memory_order_acquire);
    }

    // Wait for writable space or shutdown; re-check isFull()/nextWrite() after.
    template <typename Clock = std::chrono::steady_clock>
    bool waitForWritable(int timeoutMs, const std::atomic<bool>* aliveFlag = nullptr) {
        if (!isFull()) return true;
        std::unique_lock<std::mutex> lk(m_writableMtx);
        m_writableCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            if (aliveFlag && !aliveFlag->load(std::memory_order_relaxed)) return true;
            return !isFull();
        });
        return !isFull();
    }

    // Wait for a readable frame or shutdown.
    template <typename Clock = std::chrono::steady_clock>
    bool waitForReadable(int timeoutMs, const std::atomic<bool>* aliveFlag = nullptr) {
        if (!isEmpty()) return true;
        std::unique_lock<std::mutex> lk(m_readableMtx);
        m_readableCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            if (aliveFlag && !aliveFlag->load(std::memory_order_relaxed)) return true;
            return !isEmpty();
        });
        return !isEmpty();
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
        for (int i = 0; i < m_capacity; ++i) {
            Frame::freeAligned(m_slots[i].planeY);
            Frame::freeAligned(m_slots[i].planeCb);
            Frame::freeAligned(m_slots[i].planeCr);
            m_slots[i].planeY = m_slots[i].planeCb = m_slots[i].planeCr = nullptr;
            m_slots[i].ready.store(false, std::memory_order_release);
        }
        m_slots.reset();
    }

    std::unique_ptr<Frame[]> m_slots;
    int m_capacity = 0;
    std::atomic<int> m_writeIdx{0};
    std::atomic<int> m_readIdx{0};
    int m_width  = 0;
    int m_height = 0;

    // Waiters are notified without locking the ring's fast path.
    mutable std::mutex m_readableMtx;
    mutable std::mutex m_writableMtx;
    mutable std::condition_variable m_readableCv;
    mutable std::condition_variable m_writableCv;
};

}
