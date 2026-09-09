/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2014 - Chris Moeller
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <features/features_cpu.h>

#if TARGET_OS_IPHONE
#include <AudioToolbox/AudioToolbox.h>
#else
#include <CoreAudio/CoreAudio.h>
#endif

#include <CoreAudio/CoreAudioTypes.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioUnit/AUComponent.h>

#include <boolean.h>
#include <queues/fifo_queue.h>
#include <rthreads/rthreads.h>
#include <retro_endianness.h>
#include <string/stdstring.h>

#include <defines/cocoa_defines.h>

#include "../audio_driver.h"
#include "../../verbosity.h"
#include "../../runloop.h"
#include "../../joyemu_cadence_trace_compat.h"

typedef struct coreaudio_observation
{
   uint64_t callbacks, requested, zeroed, held;
   uint64_t offered, accepted, writes, short_writes, write_us, max_write_gap_us;
} coreaudio_observation_t;

typedef struct coreaudio
{
   slock_t *lock;
   scond_t *cond;
#if !HAS_MACOSX_10_12
   ComponentInstance dev;
#else
   AudioComponentInstance dev;
#endif
   fifo_buffer_t *buffer;
   size_t buffer_size;
   bool dev_alive;
   bool is_paused;
   bool nonblock;
   bool observe_audio;
   double observed_rate;
   retro_time_t observation_start, previous_write;
   coreaudio_observation_t observation; /* Protected by the existing FIFO lock. */
} coreaudio_t;

static bool coreaudio_should_observe(const char *core, const char *setting)
{
   return string_is_equal(core, "Azahar") && string_is_equal(setting, "1");
}

/* Called only from the producer/teardown thread. Never log from RemoteIO. */
static void coreaudio_report_observation(coreaudio_t *dev, bool force)
{
   coreaudio_observation_t value;
   retro_time_t now, elapsed;
   size_t queued;
   if (!dev->observe_audio || !dev->lock)
      return;
   now = cpu_features_get_time_usec();
   elapsed = now - dev->observation_start;
   if (!force && elapsed < 1000000)
      return;
   slock_lock(dev->lock);
   value = dev->observation;
   memset(&dev->observation, 0, sizeof(dev->observation));
   queued = dev->buffer ? FIFO_READ_AVAIL(dev->buffer) : 0;
   dev->observation_start = now;
   slock_unlock(dev->lock);
   fprintf(stderr, "AZAHAR-AUDIO window_us=%lld rate=%.0f frame_bytes=%zu capacity=%zu "
         "callbacks=%llu requested=%llu zeroed=%llu held=%llu offered=%llu accepted=%llu "
         "writes=%llu short_writes=%llu write_us=%llu max_write_gap_us=%llu queued=%zu final=%d\n",
         (long long)elapsed, dev->observed_rate, 2 * sizeof(float), dev->buffer_size,
         (unsigned long long)value.callbacks, (unsigned long long)value.requested,
         (unsigned long long)value.zeroed, (unsigned long long)value.held,
         (unsigned long long)value.offered, (unsigned long long)value.accepted,
         (unsigned long long)value.writes, (unsigned long long)value.short_writes,
         (unsigned long long)value.write_us, (unsigned long long)value.max_write_gap_us,
         queued, force);
}

static void coreaudio_free(void *data)
{
   coreaudio_t *dev = (coreaudio_t*)data;

   if (!dev)
      return;

   if (dev->dev_alive)
   {
      AudioOutputUnitStop(dev->dev);
#if !HAS_MACOSX_10_12
      CloseComponent(dev->dev);
#else
      AudioComponentInstanceDispose(dev->dev);
#endif
   }

   if (dev->buffer)
   {
      coreaudio_report_observation(dev, true);
      fifo_free(dev->buffer);
   }

   slock_free(dev->lock);
   scond_free(dev->cond);

   free(dev);
}

static OSStatus coreaudio_audio_write_cb(void *userdata,
      AudioUnitRenderActionFlags *action_flags,
      const AudioTimeStamp *time_stamp, UInt32 bus_number,
      UInt32 number_frames, AudioBufferList *io_data)
{
   unsigned write_avail;
   void     *outbuf = NULL;
   coreaudio_t *dev = (coreaudio_t*)userdata;

   (void)time_stamp;
   (void)bus_number;
   (void)number_frames;

   if (!io_data || io_data->mNumberBuffers != 1)
      return noErr;

   write_avail = io_data->mBuffers[0].mDataByteSize;
   outbuf      = io_data->mBuffers[0].mData;

   slock_lock(dev->lock);

   if (dev->observe_audio)
   {
      size_t available = FIFO_READ_AVAIL(dev->buffer);
      dev->observation.callbacks++;
      dev->observation.requested += write_avail;
      if (available < write_avail)
      {
         dev->observation.zeroed += write_avail;
         dev->observation.held += available;
      }
   }

   if (FIFO_READ_AVAIL(dev->buffer) < write_avail)
   {
      *action_flags = kAudioUnitRenderAction_OutputIsSilence;
      /* Seems to be needed. */
      memset(outbuf, 0, write_avail);
   }
   else
      fifo_read(dev->buffer, outbuf, write_avail);
   slock_unlock(dev->lock);
   scond_signal(dev->cond);
   return noErr;
}

#if !TARGET_OS_IPHONE
static void coreaudio_choose_output_device(coreaudio_t *dev, const char* device)
{
   int i;
   UInt32 device_count;
   AudioObjectPropertyAddress propaddr;
   AudioDeviceID *devices = NULL;
   UInt32 size = 0;

   propaddr.mSelector = kAudioHardwarePropertyDevices;
#if HAS_MACOSX_10_12
   propaddr.mScope    = kAudioObjectPropertyScopeOutput;
#else
   propaddr.mScope    = kAudioObjectPropertyScopeGlobal;
#endif
   propaddr.mElement  = kAudioObjectPropertyElementMaster;

   if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
            &propaddr, 0, 0, &size) != noErr)
      return;

   device_count = size / sizeof(AudioDeviceID);
   devices      = (AudioDeviceID*)malloc(size);

   if (devices && AudioObjectGetPropertyData(kAudioObjectSystemObject,
            &propaddr, 0, 0, &size, devices) == noErr)
   {
#if HAS_MACOSX_10_12
#else
      propaddr.mScope    = kAudioDevicePropertyScopeOutput;
#endif
      propaddr.mSelector = kAudioDevicePropertyDeviceName;

      for (i = 0; i < (int)device_count; i ++)
      {
         char device_name[1024];
         device_name[0] = 0;
         size           = 1024;

         if (AudioObjectGetPropertyData(devices[i],
                  &propaddr, 0, 0, &size, device_name) == noErr
               && string_is_equal(device_name, device))
         {
            AudioUnitSetProperty(dev->dev, kAudioOutputUnitProperty_CurrentDevice,
                  kAudioUnitScope_Global, 0, &devices[i], sizeof(AudioDeviceID));
            break;
         }
      }
   }

   free(devices);
}
#endif

static void *coreaudio_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   size_t fifo_size;
   UInt32 i_size;
   AudioStreamBasicDescription real_desc;
#if !HAS_MACOSX_10_12
   Component comp;
#else
   AudioComponent comp;
#endif
#ifndef TARGET_OS_IPHONE
   AudioChannelLayout layout               = {0};
#endif
   AURenderCallbackStruct cb               = {0};
   AudioStreamBasicDescription stream_desc = {0};
#if !HAS_MACOSX_10_12
   ComponentDescription desc               = {0};
#else
   AudioComponentDescription desc          = {0};
#endif
   coreaudio_t *dev                        = (coreaudio_t*)
      calloc(1, sizeof(*dev));
   if (!dev)
      return NULL;

   dev->lock = slock_new();
   dev->cond = scond_new();
   dev->observe_audio = coreaudio_should_observe(
         runloop_state_get_ptr()->system.info.library_name,
         getenv("JOY_AZAHAR_AUDIO_DIAGNOSTICS"));

   /* Create AudioComponent */
   desc.componentType         = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
   desc.componentSubType      = kAudioUnitSubType_RemoteIO;
#else
   desc.componentSubType      = kAudioUnitSubType_HALOutput;
#endif
   desc.componentManufacturer = kAudioUnitManufacturer_Apple;

#if !HAS_MACOSX_10_12
   if (!(comp = FindNextComponent(NULL, &desc)))
      goto error;
#else
   if (!(comp = AudioComponentFindNext(NULL, &desc)))
      goto error;
#endif

#if !HAS_MACOSX_10_12
   if ((OpenAComponent(comp, &dev->dev) != noErr))
      goto error;
#else
   if ((AudioComponentInstanceNew(comp, &dev->dev) != noErr))
      goto error;
#endif

#if !TARGET_OS_IPHONE
   if (device)
      coreaudio_choose_output_device(dev, device);
#endif

   dev->dev_alive                = true;

   /* Set audio format */
   stream_desc.mSampleRate       = rate;
   stream_desc.mBitsPerChannel   = sizeof(float) * CHAR_BIT;
   stream_desc.mChannelsPerFrame = 2;
   stream_desc.mBytesPerPacket   = 2 * sizeof(float);
   stream_desc.mBytesPerFrame    = 2 * sizeof(float);
   stream_desc.mFramesPerPacket  = 1;
   stream_desc.mFormatID         = kAudioFormatLinearPCM;
   stream_desc.mFormatFlags      = kAudioFormatFlagIsFloat
                                 | kAudioFormatFlagIsPacked;

   if (!is_little_endian())
      stream_desc.mFormatFlags  |= kAudioFormatFlagIsBigEndian;

   if (AudioUnitSetProperty(dev->dev, kAudioUnitProperty_StreamFormat,
         kAudioUnitScope_Input, 0, &stream_desc, sizeof(stream_desc)) != noErr)
      goto error;

   /* Check returned audio format. */
   i_size = sizeof(real_desc);
   if (AudioUnitGetProperty(dev->dev, kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input, 0, &real_desc, &i_size) != noErr)
      goto error;

   if (real_desc.mChannelsPerFrame != stream_desc.mChannelsPerFrame)
      goto error;
   if (real_desc.mBitsPerChannel != stream_desc.mBitsPerChannel)
      goto error;
   if (real_desc.mFormatFlags != stream_desc.mFormatFlags)
      goto error;
   if (real_desc.mFormatID != stream_desc.mFormatID)
      goto error;

   RARCH_LOG("[CoreAudio] Using output sample rate of %.1f Hz.\n",
         (float)real_desc.mSampleRate);
   *new_rate = real_desc.mSampleRate;
   dev->observed_rate = real_desc.mSampleRate;
   if (dev->observe_audio)
      dev->observation_start = cpu_features_get_time_usec();

   /* Set channel layout (fails on iOS). */
#ifndef TARGET_OS_IPHONE
   layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
   if (AudioUnitSetProperty(dev->dev, kAudioUnitProperty_AudioChannelLayout,
         kAudioUnitScope_Input, 0, &layout, sizeof(layout)) != noErr)
      goto error;
#endif

   /* Set callbacks and finish up. */
   cb.inputProc       = coreaudio_audio_write_cb;
   cb.inputProcRefCon = dev;

   if (AudioUnitSetProperty(dev->dev, kAudioUnitProperty_SetRenderCallback,
         kAudioUnitScope_Input, 0, &cb, sizeof(cb)) != noErr)
      goto error;

   if (AudioUnitInitialize(dev->dev) != noErr)
      goto error;

   fifo_size         = (latency * (*new_rate)) / 1000;
   fifo_size        *= 2 * sizeof(float);
   dev->buffer_size  = fifo_size;

   if (!(dev->buffer = fifo_new(fifo_size)))
      goto error;

   RARCH_LOG("[CoreAudio] Using buffer size of %u bytes: (latency = %u ms).\n",
         (unsigned)fifo_size, latency);

   if (AudioOutputUnitStart(dev->dev) != noErr)
      goto error;

   return dev;

error:
   RARCH_ERR("[CoreAudio] Failed to initialize driver.\n");
   coreaudio_free(dev);
   return NULL;
}

static ssize_t coreaudio_write(void *data, const void *buf_, size_t len)
{
   coreaudio_t *dev   = (coreaudio_t*)data;
   const uint8_t *buf = (const uint8_t*)buf_;
   size_t _len        = 0;
   size_t offered     = len;
   retro_time_t began = dev->observe_audio ? cpu_features_get_time_usec() : 0;
   joyemu_cadence_trace_token_t blocked_trace =
      joyemu_cadence_trace_begin(JOYEMU_CADENCE_TRACE_AUDIO_BLOCKED);

   while (!dev->is_paused && len > 0)
   {
      size_t write_avail;

      slock_lock(dev->lock);

      write_avail = FIFO_WRITE_AVAIL(dev->buffer);
      if (write_avail > len)
         write_avail = len;

      fifo_write(dev->buffer, buf, write_avail);
      buf     += write_avail;
      _len    += write_avail;
      len     -= write_avail;

      if (dev->nonblock)
      {
         slock_unlock(dev->lock);
         break;
      }

#if TARGET_OS_IOS
      if (write_avail == 0 && !scond_wait_timeout(
               dev->cond, dev->lock, 300000))
      {
         slock_unlock(dev->lock);
         break;
      }
#else
      if (write_avail == 0)
         scond_wait(dev->cond, dev->lock);
#endif
      slock_unlock(dev->lock);
   }

   joyemu_cadence_trace_end(JOYEMU_CADENCE_TRACE_AUDIO_BLOCKED, blocked_trace);
   if (dev->observe_audio)
   {
      retro_time_t ended = cpu_features_get_time_usec();
      uint64_t gap = dev->previous_write ? (uint64_t)(began - dev->previous_write) : 0;
      slock_lock(dev->lock);
      dev->observation.offered += offered;
      dev->observation.accepted += _len;
      dev->observation.writes++;
      dev->observation.short_writes += _len < offered;
      dev->observation.write_us += (uint64_t)(ended - began);
      if (gap > dev->observation.max_write_gap_us)
         dev->observation.max_write_gap_us = gap;
      dev->previous_write = began;
      slock_unlock(dev->lock);
      coreaudio_report_observation(dev, false);
   }
   return _len;
}

static void coreaudio_set_nonblock_state(void *data, bool state)
{
   coreaudio_t *dev = (coreaudio_t*)data;
   if (dev)
      dev->nonblock = state;
}

static bool coreaudio_alive(void *data)
{
   coreaudio_t *dev = (coreaudio_t*)data;
   if (!dev)
      return false;
   return !dev->is_paused;
}

static bool coreaudio_stop(void *data)
{
   coreaudio_t *dev = (coreaudio_t*)data;
   if (dev)
   {
      dev->is_paused = (AudioOutputUnitStop(dev->dev) == noErr) ? true : false;
      if (dev->is_paused)
         return true;
   }
   return false;
}

static bool coreaudio_start(void *data, bool is_shutdown)
{
   coreaudio_t *dev = (coreaudio_t*)data;
   if (dev)
   {
      dev->is_paused = (AudioOutputUnitStart(dev->dev) == noErr) ? false : true;
      if (!dev->is_paused)
         return true;
   }
   return false;
}

static bool coreaudio_use_float(void *data) { return true; }

static size_t coreaudio_write_avail(void *data)
{
   size_t avail;
   coreaudio_t *dev = (coreaudio_t*)data;

   slock_lock(dev->lock);
   avail = FIFO_WRITE_AVAIL(dev->buffer);
   slock_unlock(dev->lock);

   return avail;
}

static size_t coreaudio_buffer_size(void *data)
{
   coreaudio_t *dev = (coreaudio_t*)data;
   return dev->buffer_size;
}

/* TODO/FIXME - implement */
static void *coreaudio_device_list_new(void *data) { return NULL; }
static void coreaudio_device_list_free(void *data, void *array_list_data) { }

audio_driver_t audio_coreaudio = {
   coreaudio_init,
   coreaudio_write,
   coreaudio_stop,
   coreaudio_start,
   coreaudio_alive,
   coreaudio_set_nonblock_state,
   coreaudio_free,
   coreaudio_use_float,
   "coreaudio",
   coreaudio_device_list_new,
   coreaudio_device_list_free,
   coreaudio_write_avail,
   coreaudio_buffer_size,
};
