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

namespace paimon {

// ─────────────────────────────────────────────────────────────
// Open
// ─────────────────────────────────────────────────────────────
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
        if (!asset || asset.readable == NO) {
            geode::log::warn("DecoderAVF: asset not readable: {}", path);
            return false;
        }

        // Load duration
        CMTime dur = asset.duration;
        m_duration = CMTimeGetSeconds(dur);

        // Find video track
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

        // Create AVAssetReader
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader || error) {
            geode::log::warn("DecoderAVF: reader init failed");
            return false;
        }

        // Output settings: request YUV 4:2:0 planar (no conversion)
        NSDictionary* outputSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8Planar),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{} // enable IOSurface for zero-copy
        };

        AVAssetReaderTrackOutput* trackOutput =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                outputSettings:outputSettings];
        if (!trackOutput) {
            geode::log::warn("DecoderAVF: track output init failed");
            return false;
        }

        trackOutput.alwaysCopiesSampleData = NO; // zero-copy when possible
        [reader addOutput:trackOutput];

        if (![reader startReading]) {
            geode::log::warn("DecoderAVF: startReading failed");
            return false;
        }

        // Retain Obj-C objects and store as void*
        m_asset       = (__bridge_retained void*) asset;
        m_reader      = (__bridge_retained void*) reader;
        m_trackOutput = (__bridge_retained void*) trackOutput;
    }

    if (!m_ring.init(m_width, m_height)) {
        closeInternal();
        return false;
    }

    m_finished.store(false, std::memory_order_relaxed);
    m_decoding.store(false, std::memory_order_relaxed);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Decode loop
// ─────────────────────────────────────────────────────────────
void DecoderAVF::startDecoding() {
    if (m_decoding.load(std::memory_order_relaxed)) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    m_thread = std::thread(&DecoderAVF::decodeLoop, this);
}

void DecoderAVF::stopDecoding() {
    m_decoding.store(false, std::memory_order_relaxed);
    if (m_thread.joinable()) paimon::timedJoin(m_thread, std::chrono::seconds(3));
}

void DecoderAVF::decodeLoop() {
    auto* trackOutput = (__bridge AVAssetReaderTrackOutput*)m_trackOutput;
    auto* reader      = (__bridge AVAssetReader*)m_reader;

    while (m_decoding.load(std::memory_order_relaxed)) {
        if (m_ring.isFull()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        @autoreleasepool {
            CMSampleBufferRef sampleBuffer = [trackOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                // Check if reader finished
                if (reader.status == AVAssetReaderStatusCompleted ||
                    reader.status == AVAssetReaderStatusFailed) {
                    m_finished.store(true, std::memory_order_release);
                }
                break;
            }

            auto* slot = m_ring.nextWrite();
            if (!slot) {
                CFRelease(sampleBuffer);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // Get PTS
            CMTime ptsTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            slot->pts = CMTimeGetSeconds(ptsTime);

            // Get pixel buffer
            CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (!pixelBuffer) {
                CFRelease(sampleBuffer);
                continue;
            }

            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

            // 420YpCbCr8Planar: 3 planes (Y, Cb, Cr)
            int yWidth  = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, 0));
            int yHeight = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, 0));
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

            // Copy Y
            if (yBase) {
                for (int r = 0; r < yHeight; ++r) {
                    std::memcpy(slot->planeY + r * slot->strideY,
                                yBase + r * yStride,
                                std::min(slot->strideY, yWidth));
                }
            }
            // Copy Cb
            if (cbBase) {
                for (int r = 0; r < cbHeight; ++r) {
                    std::memcpy(slot->planeCb + r * slot->strideCb,
                                cbBase + r * cbStride,
                                std::min(slot->strideCb, cbWidth));
                }
            }
            // Copy Cr
            if (crBase) {
                for (int r = 0; r < crHeight; ++r) {
                    std::memcpy(slot->planeCr + r * slot->strideCr,
                                crBase + r * crStride,
                                std::min(slot->strideCr, crWidth));
                }
            }

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CFRelease(sampleBuffer); // release immediately after copy

            m_ring.commitWrite();
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Consume / Seek / Accessors
// ─────────────────────────────────────────────────────────────
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
    // AVAssetReader does not support seeking after startReading.
    // Full seek requires recreating the reader.
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();

    while (m_ring.nextRead()) m_ring.commitRead();

    // Store path to reopen — we need to recreate the reader
    // For now, drain and restart from beginning as a simple approach
    // A full implementation would store the path and re-open with a time range
    closeInternal();

    // Note: caller must re-open and re-start the decoder for seeking.
    // This is a known limitation of AVAssetReader.
    m_finished.store(false, std::memory_order_relaxed);
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

// ─────────────────────────────────────────────────────────────
// Close
// ─────────────────────────────────────────────────────────────
void DecoderAVF::closeInternal() {
    stopDecoding();

    @autoreleasepool {
        if (m_trackOutput) {
            auto* obj = (__bridge_transfer AVAssetReaderTrackOutput*)m_trackOutput;
            obj = nil; // ARC releases
            m_trackOutput = nullptr;
        }
        if (m_reader) {
            auto* obj = (__bridge_transfer AVAssetReader*)m_reader;
            [obj cancelReading];
            obj = nil;
            m_reader = nullptr;
        }
        if (m_asset) {
            auto* obj = (__bridge_transfer AVAsset*)m_asset;
            obj = nil;
            m_asset = nullptr;
        }
    }
}

} // namespace paimon

#endif // USE_AV_FOUNDATION
