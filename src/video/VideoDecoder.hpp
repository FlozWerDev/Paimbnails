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
#include <malloc.h>   // _aligned_malloc / _aligned_free
#elif defined(__ANDROID__)
#include <malloc.h>   // memalign (bionic)
#else
#include <cstdlib>    // std::aligned_alloc (C++17)
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

    // Aligned allocation helpers — 32-byte alignment enables SIMD (SSE2/AVX2)
    static size_t alignedSize(int w, int h) {
        // Round up to next multiple of 32 bytes per row for alignment
        int alignedStride = ((w + 31) / 32) * 32;
        return static_cast<size_t>(alignedStride) * h;
    }

    static int alignedStride(int w) {
        return ((w + 31) / 32) * 32;
    }

    static uint8_t* allocAligned(size_t size) {
        // Round up size to multiple of alignment (required by aligned_alloc)
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
    virtual bool consumeFrame(Frame& outFrame) = 0;
    virtual void seekTo(double seconds) = 0;
    virtual double getDuration() const = 0;
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    virtual bool isFinished() const = 0;

    // Skip the next available frame without copying any data.
    // Frees the ring buffer slot for the decode thread.
    // Returns true if a frame was skipped, false if the buffer was empty.
    virtual bool skipFrame() = 0;

    // Peek at the PTS of the next available frame without consuming it.
    // Returns DBL_MAX if the ring buffer is empty.
    virtual double peekNextPTS() const = 0;

    // Peek at the PTS of the frame AFTER the current head (slot + 1).
    // Returns DBL_MAX if fewer than 2 frames are ready.  Default fallback
    // for decoders that don't override.
    virtual double peekSecondPTS() const { return 1e300; /* ~DBL_MAX */ }

    // Zero-copy access to the next readable frame.  The returned pointer is
    // only valid until releaseFrame() is called.  Between peekFrame() and
    // releaseFrame(), the caller must NOT call consumeFrame/skipFrame/seekTo/
    // stopDecoding on this decoder.
    //
    // Returns nullptr if the ring buffer is empty or if the decoder does not
    // support zero-copy access (fallback: caller should use consumeFrame()).
    virtual const Frame* peekFrame() { return nullptr; }

    // Release a frame previously obtained from peekFrame().  No-op if
    // peekFrame() returned nullptr.  Must be called exactly once per
    // successful peekFrame() before any other decoder method (except PTS
    // peeks and state queries).
    virtual void releaseFrame() {}

    // Returns true if the decoder had to detach its worker thread during
    // shutdown/stop and is no longer safe to reuse or destroy normally.
    virtual bool isTerminal() const { return false; }

    static std::unique_ptr<IVideoDecoder> create(const std::string& path);
};

// ─────────────────────────────────────────────────────────────
// Ring buffer lock-free (single-producer / single-consumer)
//
// Adaptive slot count based on video resolution:
//   - 4K+ (frame > 8 MB/slot):  3 slots  (~37 MB total)
//   - 1080p-1440p (> 2 MB):     5 slots  (~15 MB total)
//   - 720p and below:           8 slots  (~11 MB total)
// ─────────────────────────────────────────────────────────────
class VideoRingBuffer {
public:
    using Frame = VideoFrame;

    VideoRingBuffer() = default;

    ~VideoRingBuffer() { freeSlots(); }

    // Compute adaptive slot count based on per-slot memory footprint
    static int computeSlotCount(int w, int h) {
        size_t slotBytes = Frame::alignedSize(w, h)
                         + 2 * Frame::alignedSize((w + 1) / 2, (h + 1) / 2);
        if (slotBytes > 8 * 1024 * 1024)  return 3;   // 4K+: ~37 MB
        if (slotBytes > 2 * 1024 * 1024)  return 5;   // 1080p-1440p: ~15 MB
        return 8;                                       // 720p and below: ~11 MB
    }

    // Allocate plane buffers for a given frame size.
    // Must be called once before decoding starts.
    bool init(int w, int h) {
        m_width = w;
        m_height = h;
        m_capacity = computeSlotCount(w, h);
        int uvH = (h + 1) / 2;
        int uvW = (w + 1) / 2;

        // Use aligned strides for SIMD-friendly memcpy
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

    // Producer: get next writable slot (returns nullptr if full).
    Frame* nextWrite() {
        int next = (m_writeIdx.load(std::memory_order_relaxed) + 1) % m_capacity;
        if (next == m_readIdx.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[m_writeIdx.load(std::memory_order_relaxed)];
    }

    // Producer: commit the current write slot.
    void commitWrite() {
        auto idx = m_writeIdx.load(std::memory_order_relaxed);
        m_slots[idx].ready.store(true, std::memory_order_release);
        m_writeIdx.store((idx + 1) % m_capacity, std::memory_order_release);
        // Wake any consumer waiting for a readable frame.
        m_readableCv.notify_one();
    }

    // Consumer: get next readable slot (returns nullptr if empty).
    Frame* nextRead() {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return nullptr;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[r];
    }

    // Consumer: peek at the next readable slot without committing.
    // Same semantics as nextRead — returns the current read slot.  The
    // caller must still call commitRead() to advance the read index.
    // (Kept as a separate method for readability in zero-copy paths.)
    const Frame* peekRead() const {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return nullptr;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return nullptr;
        return &m_slots[r];
    }

    // Consumer: release the current read slot.
    void commitRead() {
        auto idx = m_readIdx.load(std::memory_order_relaxed);
        m_slots[idx].ready.store(false, std::memory_order_release);
        m_readIdx.store((idx + 1) % m_capacity, std::memory_order_release);
        // Wake the producer if it was waiting because the ring was full.
        m_writableCv.notify_one();
    }

    // Consumer: skip the current read slot without copying data.
    // Returns true if a slot was skipped, false if the buffer was empty.
    bool skipRead() {
        int r = m_readIdx.load(std::memory_order_relaxed);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return false;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return false;
        m_slots[r].ready.store(false, std::memory_order_release);
        m_readIdx.store((r + 1) % m_capacity, std::memory_order_release);
        m_writableCv.notify_one();
        return true;
    }

    // Peek at the PTS of the next readable slot without consuming it.
    // Returns DBL_MAX if the ring buffer is empty or the slot isn't ready.
    double peekNextPTS() const {
        int r = m_readIdx.load(std::memory_order_acquire);
        if (r == m_writeIdx.load(std::memory_order_acquire)) return DBL_MAX;
        if (!m_slots[r].ready.load(std::memory_order_acquire)) return DBL_MAX;
        return m_slots[r].pts;
    }

    // Peek at the PTS of the slot AFTER the head (i.e. the second-in-line).
    // Returns DBL_MAX if the buffer has fewer than 2 ready frames.  Used by
    // VideoPlayer to decide whether to skip the current head (there's a
    // newer frame already past-due right after).
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

    // Producer-side wait: blocks (up to timeoutMs) until the ring has room
    // for another write, or until the abort flag becomes true.  Returns true
    // if a writable slot is available; false if timed out or aborted.
    // The caller MUST re-check isFull()/nextWrite() after this returns.
    template <typename Clock = std::chrono::steady_clock>
    bool waitForWritable(int timeoutMs, const std::atomic<bool>* abortFlag = nullptr) {
        if (!isFull()) return true;
        std::unique_lock<std::mutex> lk(m_writableMtx);
        m_writableCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            if (abortFlag && !abortFlag->load(std::memory_order_relaxed)) return true;
            return !isFull();
        });
        return !isFull();
    }

    // Consumer-side wait: blocks (up to timeoutMs) until a frame is readable,
    // or until the abort flag becomes true.  Returns true if a readable slot
    // is available; false if timed out or aborted.
    template <typename Clock = std::chrono::steady_clock>
    bool waitForReadable(int timeoutMs, const std::atomic<bool>* abortFlag = nullptr) {
        if (!isEmpty()) return true;
        std::unique_lock<std::mutex> lk(m_readableMtx);
        m_readableCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            if (abortFlag && !abortFlag->load(std::memory_order_relaxed)) return true;
            return !isEmpty();
        });
        return !isEmpty();
    }

    // Wake every waiter — used during shutdown so threads can observe
    // an aborted state and exit promptly.
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

    // Wakeup primitives — notify_one() in commit*() avoids decoder busy-poll.
    // Mutexes are only ever taken from the wait_for predicate; commit*()
    // never locks them, preserving the lock-free fast path.
    mutable std::mutex m_readableMtx;
    mutable std::mutex m_writableMtx;
    mutable std::condition_variable m_readableCv;
    mutable std::condition_variable m_writableCv;
};

} // namespace paimon
