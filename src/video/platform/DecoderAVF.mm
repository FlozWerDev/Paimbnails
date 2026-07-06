#import "DecoderAVF.hpp"

#if defined(USE_AV_FOUNDATION)

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include <Geode/loader/Log.hpp>
#include "../../utils/TimedJoin.hpp"
#include <cstring>
#include <chrono>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PAIMON_AVF_HAVE_NEON 1
#endif

namespace paimon {

namespace {bool loadAssetKeysSynchronously(AVAsset* asset, NSArray<NSString*>* keys,
                                NSTimeInterval timeoutSeconds) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block BOOL loaded = NO;
    [asset loadValuesAsynchronouslyForKeys:keys completionHandler:^{
        BOOL allOk = YES;
        for (NSString* key in keys) {
            NSError* err = nil;
            AVKeyValueStatus st = [asset statusOfValueForKey:key error:&err];
            if (st != AVKeyValueStatusLoaded) {
                allOk = NO;
                break;
            }
        }
        loaded = allOk;
        dispatch_semaphore_signal(sem);
    }];
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW,
                                             (int64_t)(timeoutSeconds * NSEC_PER_SEC));
    if (dispatch_semaphore_wait(sem, deadline) != 0) {
        return false;
    }
    return loaded == YES;
}

inline void deinterleaveNV12Row_AVF(const uint8_t* uv, uint8_t* cb, uint8_t* cr, int uvW) {
#if PAIMON_AVF_HAVE_NEON
    int c = 0;
    int vecEnd = uvW & ~15;
    for (; c < vecEnd; c += 16) {
        uint8x16x2_t d = vld2q_u8(uv + c * 2);
        vst1q_u8(cb + c, d.val[0]);
        vst1q_u8(cr + c, d.val[1]);
    }
    for (; c < uvW; ++c) {
        cb[c] = uv[c * 2];
        cr[c] = uv[c * 2 + 1];
    }
#else
    for (int c = 0; c < uvW; ++c) {
        cb[c] = uv[c * 2];
        cr[c] = uv[c * 2 + 1];
    }
#endif
}

} // anon namespace

bool DecoderAVF::open(const std::string& path) {
    closeInternal();

    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        if (!nsPath) {
            geode::log::warn("DecoderAVF: path conversion failed");
            return false;
        }

        NSURL* url = [NSURL fileURLWithPath:nsPath];
        AVAsset* asset = [AVAsset assetWithURL:url];
        if (!asset) {
            geode::log::warn("DecoderAVF: assetWithURL returned nil");
            return false;
        }

        if (!loadAssetKeysSynchronously(asset, @[@"tracks", @"duration", @"readable"], 3.0)) {
            geode::log::warn("DecoderAVF: asset key load timed out/failed");
            return false;
        }

        if (asset.readable == NO) {
            geode::log::warn("DecoderAVF: asset not readable: {}", path);
            return false;
        }

        CMTime dur = asset.duration;
        m_duration = CMTimeGetSeconds(dur);

        AVAssetTrack* videoTrack = nil;
        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        if (tracks.count == 0) {
            geode::log::warn("DecoderAVF: no video track");
            return false;
        }
        videoTrack = tracks[0];

        CGSize naturalSize = videoTrack.naturalSize;
        m_width  = static_cast<int>(naturalSize.width);
        m_height = static_cast<int>(naturalSize.height);
        if (m_width <= 0 || m_height <= 0) {
            geode::log::warn("DecoderAVF: invalid dimensions {}x{}", m_width, m_height);
            return false;
        }

        m_asset      = (__bridge_retained void*) asset;
        m_videoTrack = (__bridge void*) videoTrack;  // weak ref into asset
    }

    if (!m_ring.init(m_width, m_height)) {
        closeInternal();
        return false;
    }

    if (!buildReader(0.0)) {
        closeInternal();
        return false;
    }

    m_finished.store(false, std::memory_order_relaxed);
    m_decoding.store(false, std::memory_order_relaxed);
    return true;
}

bool DecoderAVF::buildReader(double startTimeSeconds) {
    if (!m_asset || !m_videoTrack) return false;

    releaseReaderOnly();

    @autoreleasepool {
        AVAsset* asset = (__bridge AVAsset*)m_asset;
        AVAssetTrack* videoTrack = (__bridge AVAssetTrack*)m_videoTrack;

        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader || error) {
            geode::log::warn("DecoderAVF: reader init failed");
            return false;
        }

        if (startTimeSeconds > 0.0 && m_duration > 0.0) {
            CMTime start = CMTimeMakeWithSeconds(startTimeSeconds, 600);
            CMTime total = asset.duration;
            if (CMTIME_IS_NUMERIC(total) && CMTIME_IS_NUMERIC(start)) {
                reader.timeRange = CMTimeRangeMake(start, CMTimeSubtract(total, start));
            }
        }

        NSDictionary* bp = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };

        AVAssetReaderTrackOutput* trackOutput =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                outputSettings:bp];
        if (!trackOutput) {
            geode::log::warn("DecoderAVF: track output init (NV12) failed, trying planar");
            NSDictionary* planar = @{
                (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8Planar),
                (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
            };
            trackOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                          outputSettings:planar];
            if (!trackOutput) {
                geode::log::warn("DecoderAVF: track output init (planar) failed");
                return false;
            }
            m_pixelFormat = kCVPixelFormatType_420YpCbCr8Planar;
        } else {
            m_pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        }

        trackOutput.alwaysCopiesSampleData = NO;
        [reader addOutput:trackOutput];

        if (![reader startReading]) {
            geode::log::warn("DecoderAVF: startReading failed ({})",
                             reader.error.localizedDescription.UTF8String ?: "unknown");
            return false;
        }

        m_reader      = (__bridge_retained void*) reader;
        m_trackOutput = (__bridge_retained void*) trackOutput;
    }

    return true;
}

void DecoderAVF::releaseReaderOnly() {
    @autoreleasepool {
        if (m_trackOutput) {
            auto* obj = (__bridge_transfer AVAssetReaderTrackOutput*)m_trackOutput;
            obj = nil;
            m_trackOutput = nullptr;
        }
        if (m_reader) {
            auto* obj = (__bridge_transfer AVAssetReader*)m_reader;
            [obj cancelReading];
            obj = nil;
            m_reader = nullptr;
        }
    }
}

void DecoderAVF::startDecoding() {
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
    if (m_decoding.load(std::memory_order_relaxed)) return;
    if (!m_reader) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    m_thread = std::thread(&DecoderAVF::decodeLoop, this);
}

void DecoderAVF::stopDecoding() {
    m_decoding.store(false, std::memory_order_relaxed);
    m_ring.wakeAll();  // unblock any cv waits ASAP
    if (m_thread.joinable() && !paimon::timedJoin(m_thread, std::chrono::seconds(3), &m_decoding)) {
        m_decodeThreadDetached.store(true, std::memory_order_release);
    }
}

void DecoderAVF::decodeLoop() {
    auto* trackOutput = (__bridge AVAssetReaderTrackOutput*)m_trackOutput;
    auto* reader      = (__bridge AVAssetReader*)m_reader;
    if (!trackOutput || !reader) return;

    while (m_decoding.load(std::memory_order_relaxed)) {
        if (m_ring.isFull()) {
            m_ring.waitForWritable(50, &m_decoding);
            continue;
        }

        @autoreleasepool {
            CMSampleBufferRef sampleBuffer = [trackOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                if (reader.status == AVAssetReaderStatusCompleted ||
                    reader.status == AVAssetReaderStatusFailed) {
                    m_finished.store(true, std::memory_order_release);
                }
                break;
            }

            auto* slot = m_ring.nextWrite();
            if (!slot) {
                CFRelease(sampleBuffer);
                m_ring.waitForWritable(50, &m_decoding);
                continue;
            }

            CMTime ptsTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            slot->pts = CMTimeGetSeconds(ptsTime);

            CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (!pixelBuffer) {
                CFRelease(sampleBuffer);
                continue;
            }

            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

            if (m_pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                m_pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
                // ── NV12 bi-planar path ──
                int yWidth   = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, 0));
                int yHeight  = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, 0));
                int uvWidth  = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, 1));
                int uvHeight = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, 1));
                int yStride  = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0));
                int uvStride = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1));
                uint8_t* yBase  = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0));
                uint8_t* uvBase = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1));

                if (yBase) {
                    int copy = std::min(slot->strideY, yWidth);
                    for (int r = 0; r < yHeight; ++r) {
                        std::memcpy(slot->planeY + r * slot->strideY,
                                    yBase + r * yStride, copy);
                    }
                }
                if (uvBase) {
                    for (int r = 0; r < uvHeight; ++r) {
                        deinterleaveNV12Row_AVF(uvBase + r * uvStride,
                                                slot->planeCb + r * slot->strideCb,
                                                slot->planeCr + r * slot->strideCr,
                                                uvWidth);
                    }
                }
            } else {
                // ── Planar I420 path (legacy fallback) ──
                int yWidth   = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, 0));
                int yHeight  = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, 0));
                int cbWidth  = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, 1));
                int cbHeight = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, 1));
                int crWidth  = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, 2));
                int crHeight = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, 2));

                uint8_t* yBase  = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0));
                uint8_t* cbBase = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1));
                uint8_t* crBase = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 2));

                int yStride  = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0));
                int cbStride = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1));
                int crStride = static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 2));

                if (yBase) {
                    for (int r = 0; r < yHeight; ++r) {
                        std::memcpy(slot->planeY + r * slot->strideY,
                                    yBase + r * yStride,
                                    std::min(slot->strideY, yWidth));
                    }
                }
                if (cbBase) {
                    for (int r = 0; r < cbHeight; ++r) {
                        std::memcpy(slot->planeCb + r * slot->strideCb,
                                    cbBase + r * cbStride,
                                    std::min(slot->strideCb, cbWidth));
                    }
                }
                if (crBase) {
                    for (int r = 0; r < crHeight; ++r) {
                        std::memcpy(slot->planeCr + r * slot->strideCr,
                                    crBase + r * crStride,
                                    std::min(slot->strideCr, crWidth));
                    }
                }
            }

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CFRelease(sampleBuffer);

            m_ring.commitWrite();
        }
    }
}

bool DecoderAVF::consumeFrame(Frame& outFrame) {
    auto* slot = m_ring.nextRead();
    if (!slot) return false;

    int ySize  = slot->strideY * slot->height;
    int uvH    = (slot->height + 1) / 2;
    int uvSize = slot->strideCb * uvH;

    std::memcpy(outFrame.planeY,  slot->planeY,  ySize);
    std::memcpy(outFrame.planeCb, slot->planeCb, uvSize);
    std::memcpy(outFrame.planeCr, slot->planeCr, uvSize);
    outFrame.strideY  = slot->strideY;
    outFrame.strideCb = slot->strideCb;
    outFrame.strideCr = slot->strideCr;
    outFrame.width    = slot->width;
    outFrame.height   = slot->height;
    outFrame.pts      = slot->pts;

    m_ring.commitRead();
    return true;
}

void DecoderAVF::seekTo(double seconds) {
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;

    while (m_ring.nextRead()) m_ring.commitRead();

    if (!buildReader(std::max(0.0, seconds))) {
        geode::log::warn("DecoderAVF: seekTo({}) - buildReader failed", seconds);
        m_finished.store(true, std::memory_order_release);
        return;
    }
    m_finished.store(false, std::memory_order_relaxed);
    if (wasDecoding) startDecoding();
}

bool DecoderAVF::skipFrame() {
    return m_ring.skipRead();
}

double DecoderAVF::getDuration() const { return m_duration; }
int DecoderAVF::getWidth()  const { return m_width; }
int DecoderAVF::getHeight() const { return m_height; }
bool DecoderAVF::isFinished() const {
    return m_finished.load(std::memory_order_acquire);
}

double DecoderAVF::peekNextPTS() const {
    return m_ring.peekNextPTS();
}

double DecoderAVF::peekSecondPTS() const {
    return m_ring.peekSecondPTS();
}

const paimon::VideoFrame* DecoderAVF::peekFrame() {
    return m_ring.peekRead();
}

void DecoderAVF::releaseFrame() {
    if (m_ring.peekRead()) m_ring.commitRead();
}

void DecoderAVF::closeInternal() {
    stopDecoding();

    if (m_decodeThreadDetached.load(std::memory_order_acquire)) {
        m_trackOutput = nullptr;
        m_reader = nullptr;
        m_asset = nullptr;
        m_videoTrack = nullptr;
        return;
    }

    releaseReaderOnly();

    @autoreleasepool {
        if (m_asset) {
            auto* obj = (__bridge_transfer AVAsset*)m_asset;
            obj = nil;
            m_asset = nullptr;
        }
        m_videoTrack = nullptr;  // weak ref, no release
    }
    m_pixelFormat = 0;
}

} // namespace paimon

#endif // USE_AV_FOUNDATION
