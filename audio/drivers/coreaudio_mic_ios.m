/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2025 - Joseph Mattiello
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 */

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include "audio/microphone_driver.h"
#include "queues/fifo_queue.h"
#include "verbosity.h"
#include <memory.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>

#include "audio/audio_driver.h"
#include "../../verbosity.h"
#include "../../configuration.h"

typedef struct coreaudio_microphone
{
    AudioUnit audio_unit; /* CoreAudio audio unit */
    AudioStreamBasicDescription format; /* Audio format */
    fifo_buffer_t *sample_buffer; /* Sample buffer */
    bool is_running; /* Whether the microphone is running */
    bool nonblock; /* Non-blocking mode flag */
    int sample_rate; /* Current sample rate */
    bool use_float; /* Whether to use float format */
} coreaudio_microphone_t;

static OSStatus coreaudio_input_callback(
    void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags,
    const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber,
    UInt32 inNumberFrames,
    AudioBufferList *ioData)
{
    coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)inRefCon;

    UInt32 bufferSize = inNumberFrames * microphone->format.mBytesPerFrame;
    if (bufferSize == 0)
        return kAudio_ParamError;

    uint8_t stackBuf[4096];
    if (bufferSize > sizeof(stackBuf))
        return kAudio_ParamError;

    AudioBufferList bufferList;
    bufferList.mNumberBuffers              = 1;
    bufferList.mBuffers[0].mDataByteSize   = bufferSize;
    bufferList.mBuffers[0].mData           = stackBuf;

    OSStatus status = AudioUnitRender(microphone->audio_unit,
                                      ioActionFlags,
                                      inTimeStamp,
                                      inBusNumber,
                                      inNumberFrames,
                                      &bufferList);

    if (status == noErr) {
        size_t writeAvail = FIFO_WRITE_AVAIL(microphone->sample_buffer);
        size_t actual     = bufferList.mBuffers[0].mDataByteSize;
        if (actual <= writeAvail)
            fifo_write(microphone->sample_buffer,
                       bufferList.mBuffers[0].mData, actual);

    }

    return status;
}

/* Initialize CoreAudio microphone driver
 *
 * NOTE: driver_context and mic_context MUST be distinct allocations (see
 * microphone_driver.h). Previously this returned a calloc'd
 * coreaudio_microphone_t and open_mic/close_mic aliased that same pointer as
 * the mic handle — which caused a double free on shutdown:
 *   microphone_driver_deinit -> close_mic (free) -> driver->free (free) -> abort().
 * We follow the same pattern as coreaudio_mic_macos.m: the driver state is a
 * non-NULL placeholder, and open_mic allocates a fresh mic handle. */
static void *coreaudio_microphone_init(void)
{
   return (void*)1;
}

/* Free CoreAudio microphone driver.
 * driver_context is a placeholder from init() — nothing to free. */
static void coreaudio_microphone_free(void *driver_context)
{
   (void)driver_context;
}

/* Read samples from microphone */
static int coreaudio_microphone_read(void *driver_context,
      void *microphone_context, void *buf, size_t size)
{
   coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)microphone_context;
   size_t avail, read_amt;
   (void)driver_context;

   if (!microphone || !buf)
   {
      RARCH_ERR("[CoreAudio] Invalid parameters in read.\n");
      return -1;
   }

   avail    = FIFO_READ_AVAIL(microphone->sample_buffer);
   read_amt = MIN(avail, size);

   if (microphone->nonblock && read_amt == 0)
      return 0; /* Return immediately in non-blocking mode */

   if (read_amt > 0)
   {
      fifo_read(microphone->sample_buffer, buf, read_amt);
   }

   return (int)read_amt;
}

/* Set non-blocking state.
 * driver_context is a placeholder in this driver; read() already returns
 * immediately with whatever is currently available, so nonblock is a no-op. */
static void coreaudio_microphone_set_nonblock_state(void *driver_context, bool state)
{
   (void)driver_context;
   (void)state;
}

/* Helper method to set audio format */
static void coreaudio_microphone_set_format(coreaudio_microphone_t *microphone, bool use_float)
{
   microphone->use_float           = use_float; /* Store the format choice */
   microphone->format.mSampleRate  = microphone->sample_rate;
   microphone->format.mFormatID    = kAudioFormatLinearPCM;
   microphone->format.mFormatFlags = use_float
      ? (kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked)
      : (kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
   microphone->format.mFramesPerPacket  = 1;
   microphone->format.mChannelsPerFrame = 1;
   microphone->format.mBitsPerChannel = use_float ? 32 : 16;
   microphone->format.mBytesPerFrame  = microphone->format.mChannelsPerFrame * microphone->format.mBitsPerChannel / 8;
   microphone->format.mBytesPerPacket = microphone->format.mBytesPerFrame * microphone->format.mFramesPerPacket;

   RARCH_LOG("[CoreAudio] Format setup: sample_rate=%d, bits=%d, bytes_per_frame=%d.\n",
         (int)microphone->format.mSampleRate,
         microphone->format.mBitsPerChannel,
         microphone->format.mBytesPerFrame);
}

static void *coreaudio_microphone_open_mic(void *driver_context,
      const char *device,
      unsigned rate,
      unsigned latency,
      unsigned *new_rate)
{
   (void)driver_context;

   coreaudio_microphone_t *microphone =
         (coreaudio_microphone_t*)calloc(1, sizeof(*microphone));
   if (!microphone)
   {
      RARCH_ERR("[CoreAudio] Failed to allocate microphone handle.\n");
      return NULL;
   }

   microphone->use_float = false;

   if (rate != 44100 && rate != 48000)
   {
      RARCH_WARN("[CoreAudio] Requested sample rate %u not supported, defaulting to 48000.\n", rate);
      rate = 48000;
   }
   microphone->sample_rate = rate;

#if TARGET_OS_IPHONE
   AVAudioSession *audioSession = [AVAudioSession sharedInstance];
   NSError *error = nil;
   [audioSession setCategory:AVAudioSessionCategoryPlayAndRecord
                 withOptions:AVAudioSessionCategoryOptionMixWithOthers
                           | AVAudioSessionCategoryOptionAllowBluetoothA2DP
                           | AVAudioSessionCategoryOptionAllowAirPlay
                           | AVAudioSessionCategoryOptionDefaultToSpeaker
                       error:&error];
   if (error)
   {
      RARCH_ERR("[CoreAudio] Failed to set audio session category: %s.\n",
                [[error localizedDescription] UTF8String]);
      free(microphone);
      return NULL;
   }

   [audioSession setActive:YES error:&error];
   if (error)
   {
      RARCH_ERR("[CoreAudio] Failed to activate audio session: %s.\n",
                [[error localizedDescription] UTF8String]);
      free(microphone);
      return NULL;
   }
   RARCH_LOG("[CoreAudio] Audio session activated (PlayAndRecord).\n");

   if (@available(iOS 13.0, *)) {
      if ([audioSession respondsToSelector:@selector(setAllowHapticsAndSystemSoundsDuringRecording:error:)]) {
         BOOL ok = [audioSession setAllowHapticsAndSystemSoundsDuringRecording:YES error:&error];
         if (!ok || error) {
            RARCH_WARN("[CoreAudio] Failed to allow haptics during recording: %s\n",
                       error ? [[error localizedDescription] UTF8String] : "unknown");
            error = nil;
         }
      }
   }

   [audioSession setPreferredSampleRate:rate error:&error];
   if (error)
   {
      RARCH_ERR("[CoreAudio] Failed to set preferred sample rate: %s.\n",
                [[error localizedDescription] UTF8String]);
      free(microphone);
      return NULL;
   }

   Float64 actualRate = [audioSession sampleRate];
   if (new_rate)
      *new_rate = (unsigned)actualRate;
   microphone->sample_rate = (int)actualRate;

   RARCH_LOG("[CoreAudio] Using sample rate: %d Hz.\n", microphone->sample_rate);
#else

#endif

   coreaudio_microphone_set_format(microphone, false);

   size_t fifoBufferSize = (latency * microphone->sample_rate * microphone->format.mBytesPerFrame) / 1000;
   if (fifoBufferSize == 0)
   {
      RARCH_WARN("[CoreAudio] Calculated FIFO buffer size is 0 for latency: %u, sample_rate: %d, bytes_per_frame: %d.\n",
            latency, microphone->sample_rate, microphone->format.mBytesPerFrame);
      fifoBufferSize = 1024;
   }

   RARCH_LOG("[CoreAudio] FIFO buffer size: %zu bytes.\n", fifoBufferSize);

   microphone->sample_buffer = fifo_new(fifoBufferSize);
   if (!microphone->sample_buffer)
   {
      RARCH_ERR("[CoreAudio] Failed to create sample buffer.\n");
      free(microphone);
      return NULL;
   }

   AudioComponentDescription desc = {
      .componentType = kAudioUnitType_Output,
#if TARGET_OS_IPHONE
      .componentSubType = kAudioUnitSubType_RemoteIO,
#else
      .componentSubType = kAudioUnitSubType_HALOutput,
#endif
      .componentManufacturer = kAudioUnitManufacturer_Apple,
      .componentFlags = 0,
      .componentFlagsMask = 0
   };

   AudioComponent comp = AudioComponentFindNext(NULL, &desc);
   OSStatus status     = AudioComponentInstanceNew(comp, &microphone->audio_unit);
   if (status != noErr)
   {
      RARCH_ERR("[CoreAudio] Failed to create audio unit.\n");
      goto error;
   }

   UInt32 flag = 1;
   status = AudioUnitSetProperty(microphone->audio_unit,
         kAudioOutputUnitProperty_EnableIO,
         kAudioUnitScope_Input,
         1,
         &flag,
         sizeof(flag));
   if (status != noErr)
   {
      RARCH_ERR("[CoreAudio] Failed to enable input.\n");
      goto error;
   }

   status = AudioUnitSetProperty(microphone->audio_unit,
         kAudioUnitProperty_StreamFormat,
         kAudioUnitScope_Output,
         1,
         &microphone->format,
         sizeof(microphone->format));
   if (status != noErr)
   {
      RARCH_ERR("[CoreAudio] Failed to set format: %d.\n", status);
      goto error;
   }

   AURenderCallbackStruct callback = { coreaudio_input_callback, microphone };
   status = AudioUnitSetProperty(microphone->audio_unit,
         kAudioOutputUnitProperty_SetInputCallback,
         kAudioUnitScope_Global,
         1,
         &callback,
         sizeof(callback));
   if (status != noErr)
   {
      RARCH_ERR("[CoreAudio] Failed to set callback.\n");
      goto error;
   }

   status = AudioUnitInitialize(microphone->audio_unit);
   if (status != noErr)
   {
      RARCH_ERR("[CoreAudio] Failed to initialize audio unit: %d.\n", status);
      goto error;
   }

   microphone->is_running = false;
   RARCH_LOG("[CoreAudio] Microphone opened successfully (idle, waiting for start_mic).\n");

   return microphone;

error:
   if (microphone)
   {
      if (microphone->audio_unit)
      {
         AudioComponentInstanceDispose(microphone->audio_unit);
         microphone->audio_unit = nil;
      }
      if (microphone->sample_buffer)
      {
         fifo_free(microphone->sample_buffer);
         microphone->sample_buffer = NULL;
      }
      free(microphone);
   }
   return NULL;
}

static void coreaudio_microphone_close_mic(void *driver_context, void *microphone_context)
{
   coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)microphone_context;
   if (microphone)
   {
      if (microphone->audio_unit)
      {
         AudioOutputUnitStop(microphone->audio_unit);
         AudioComponentInstanceDispose(microphone->audio_unit);
         microphone->audio_unit = nil;
      }
      microphone->is_running = false;
      if (microphone->sample_buffer)
         fifo_free(microphone->sample_buffer);
      free(microphone);
      RARCH_LOG("[CoreAudio] Microphone closed.\n");
   }
   else
   {
      RARCH_ERR("[CoreAudio] Failed to close microphone (null context).\n");
   }

#if TARGET_OS_IPHONE
   AVAudioSessionCategoryOptions options =
         AVAudioSessionCategoryOptionMixWithOthers
       | AVAudioSessionCategoryOptionAllowBluetoothA2DP
       | AVAudioSessionCategoryOptionAllowAirPlay;
   settings_t *settings = config_get_ptr();
   if (settings && settings->bools.audio_respect_silent_mode)
      [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient
                                       withOptions:options error:nil];
   else
      [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback
                                       withOptions:options error:nil];
#endif
}

static bool coreaudio_microphone_start_mic(void *driver_context, void *microphone_context)
{
   coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)microphone_context;

   if (!microphone || !microphone->audio_unit)
   {
      RARCH_ERR("[CoreAudio] Failed to start microphone (null context or audio_unit).\n");
      return false;
   }

   if (microphone->is_running)
      return true;

#if TARGET_OS_IPHONE
   AVAudioSession *audioSession = [AVAudioSession sharedInstance];
   if (![audioSession.category isEqualToString:AVAudioSessionCategoryPlayAndRecord])
   {
      NSError *error = nil;
      [audioSession setCategory:AVAudioSessionCategoryPlayAndRecord
                    withOptions:AVAudioSessionCategoryOptionMixWithOthers
                              | AVAudioSessionCategoryOptionAllowBluetoothA2DP
                              | AVAudioSessionCategoryOptionAllowAirPlay
                              | AVAudioSessionCategoryOptionDefaultToSpeaker
                          error:&error];
      if (error)
         RARCH_WARN("[CoreAudio] start_mic: failed to re-set PlayAndRecord: %s.\n",
                    [[error localizedDescription] UTF8String]);

      [audioSession setActive:YES error:&error];
      if (error)
         RARCH_WARN("[CoreAudio] start_mic: failed to re-activate session: %s.\n",
                    [[error localizedDescription] UTF8String]);

      if (@available(iOS 13.0, *)) {
         if ([audioSession respondsToSelector:@selector(setAllowHapticsAndSystemSoundsDuringRecording:error:)]) {
            error = nil;
            BOOL ok = [audioSession setAllowHapticsAndSystemSoundsDuringRecording:YES error:&error];
            if (!ok || error)
               RARCH_WARN("[CoreAudio] start_mic: failed to re-allow haptics: %s.\n",
                          error ? [[error localizedDescription] UTF8String] : "unknown");
         }
      }

      RARCH_LOG("[CoreAudio] start_mic: reconfigured session to PlayAndRecord.\n");
   }
#endif

   if (microphone->sample_buffer)
      fifo_clear(microphone->sample_buffer);

   OSStatus status = AudioOutputUnitStart(microphone->audio_unit);
   if (status == noErr)
   {
      microphone->is_running = true;
      return true;
   }
   RARCH_ERR("[CoreAudio] Failed to start microphone: %d.\n", status);
   return false;
}

/* Stop microphone */
static bool coreaudio_microphone_stop_mic(void *driver_context, void *microphone_context)
{
   coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)microphone_context;
   if (!microphone)
   {
      RARCH_ERR("[CoreAudio] Failed to stop microphone.\n");
      return false;
   }

   if (microphone->is_running)
   {
      OSStatus status = AudioOutputUnitStop(microphone->audio_unit);
      if (status == noErr)
      {
         microphone->is_running = false;
         return true;
      }
      RARCH_ERR("[CoreAudio] Failed to stop microphone: %d.\n", status);
   }
   return true; /* Already stopped */
}

/* Check if microphone is alive */
static bool coreaudio_microphone_mic_alive(const void *driver_context, const void *microphone_context)
{
    coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)microphone_context;
    return microphone && microphone->is_running;
}

/* Check if microphone uses float samples */
static bool coreaudio_microphone_mic_use_float(const void *driver_context, const void *microphone_context)
{
   coreaudio_microphone_t *microphone = (coreaudio_microphone_t*)microphone_context;
   return microphone && microphone->use_float;
}

/* Get device list (not implemented for CoreAudio) */
static struct string_list *coreaudio_microphone_device_list_new(const void *driver_context)
{
    return NULL;
}

/* Free device list (not implemented for CoreAudio) */
static void coreaudio_microphone_device_list_free(const void *driver_context, struct string_list *devices)
{
}

/* CoreAudio microphone driver structure */
microphone_driver_t microphone_coreaudio = {
    coreaudio_microphone_init,
    coreaudio_microphone_free,
    coreaudio_microphone_read,
    coreaudio_microphone_set_nonblock_state,
    "coreaudio",
    coreaudio_microphone_device_list_new,
    coreaudio_microphone_device_list_free,
    coreaudio_microphone_open_mic,
    coreaudio_microphone_close_mic,
    coreaudio_microphone_mic_alive,
    coreaudio_microphone_start_mic,
    coreaudio_microphone_stop_mic,
    coreaudio_microphone_mic_use_float
};
