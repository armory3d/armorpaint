#ifdef IRON_AUDIO

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <iron_audio.h>
#include <iron_system.h>
#include <stdlib.h>

static bool             initialized = false;
static AudioUnit        unit;
static iron_a2_buffer_t a2_buffer;
static uint32_t         samples_per_second = 44100;

uint32_t iron_a2_samples_per_second(void) {
	return samples_per_second;
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *out) {
	iron_a2_internal_callback(&a2_buffer, frames);
	float *left  = (float *)out->mBuffers[0].mData;
	float *right = (float *)out->mBuffers[1].mData;
	for (UInt32 i = 0; i < frames; ++i) {
		left[i]  = a2_buffer.channels[0][a2_buffer.read_location];
		right[i] = a2_buffer.channels[1][a2_buffer.read_location];
		a2_buffer.read_location += 1;
		if (a2_buffer.read_location >= a2_buffer.data_size) {
			a2_buffer.read_location = 0;
		}
	}
	return noErr;
}

static void check(OSStatus err, const char *what) {
	if (err != noErr) {
		iron_log("Audio: %s failed (%i)", what, (int)err);
	}
}

void iron_a2_init(void) {
	if (initialized) {
		return;
	}
	initialized = true;

	iron_a2_internal_init();
	a2_buffer.read_location  = 0;
	a2_buffer.write_location = 0;
	a2_buffer.data_size      = 128 * 1024;
	a2_buffer.channel_count  = 2;
	a2_buffer.channels[0]    = (float *)calloc(a2_buffer.data_size, sizeof(float));
	a2_buffer.channels[1]    = (float *)calloc(a2_buffer.data_size, sizeof(float));

	// ios_system.m has set the category
	AVAudioSession *session = [AVAudioSession sharedInstance];
	[session setActive:YES error:nil];
	samples_per_second = (uint32_t)session.sampleRate;
	iron_a2_internal_sample_rate_callback();

	AudioComponentDescription desc = {0};
	desc.componentType             = kAudioUnitType_Output;
	desc.componentSubType          = kAudioUnitSubType_RemoteIO;
	desc.componentManufacturer     = kAudioUnitManufacturer_Apple;
	check(AudioComponentInstanceNew(AudioComponentFindNext(NULL, &desc), &unit), "AudioComponentInstanceNew");

	AudioStreamBasicDescription format = {0};
	format.mSampleRate                 = samples_per_second;
	format.mFormatID                   = kAudioFormatLinearPCM;
	format.mFormatFlags                = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
	format.mFramesPerPacket            = 1;
	format.mChannelsPerFrame           = 2;
	format.mBitsPerChannel             = 32;
	format.mBytesPerFrame              = sizeof(float); // Per channel, as they are not interleaved
	format.mBytesPerPacket             = sizeof(float);
	check(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)), "StreamFormat");

	AURenderCallbackStruct callback = {render, NULL};
	check(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)), "SetRenderCallback");
	check(AudioUnitInitialize(unit), "AudioUnitInitialize");
	check(AudioOutputUnitStart(unit), "AudioOutputUnitStart");
}

void iron_a2_shutdown(void) {
	if (initialized) {
		AudioOutputUnitStop(unit);
		initialized = false;
	}
}

#endif
