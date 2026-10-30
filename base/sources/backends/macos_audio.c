
#ifdef IRON_AUDIO

#include <CoreAudio/AudioHardware.h>
#include <CoreServices/CoreServices.h>
#include <iron_audio.h>
#include <iron_system.h>
#include <stdio.h>
#include <stdlib.h>

static void affirm(OSStatus err) {
	if (err != kAudioHardwareNoError) {
		iron_error("Error: %i\n", err);
	}
}

static bool                        initialized;
static bool                        soundPlaying;
static AudioDeviceID               device;
static UInt32                      deviceBufferSize;
static UInt32                      size;
static AudioStreamBasicDescription deviceFormat;
static AudioObjectPropertyAddress  address;

static AudioDeviceIOProcID theIOProcID = NULL;

static iron_a2_buffer_t a2_buffer;

static uint32_t samples_per_second = 44100;

uint32_t iron_a2_samples_per_second(void) {
	return samples_per_second;
}

static void copySample(void *buffer) {
	float left_value  = *(float *)&a2_buffer.channels[0][a2_buffer.read_location];
	float right_value = *(float *)&a2_buffer.channels[1][a2_buffer.read_location];
	a2_buffer.read_location += 1;
	if (a2_buffer.read_location >= a2_buffer.data_size) {
		a2_buffer.read_location = 0;
	}
	((float *)buffer)[0] = left_value;
	((float *)buffer)[1] = right_value;
}

static OSStatus appIOProc(AudioDeviceID inDevice, const AudioTimeStamp *inNow, const AudioBufferList *inInputData, const AudioTimeStamp *inInputTime,
                          AudioBufferList *outOutputData, const AudioTimeStamp *inOutputTime, void *userdata) {
	affirm(AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &deviceFormat));
	if (samples_per_second != (int)deviceFormat.mSampleRate) {
		samples_per_second = (int)deviceFormat.mSampleRate;
		iron_a2_internal_sample_rate_callback();
	}
	int num_frames = deviceBufferSize / deviceFormat.mBytesPerFrame;
	iron_a2_internal_callback(&a2_buffer, num_frames);
	float *output = (float *)outOutputData->mBuffers[0].mData;
	for (int i = 0; i < num_frames; ++i) {
		copySample(output);
		output += 2;
	}
	return kAudioHardwareNoError;
}

static bool initialized = false;

void iron_a2_init(void) {
	if (initialized) {
		return;
	}

	iron_a2_internal_init();
	initialized = true;

	a2_buffer.read_location  = 0;
	a2_buffer.write_location = 0;
	a2_buffer.data_size      = 128 * 1024;
	a2_buffer.channel_count  = 2;
	a2_buffer.channels[0]    = (float *)malloc(a2_buffer.data_size * sizeof(float));
	a2_buffer.channels[1]    = (float *)malloc(a2_buffer.data_size * sizeof(float));

	device = kAudioDeviceUnknown;

	initialized = false;

	size              = sizeof(AudioDeviceID);
	address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
	address.mScope    = kAudioObjectPropertyScopeGlobal;
	address.mElement  = kAudioObjectPropertyElementMain;
	affirm(AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device));

	size              = sizeof(UInt32);
	address.mSelector = kAudioDevicePropertyBufferSize;
	address.mScope    = kAudioDevicePropertyScopeOutput;
	affirm(AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &deviceBufferSize));

	iron_log("deviceBufferSize = %i\n", deviceBufferSize);

	size              = sizeof(AudioStreamBasicDescription);
	address.mSelector = kAudioDevicePropertyStreamFormat;
	address.mScope    = kAudioDevicePropertyScopeOutput;

	affirm(AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &deviceFormat));

	if (deviceFormat.mFormatID != kAudioFormatLinearPCM) {
		iron_error("mFormatID !=  kAudioFormatLinearPCM\n");
		return;
	}

	if (!(deviceFormat.mFormatFlags & kLinearPCMFormatFlagIsFloat)) {
		iron_error("Only works with float format.\n");
		return;
	}

	if (samples_per_second != (int)deviceFormat.mSampleRate) {
		samples_per_second = (int)deviceFormat.mSampleRate;
		iron_a2_internal_sample_rate_callback();
	}

	initialized = true;

	iron_log("mSampleRate = %g\n", deviceFormat.mSampleRate);
	iron_log("mFormatFlags = %08X\n", (unsigned int)deviceFormat.mFormatFlags);
	iron_log("mBytesPerPacket = %d\n", (unsigned int)deviceFormat.mBytesPerPacket);
	iron_log("mFramesPerPacket = %d\n", (unsigned int)deviceFormat.mFramesPerPacket);
	iron_log("mChannelsPerFrame = %d\n", (unsigned int)deviceFormat.mChannelsPerFrame);
	iron_log("mBytesPerFrame = %d\n", (unsigned int)deviceFormat.mBytesPerFrame);
	iron_log("mBitsPerChannel = %d\n", (unsigned int)deviceFormat.mBitsPerChannel);

	if (soundPlaying)
		return;

	affirm(AudioDeviceCreateIOProcID(device, appIOProc, NULL, &theIOProcID));
	affirm(AudioDeviceStart(device, theIOProcID));

	soundPlaying = true;
}

void iron_a2_shutdown(void) {
	if (!initialized)
		return;
	if (!soundPlaying)
		return;

	affirm(AudioDeviceStop(device, theIOProcID));
	affirm(AudioDeviceDestroyIOProcID(device, theIOProcID));

	soundPlaying = false;
}

#endif
