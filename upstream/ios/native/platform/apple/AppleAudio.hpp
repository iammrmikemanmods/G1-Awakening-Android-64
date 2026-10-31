#pragma once

#import <Foundation/Foundation.h>
#include "g1/AudioEvents.hpp"

// Main-thread native output adapter. Original AudioEvents chooses every load,
// unload, stop and play operation. The adapter never creates a music loop.
@interface G1AppleAudio : NSObject
- (instancetype)initWithArchive:(const g1::Archive&)archive;
- (bool)request:(g1::MissionAudioRequest)request;
- (void)setForegroundActive:(BOOL)active;
- (void)setMusicEnabled:(BOOL)music soundEnabled:(BOOL)sound;
- (BOOL)musicEnabled;
- (BOOL)soundEnabled;
- (BOOL)operational;
- (NSDictionary*)diagnostics;
@end
