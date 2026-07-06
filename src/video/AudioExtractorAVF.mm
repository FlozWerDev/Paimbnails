#import "AudioExtractor.hpp"

#if defined(USE_AV_FOUNDATION)

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <AudioToolbox/AudioToolbox.h>

#include "AudioWavCommon.hpp"
#include <Geode/loader/Log.hpp>
#include <filesystem>
#include <vector>

namespace paimon::video {

namespace {
bool loadKeys(AVAsset* asset, NSArray<NSString*>* keys, NSTimeInterval timeout) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block BOOL ok = NO;
    [asset loadValuesAsynchronouslyForKeys:keys completionHandler:^{
        BOOL all = YES;
        for (NSString* key in keys) {
            NSError* err = nil;
            if ([asset statusOfValueForKey:key error:&err] != AVKeyValueStatusLoaded) {
                all = NO;
                break;
            }
        }
        ok = all;
        dispatch_semaphore_signal(sem);
    }];
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC));
    if (dispatch_semaphore_wait(sem, deadline) != 0) return false;
    return ok == YES;
}
} 

std::string extractAudioToWav(const std::string& videoPath) {
    std::lock_guard<std::mutex> lock(detail::audioExtractorMutex());

    if (videoPath.empty()) return {};

    auto wavPath = detail::makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(wavPath, ec) && !ec) {
        geode::log::info("[AudioExtract] cached WAV exists: {}", wavPath);
        return wavPath;
    }

    std::vector<uint8_t> pcm;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;

    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:videoPath.c_str()];
        if (!nsPath) return {};

        AVAsset* asset = [AVAsset assetWithURL:[NSURL fileURLWithPath:nsPath]];
        if (!asset || !loadKeys(asset, @[@"tracks", @"readable"], 3.0) || asset.readable == NO) {
            geode::log::warn("[AudioExtract] asset not readable: {}", videoPath);
            return {};
        }

        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeAudio];
        if (tracks.count == 0) {
            geode::log::warn("[AudioExtract] no audio track in {}", videoPath);
            return {};
        }
        AVAssetTrack* audioTrack = tracks[0];

        // Pull source channel count / sample rate from the track's format.
        CMAudioFormatDescriptionRef fmtDesc =
            (__bridge CMAudioFormatDescriptionRef)audioTrack.formatDescriptions.firstObject;
        const AudioStreamBasicDescription* asbd =
            fmtDesc ? CMAudioFormatDescriptionGetStreamBasicDescription(fmtDesc) : nullptr;
        sampleRate = asbd ? static_cast<uint32_t>(asbd->mSampleRate) : 44100u;
        channels   = asbd ? static_cast<uint16_t>(asbd->mChannelsPerFrame) : 2u;
        if (sampleRate == 0) sampleRate = 44100;
        if (channels == 0)   channels = 2;

        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader || error) {
            geode::log::warn("[AudioExtract] reader init failed");
            return {};
        }

        NSDictionary* settings = @{
            (id)AVFormatIDKey:               @(kAudioFormatLinearPCM),
            (id)AVSampleRateKey:             @(sampleRate),
            (id)AVNumberOfChannelsKey:       @(channels),
            (id)AVLinearPCMBitDepthKey:      @16,
            (id)AVLinearPCMIsFloatKey:       @NO,
            (id)AVLinearPCMIsBigEndianKey:   @NO,
            (id)AVLinearPCMIsNonInterleaved: @NO,
        };

        AVAssetReaderTrackOutput* output =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:audioTrack outputSettings:settings];
        if (!output) {
            geode::log::warn("[AudioExtract] track output init failed");
            return {};
        }
        output.alwaysCopiesSampleData = NO;
        [reader addOutput:output];

        if (![reader startReading]) {
            geode::log::warn("[AudioExtract] startReading failed: {}",
                             reader.error.localizedDescription.UTF8String ?: "unknown");
            return {};
        }

        pcm.reserve(1u << 20);
        while (true) {
            CMSampleBufferRef sb = [output copyNextSampleBuffer];
            if (!sb) break;

            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sb);
            if (block) {
                size_t len = CMBlockBufferGetDataLength(block);
                if (len > 0) {
                    size_t oldSize = pcm.size();
                    pcm.resize(oldSize + len);
                    if (CMBlockBufferCopyDataBytes(block, 0, len, pcm.data() + oldSize) != kCMBlockBufferNoErr) {
                        pcm.resize(oldSize);
                    }
                }
            }
            CFRelease(sb);
        }

        if (reader.status == AVAssetReaderStatusFailed) {
            geode::log::warn("[AudioExtract] reader failed: {}",
                             reader.error.localizedDescription.UTF8String ?: "unknown");
        }
        [reader cancelReading];
    }

    if (pcm.empty()) {
        geode::log::warn("[AudioExtract] no audio data extracted from {}", videoPath);
        return {};
    }

    if (!detail::writeWavFile(wavPath, pcm.data(), pcm.size(), channels, sampleRate, 16)) {
        return {};
    }

    geode::log::info("[AudioExtract] extracted {} bytes of PCM audio to {}",
                     pcm.size(), wavPath);
    return wavPath;
}

} // namespace paimon::video

#endif // USE_AV_FOUNDATION
