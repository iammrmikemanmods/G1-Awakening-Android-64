#import "AppleAudio.hpp"
#import <AVFAudio/AVFAudio.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace {
constexpr NSUInteger max_voices = 64; // Explicit native resource bound; no silent voice stealing.

NSString* audio_string(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size()
                                encoding:NSUTF8StringEncoding] ?: @"Native audio error";
}

std::runtime_error audio_error(NSString* operation, NSError* error) {
    NSString* detail = [NSString stringWithFormat:@"%@: %@",operation,
        error.localizedDescription ?: @"the audio framework returned failure"];
    return std::runtime_error(detail.UTF8String ?: "Native audio operation failed");
}
} // namespace

@interface G1AudioVoice : NSObject
@property(nonatomic,strong) AVAudioPlayerNode* node;
@property(nonatomic,strong) AVAudioPCMBuffer* buffer;
@property(nonatomic) NSUInteger slot;
@property(nonatomic) std::uint64_t ticket;
@property(nonatomic) BOOL active;
@property(nonatomic) BOOL paused;
@end
@implementation G1AudioVoice
@end

@interface G1AppleAudio ()
- (void)createEngine;
- (void)startEngineAndResume;
- (void)pauseOutput;
- (void)stopAll;
- (void)loadSlot:(std::uint8_t)slot;
- (void)playSlot:(std::uint8_t)slot;
- (void)completedVoice:(NSUInteger)index ticket:(std::uint64_t)ticket;
- (void)recordFailure:(NSString*)message;
- (void)interruption:(NSNotification*)notification;
- (void)engineChanged:(NSNotification*)notification;
- (void)mediaReset:(NSNotification*)notification;
@end

@implementation G1AppleAudio {
    std::unique_ptr<g1::Archive> _archive;
    g1::AudioControlState _control;
    g1::AudioConfiguration _configuration;
    AVAudioEngine* _engine;
    AVAudioFormat* _format;
    NSMutableDictionary<NSNumber*,AVAudioPCMBuffer*>* _buffers;
    NSMutableArray<G1AudioVoice*>* _voices;
    NSMutableArray* _observers;
    NSMutableArray<NSString*>* _errors;
    bool _foreground, _interrupted, _failed;
    std::uint64_t _requests, _loads, _unloads, _scheduled, _playedBackCallbacks;
    std::uint64_t _explicitlyStopped, _interruptionCount, _configurationChanges, _mediaResets;
    std::uint64_t _configurationDroppedVoices, _generation;
}

- (instancetype)initWithArchive:(const g1::Archive&)archive {
    self = [super init];
    if (!self) return nil;
    _buffers = [NSMutableDictionary new];
    _voices = [NSMutableArray new];
    _observers = [NSMutableArray new];
    _errors = [NSMutableArray new];
    _configuration.enabled_a4 = true; // Explicit native preference, not a recovered cold default.
    _foreground = UIApplication.sharedApplication.applicationState != UIApplicationStateBackground;
    @try {
        try {
            if (!NSThread.isMainThread) throw std::runtime_error("Audio host must be created on the main thread");
            _archive = std::make_unique<g1::Archive>(archive);
            [self createEngine];
            if (_foreground) [self startEngineAndResume];
            __weak G1AppleAudio* weakSelf = self;
            NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
            [_observers addObject:[center addObserverForName:AVAudioSessionInterruptionNotification
                object:AVAudioSession.sharedInstance queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification* note) { [weakSelf interruption:note]; }]];
            [_observers addObject:[center addObserverForName:AVAudioEngineConfigurationChangeNotification
                object:nil queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification* note) { [weakSelf engineChanged:note]; }]];
            [_observers addObject:[center addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                object:AVAudioSession.sharedInstance queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification* note) { [weakSelf mediaReset:note]; }]];
        } catch (const std::exception& error) { [self recordFailure:audio_string(error.what())]; }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio initialization exception"]; }
    return self;
}

- (void)dealloc {
    for (id token in _observers) [NSNotificationCenter.defaultCenter removeObserver:token];
    for (G1AudioVoice* voice in _voices) { voice.active = NO; ++voice.ticket; [voice.node stop]; }
    [_engine stop];
}

- (void)createEngine {
    _engine = [AVAudioEngine new];
    _format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:22050 channels:1];
    if (!_engine || !_format) throw std::runtime_error("Native audio engine/format allocation failed");
    // Access the mixer so the engine has its ordinary output graph. Individual
    // original 22050-Hz mono voices connect at their decoded input format.
    (void)_engine.mainMixerNode;
}

- (void)startEngineAndResume {
    if (_failed || !_foreground || _interrupted) return;
    NSError* error = nil;
    AVAudioSession* session = AVAudioSession.sharedInstance;
    if (![session setCategory:AVAudioSessionCategoryAmbient mode:AVAudioSessionModeDefault options:0 error:&error])
        throw audio_error(@"Set ambient audio category",error);
    if (![session setActive:YES error:&error]) throw audio_error(@"Activate audio session",error);
    if (!_engine.isRunning) {
        [_engine prepare];
        if (![_engine startAndReturnError:&error]) throw audio_error(@"Start audio engine",error);
    }
    for (G1AudioVoice* voice in _voices) if (voice.active && voice.paused) {
        [voice.node play];
        voice.paused = NO;
    }
}

- (void)pauseOutput {
    for (G1AudioVoice* voice in _voices) if (voice.active) {
        [voice.node pause];
        voice.paused = YES;
    }
    [_engine pause];
}

- (void)stopAll {
    for (G1AudioVoice* voice in _voices) {
        if (voice.active) ++_explicitlyStopped;
        voice.active = NO;
        voice.paused = NO;
        ++voice.ticket; // Completion from an unscheduled buffer is not playback evidence.
        [voice.node stop];
        voice.buffer = nil;
    }
}

- (void)recordFailure:(NSString*)message {
    _failed = true;
    [_errors addObject:message ?: @"Unspecified native audio failure"];
    if (_errors.count > 32) [_errors removeObjectAtIndex:0];
    @try { [self stopAll]; [_engine pause]; } @catch (NSException* ignored) { (void)ignored; }
    // Reconcile logical load flags with buffers actually retained after any
    // partially completed dispatch; failed loads are never reported loaded.
    for (std::size_t i=0;i<_control.loaded.size();++i) _control.loaded[i] = _buffers[@(i)] != nil;
}

- (void)loadSlot:(std::uint8_t)slot {
    if (_buffers[@(slot)]) return;
    const auto pcm = g1::decode_audio_slot(*_archive,slot);
    if (pcm.sample_rate != 22050 || pcm.channels != 1 || pcm.frames() == 0 ||
        pcm.frames() > std::numeric_limits<AVAudioFrameCount>::max())
        throw g1::FormatError("Audio output supports the verified original 22050-Hz mono corpus");
    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_format
        frameCapacity:static_cast<AVAudioFrameCount>(pcm.frames())];
    if (!buffer || !buffer.floatChannelData || !buffer.floatChannelData[0])
        throw std::runtime_error("Native PCM buffer allocation failed");
    std::copy(pcm.samples.begin(),pcm.samples.end(),buffer.floatChannelData[0]);
    buffer.frameLength = static_cast<AVAudioFrameCount>(pcm.frames());
    _buffers[@(slot)] = buffer;
    ++_loads;
}

- (void)playSlot:(std::uint8_t)slot {
    AVAudioPCMBuffer* buffer = _buffers[@(slot)];
    if (!buffer) throw std::runtime_error("AudioEvents requested a slot without a decoded PCM buffer");
    [self startEngineAndResume];
    if (!_engine.isRunning || !_foreground || _interrupted)
        throw std::runtime_error("AudioEvents requested playback while native output was unavailable");
    NSUInteger index = 0;
    while (index < _voices.count && _voices[index].active) ++index;
    if (index == _voices.count) {
        if (index >= max_voices) throw std::runtime_error("Native audio voice bound reached; no voice was silently discarded");
        G1AudioVoice* voice = [G1AudioVoice new];
        voice.node = [AVAudioPlayerNode new];
        if (!voice.node) throw std::runtime_error("Native audio player allocation failed");
        [_engine attachNode:voice.node];
        [_engine connect:voice.node to:_engine.mainMixerNode format:_format];
        [_voices addObject:voice];
    }
    G1AudioVoice* voice = _voices[index];
    [voice.node stop];
    voice.slot = slot;
    voice.buffer = buffer; // Retain until completion, even if the cache unloads it.
    voice.active = YES;
    voice.paused = NO;
    const std::uint64_t ticket = ++voice.ticket;
    const std::uint64_t generation = _generation;
    __weak G1AppleAudio* weakSelf = self;
    // This iOS 11+ selector schedules exactly once. It does not request the
    // loop option. Apple permits callbacks on stop as well, hence tickets.
    [voice.node scheduleBuffer:buffer completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
        completionHandler:^(AVAudioPlayerNodeCompletionCallbackType type) {
            if (type != AVAudioPlayerNodeCompletionDataPlayedBack) return;
            dispatch_async(dispatch_get_main_queue(),^{
                G1AppleAudio* strongSelf = weakSelf;
                if (strongSelf && strongSelf->_generation == generation)
                    [strongSelf completedVoice:index ticket:ticket];
            });
        }];
    [voice.node play];
    ++_scheduled;
}

- (void)completedVoice:(NSUInteger)index ticket:(std::uint64_t)ticket {
    if (index >= _voices.count) return;
    G1AudioVoice* voice = _voices[index];
    if (!voice.active || voice.ticket != ticket) return;
    voice.active = NO;
    voice.paused = NO;
    voice.buffer = nil;
    ++_playedBackCallbacks;
    // This executes on the main queue, never inside the audio completion
    // callback itself; Apple warns against stopping there because of deadlock.
    [voice.node stop];
}

- (bool)request:(g1::MissionAudioRequest)request {
    ++_requests;
    if (_failed) return false;
    @try {
        try {
            if (!NSThread.isMainThread) throw std::runtime_error("Audio requests require the main thread");
            bool playing = false;
            for (G1AudioVoice* voice in _voices)
                playing = playing || (voice.active && voice.node.isPlaying && _engine.isRunning);
            auto next = _control;
            const auto dispatch = g1::dispatch_audio_request(next,_configuration,
                {_foreground && !_interrupted && !_failed,playing},request);
            for (const auto action : dispatch.actions) switch (action.kind) {
            case g1::AudioActionKind::load: [self loadSlot:action.slot]; break;
            case g1::AudioActionKind::unload:
                [_buffers removeObjectForKey:@(action.slot)]; ++_unloads; break;
            case g1::AudioActionKind::stop_all: [self stopAll]; break;
            case g1::AudioActionKind::play_once:
                // Independent native Music/Sound preferences. The original
                // dispatch still determines resource/order/one-shot semantics.
                if ((action.slot<=3 && _configuration.enabled_a4) ||
                    (action.slot>3 && _configuration.enabled_a3)) [self playSlot:action.slot];
                break;
            }
            _control = next;
            return dispatch.return_value;
        } catch (const std::exception& error) { [self recordFailure:audio_string(error.what())]; }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio request exception"]; }
    return false;
}

- (BOOL)musicEnabled { return _configuration.enabled_a4; }
- (BOOL)soundEnabled { return _configuration.enabled_a3; }
- (void)setMusicEnabled:(BOOL)music soundEnabled:(BOOL)sound {
    if (!NSThread.isMainThread) { [self recordFailure:@"Audio preferences require the main thread"]; return; }
    _configuration.enabled_a4=music;
    _configuration.enabled_a3=sound;
    _configuration.suppress_music_a0=!music;
    @try {
        for (G1AudioVoice* voice in _voices) if (voice.active &&
            ((voice.slot<=3 && !music) || (voice.slot>3 && !sound))) {
            voice.active=NO;voice.paused=NO;++voice.ticket;++_explicitlyStopped;
            [voice.node stop];voice.buffer=nil;
        }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio preference change failed"]; }
}

- (void)setForegroundActive:(BOOL)active {
    _foreground = active;
    if (_failed) return;
    @try {
        try {
            if (active) [self startEngineAndResume];
            else {
                [self pauseOutput];
                NSError* error = nil;
                if (![AVAudioSession.sharedInstance setActive:NO
                    withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:&error])
                    throw audio_error(@"Deactivate background audio session",error);
            }
        } catch (const std::exception& error) { [self recordFailure:audio_string(error.what())]; }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio lifecycle exception"]; }
}

- (void)interruption:(NSNotification*)notification {
    NSNumber* value = notification.userInfo[AVAudioSessionInterruptionTypeKey];
    if (!value || _failed) return;
    @try {
        try {
            if (value.unsignedIntegerValue == AVAudioSessionInterruptionTypeBegan) {
                ++_interruptionCount;
                _interrupted = true;
                [self pauseOutput];
            } else if (value.unsignedIntegerValue == AVAudioSessionInterruptionTypeEnded) {
                _interrupted = false;
                const auto options = [notification.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
                if (options & AVAudioSessionInterruptionOptionShouldResume) [self startEngineAndResume];
                // Stop retained buffers when resumption was not suggested.
                // Fresh active-game requests can still start audio afterward;
                // Apple's shouldResume contract explicitly permits this for games.
                else [self stopAll];
            }
        } catch (const std::exception& error) { [self recordFailure:audio_string(error.what())]; }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio interruption exception"]; }
}

- (void)engineChanged:(NSNotification*)notification {
    if (notification.object != _engine || _failed) return;
    @try {
        try {
            ++_configurationChanges;
            for (G1AudioVoice* voice in _voices) if (voice.active) ++_configurationDroppedVoices;
            [self stopAll];
            [_engine stop];
            ++_generation;
            [_voices removeAllObjects];
            [self createEngine];
            if (_foreground && !_interrupted) [self startEngineAndResume];
        } catch (const std::exception& error) { [self recordFailure:audio_string(error.what())]; }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio reconfiguration exception"]; }
}

- (void)mediaReset:(NSNotification*)notification {
    (void)notification;
    if (_failed) return;
    @try {
        try {
            ++_mediaResets;
            const auto loaded = _control.loaded;
            [self stopAll];
            [_engine stop];
            ++_generation;
            [_voices removeAllObjects];
            [_buffers removeAllObjects];
            [self createEngine];
            for (std::size_t slot=0;slot<loaded.size();++slot)
                if (loaded[slot]) [self loadSlot:static_cast<std::uint8_t>(slot)];
            if (_foreground && !_interrupted) [self startEngineAndResume];
        } catch (const std::exception& error) { [self recordFailure:audio_string(error.what())]; }
    } @catch (NSException* exception) { [self recordFailure:exception.reason ?: @"Audio media-reset exception"]; }
}

- (BOOL)operational { return !_failed && _engine != nil && _archive != nullptr; }

- (NSDictionary*)diagnostics {
    NSMutableArray* loaded = [NSMutableArray new];
    std::uint64_t frames = 0;
    NSUInteger active = 0, paused = 0, playing = 0;
    for (std::size_t i=0;i<_control.loaded.size();++i) if (_buffers[@(i)]) {
        [loaded addObject:@(i)]; frames += _buffers[@(i)].frameLength;
    }
    for (G1AudioVoice* voice in _voices) if (voice.active) {
        ++active;
        if (voice.paused) ++paused;
        if (voice.node.isPlaying && _engine.isRunning) ++playing;
    }
    return @{@"host":@"AVAudioEngine / AVAudioPlayerNode",@"operational":@([self operational]),
        @"engine_running":@(_engine.isRunning),@"session_category":@"ambient",
        @"foreground":@(_foreground),@"interrupted":@(_interrupted),
        @"native_music_preference":@(_configuration.enabled_a4),
        @"native_sound_preference":@(_configuration.enabled_a3),
        @"preference_defaults":@"Native Music/Sound ON; historical cold defaults not claimed",
        @"decoded_rate_hz":@22050,@"decoded_channels":@1,@"cached_slots":loaded,
        @"cached_pcm_frames":@(frames),@"requests_received":@(_requests),
        @"buffers_loaded":@(_loads),@"buffers_unloaded":@(_unloads),
        @"one_shot_buffers_scheduled":@(_scheduled),@"data_played_back_callbacks":@(_playedBackCallbacks),
        @"explicitly_stopped_voices":@(_explicitlyStopped),@"active_voices":@(active),
        @"paused_voices":@(paused),@"playing_nodes":@(playing),@"voice_bound":@(max_voices),
        @"interruptions":@(_interruptionCount),@"configuration_changes":@(_configurationChanges),
        @"configuration_dropped_voices":@(_configurationDroppedVoices),@"media_service_resets":@(_mediaResets),
        @"last_music_slot":@(_control.last_music_slot),@"last_played_slot":@(_control.last_played_slot),
        @"automatic_looping":@NO,@"device_audibility_verified":@NO,@"errors":[_errors copy],
        @"measurement_scope":@"Framework scheduling/state and completion callbacks; not a microphone or audibility measurement",
        @"lifecycle_policy":@"Pause retained requests on background/interruption; resume permitted requests; configuration/reset drops invalid voices without replay"};
}
@end
