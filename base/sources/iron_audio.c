#include "iron_audio.h"

#ifdef IRON_AUDIO

#include "iron_audio.h"
#include "iron_file.h"
#include "iron_math.h"
#include "iron_thread.h"
#include "iron_video.h"

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void     iron_a2_init(void);
void     iron_a2_set_callback(void (*iron_a2_audio_callback)(iron_a2_buffer_t *buffer, uint32_t samples, void *userdata), void *userdata);
uint32_t iron_a2_samples_per_second(void);
void     iron_a2_set_sample_rate_callback(void (*iron_a2_sample_rate_callback)(void *userdata), void *userdata);

struct iron_a1_channel {
	iron_a1_sound_t *sound;
	float            position;
	bool             loop;
	volatile float   volume;
	volatile float   pitch;
};

struct iron_a1_stream_channel {
	iron_a1_sound_stream_t *stream;
	int                     position;
};

struct iron_internal_video_channel {
	struct iron_internal_video_sound_stream *stream;
	int                                      position;
};

static iron_mutex_t mutex;

#define CHANNEL_COUNT 16
static iron_a1_channel_t        channels[CHANNEL_COUNT];
static iron_a1_stream_channel_t streamchannels[CHANNEL_COUNT];
// static iron_internal_video_channel_t videos[CHANNEL_COUNT];

static float sample_linear(int16_t *data, float position) {
	int   pos1    = (int)position;
	int   pos2    = (int)(position + 1);
	float sample1 = data[pos1] / 32767.0f;
	float sample2 = data[pos2] / 32767.0f;
	float a       = position - pos1;
	return sample1 * (1 - a) + sample2 * a;
}

static void iron_a2_on_a1_mix(iron_a2_buffer_t *buffer, uint32_t samples, void *userdata) {
	iron_a1_mix(buffer, samples);
}

void iron_a1_mix(iron_a2_buffer_t *buffer, uint32_t samples) {
	for (uint32_t i = 0; i < samples; ++i) {
		float left_value  = 0.0f;
		float right_value = 0.0f;

		iron_mutex_lock(&mutex);

		for (int i = 0; i < CHANNEL_COUNT; ++i) {
			if (channels[i].sound != NULL) {
				left_value += sample_linear(channels[i].sound->left, channels[i].position) * channels[i].volume * channels[i].sound->volume;
				right_value = fmaxf(fminf(right_value, 1.0f), -1.0f);
				right_value += sample_linear(channels[i].sound->right, channels[i].position) * channels[i].volume * channels[i].sound->volume;
				left_value = fmaxf(fminf(left_value, 1.0f), -1.0f);

				channels[i].position += channels[i].pitch / channels[i].sound->sample_rate_pos;
				// channels[i].position += 2;
				if (channels[i].position + 1 >= channels[i].sound->size) {
					if (channels[i].loop) {
						channels[i].position = 0;
					}
					else {
						channels[i].sound = NULL;
					}
				}
			}
		}

		for (int i = 0; i < CHANNEL_COUNT; ++i) {
			if (streamchannels[i].stream != NULL) {
				float *samples = iron_a1_sound_stream_next_frame(streamchannels[i].stream);
				left_value += samples[0] * iron_a1_sound_stream_volume(streamchannels[i].stream);
				left_value = fmaxf(fminf(left_value, 1.0f), -1.0f);
				right_value += samples[1] * iron_a1_sound_stream_volume(streamchannels[i].stream);
				right_value = fmaxf(fminf(right_value, 1.0f), -1.0f);
				if (iron_a1_sound_stream_ended(streamchannels[i].stream)) {
					streamchannels[i].stream = NULL;
				}
			}
		}

		// for (int i = 0; i < CHANNEL_COUNT; ++i) {
		// 	if (videos[i].stream != NULL) {
		// 		float *samples = iron_internal_video_sound_stream_next_frame(videos[i].stream);
		// 		left_value += samples[0];
		// 		left_value = fmaxf(fminf(left_value, 1.0f), -1.0f);
		// 		right_value += samples[1];
		// 		right_value = fmaxf(fminf(right_value, 1.0f), -1.0f);
		// 		if (iron_internal_video_sound_stream_ended(videos[i].stream)) {
		// 			videos[i].stream = NULL;
		// 		}
		// 	}
		// }

		iron_mutex_unlock(&mutex);

		assert(buffer->channel_count >= 2);
		buffer->channels[0][buffer->write_location] = left_value;
		buffer->channels[1][buffer->write_location] = right_value;

		buffer->write_location += 1;
		if (buffer->write_location >= buffer->data_size) {
			buffer->write_location = 0;
		}
	}
}

static bool a1_initialized = false;

void iron_a1_init(void) {
	if (a1_initialized) {
		return;
	}
	a1_initialized = true;

	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		channels[i].sound    = NULL;
		channels[i].position = 0;
	}
	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		streamchannels[i].stream   = NULL;
		streamchannels[i].position = 0;
	}
	iron_mutex_init(&mutex);

	iron_a2_init();
	iron_a2_set_callback(iron_a2_on_a1_mix, NULL);
}

iron_a1_channel_t *audio_play(iron_a1_sound_t *sound, bool loop) {
	float              pitch   = 1.0;
	bool               unique  = false;
	iron_a1_channel_t *channel = NULL;
	iron_mutex_lock(&mutex);
	bool found = false;
	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		if (channels[i].sound == sound) {
			found = true;
			break;
		}
	}
	if (!found || !unique) {
		for (int i = 0; i < CHANNEL_COUNT; ++i) {
			if (channels[i].sound == NULL) {
				channels[i].sound    = sound;
				channels[i].position = 0;
				channels[i].loop     = loop;
				channels[i].pitch    = pitch;
				channels[i].volume   = sound->volume;
				channel              = &channels[i];
				break;
			}
		}
	}
	iron_mutex_unlock(&mutex);
	return channel;
}

void audio_stop(iron_a1_sound_t *sound) {
	iron_mutex_lock(&mutex);
	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		if (channels[i].sound == sound) {
			channels[i].sound    = NULL;
			channels[i].position = 0;
		}
	}
	iron_mutex_unlock(&mutex);
}

bool audio_is_playing(iron_a1_sound_t *sound) {
	bool playing = false;
	iron_mutex_lock(&mutex);
	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		if (channels[i].sound == sound) {
			playing = true;
			break;
		}
	}
	iron_mutex_unlock(&mutex);
	return playing;
}

void iron_a1_play_sound_stream(iron_a1_sound_stream_t *stream) {
	iron_mutex_lock(&mutex);

	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		if (streamchannels[i].stream == stream) {
			streamchannels[i].stream   = NULL;
			streamchannels[i].position = 0;
			break;
		}
	}

	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		if (streamchannels[i].stream == NULL) {
			streamchannels[i].stream   = stream;
			streamchannels[i].position = 0;
			break;
		}
	}

	iron_mutex_unlock(&mutex);
}

void iron_a1_stop_sound_stream(iron_a1_sound_stream_t *stream) {
	iron_mutex_lock(&mutex);
	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		if (streamchannels[i].stream == stream) {
			streamchannels[i].stream   = NULL;
			streamchannels[i].position = 0;
			break;
		}
	}
	iron_mutex_unlock(&mutex);
}

float iron_a1_channel_get_volume(iron_a1_channel_t *channel) {
	return channel->volume;
}

void iron_a1_channel_set_volume(iron_a1_channel_t *channel, float volume) {
	IRON_ATOMIC_EXCHANGE_FLOAT(&channel->volume, volume);
}

void iron_a1_channel_set_pitch(iron_a1_channel_t *channel, float pitch) {
	IRON_ATOMIC_EXCHANGE_FLOAT(&channel->pitch, pitch);
}

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

struct WaveData {
	uint16_t audioFormat;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t bytesPerSecond;
	uint16_t bitsPerSample;
	uint32_t dataSize;
	uint8_t *data;
};

static void checkFOURCC(uint8_t **data, const char *fourcc) {
	for (int i = 0; i < 4; ++i) {
		++*data;
	}
}

static void readFOURCC(uint8_t **data, char *fourcc) {
	for (int i = 0; i < 4; ++i) {
		fourcc[i] = **data;
		++*data;
	}
	fourcc[4] = 0;
}

static void readChunk(uint8_t **data, struct WaveData *wave) {
	char fourcc[5];
	readFOURCC(data, fourcc);
	uint32_t chunksize = iron_read_u32le(*data);
	*data += 4;
	if (strcmp(fourcc, "fmt ") == 0) {
		wave->audioFormat    = iron_read_u16le(*data + 0);
		wave->numChannels    = iron_read_u16le(*data + 2);
		wave->sampleRate     = iron_read_u32le(*data + 4);
		wave->bytesPerSecond = iron_read_u32le(*data + 8);
		wave->bitsPerSample  = iron_read_u16le(*data + 14);
		*data += chunksize;
	}
	else if (strcmp(fourcc, "data") == 0) {
		wave->dataSize = chunksize;
		wave->data     = (uint8_t *)malloc(chunksize * sizeof(uint8_t));
		memcpy(wave->data, *data, chunksize);
		*data += chunksize;
	}
	else {
		*data += chunksize;
	}
}

static int16_t convert8to16(uint8_t sample) {
	return (sample - 127) << 8;
}

static void splitStereo8(uint8_t *data, int size, int16_t *left, int16_t *right) {
	for (int i = 0; i < size; ++i) {
		left[i]  = convert8to16(data[i * 2 + 0]);
		right[i] = convert8to16(data[i * 2 + 1]);
	}
}

static void splitStereo16(int16_t *data, int size, int16_t *left, int16_t *right) {
	for (int i = 0; i < size; ++i) {
		left[i]  = data[i * 2 + 0];
		right[i] = data[i * 2 + 1];
	}
}

static void splitMono8(uint8_t *data, int size, int16_t *left, int16_t *right) {
	for (int i = 0; i < size; ++i) {
		left[i]  = convert8to16(data[i]);
		right[i] = convert8to16(data[i]);
	}
}

static void splitMono16(int16_t *data, int size, int16_t *left, int16_t *right) {
	for (int i = 0; i < size; ++i) {
		left[i]  = data[i];
		right[i] = data[i];
	}
}

#define MAXIMUM_SOUNDS 128
static iron_a1_sound_t sounds[MAXIMUM_SOUNDS] = {0};

static iron_a1_sound_t *find_sound(void) {
	for (int i = 0; i < MAXIMUM_SOUNDS; ++i) {
		if (!sounds[i].in_use) {
			return &sounds[i];
		}
	}
	return NULL;
}

iron_a1_sound_t *iron_a1_sound_create_from_bytes(uint8_t *filedata, int filedata_size, const char *format) {
	iron_a1_sound_t *sound = find_sound();
	assert(sound != NULL);
	sound->in_use = true;
	sound->volume = 1.0f;
	sound->size   = 0;
	sound->left   = NULL;
	sound->right  = NULL;
	uint8_t *data = NULL;

	if (strncmp(format, ".ogg", 4) == 0) {
		int channels, sample_rate;
		int samples               = stb_vorbis_decode_memory(filedata, (int)filedata_size, &channels, &sample_rate, (short **)&data);
		sound->channel_count      = (uint8_t)channels;
		sound->samples_per_second = (uint32_t)sample_rate;
		sound->size               = samples * 2 * sound->channel_count;
		sound->bits_per_sample    = 16;
	}
	else if (strncmp(format, ".wav", 4) == 0) {
		struct WaveData wave = {0};
		{
			uint8_t *pos = filedata;

			checkFOURCC(&pos, "RIFF");
			uint32_t filesize = iron_read_u32le(pos);
			pos += 4;
			checkFOURCC(&pos, "WAVE");
			while (pos + 8 - filedata < (intptr_t)filesize) {
				readChunk(&pos, &wave);
			}
		}

		sound->bits_per_sample    = (uint8_t)wave.bitsPerSample;
		sound->channel_count      = (uint8_t)wave.numChannels;
		sound->samples_per_second = wave.sampleRate;
		data                      = wave.data;
		sound->size               = wave.dataSize;
	}
	else {
		sound->in_use = false;
		return NULL;
	}

	if (sound->channel_count == 1) {
		if (sound->bits_per_sample == 8) {
			sound->left  = (int16_t *)malloc(sound->size * sizeof(int16_t));
			sound->right = (int16_t *)malloc(sound->size * sizeof(int16_t));
			splitMono8(data, sound->size, sound->left, sound->right);
		}
		else if (sound->bits_per_sample == 16) {
			sound->size /= 2;
			sound->left  = (int16_t *)malloc(sound->size * sizeof(int16_t));
			sound->right = (int16_t *)malloc(sound->size * sizeof(int16_t));
			splitMono16((int16_t *)data, sound->size, sound->left, sound->right);
		}
	}
	else {
		// Left and right channel are in s16 audio stream, alternating.
		if (sound->bits_per_sample == 8) {
			sound->size /= 2;
			sound->left  = (int16_t *)malloc(sound->size * sizeof(int16_t));
			sound->right = (int16_t *)malloc(sound->size * sizeof(int16_t));
			splitStereo8(data, sound->size, sound->left, sound->right);
		}
		else if (sound->bits_per_sample == 16) {
			sound->size /= 4;
			sound->left  = (int16_t *)malloc(sound->size * sizeof(int16_t));
			sound->right = (int16_t *)malloc(sound->size * sizeof(int16_t));
			splitStereo16((int16_t *)data, sound->size, sound->left, sound->right);
		}
	}
	sound->sample_rate_pos = 44100 / (float)sound->samples_per_second;
	free(data);

	return sound;
}

iron_a1_sound_t *iron_a1_sound_create(const char *filename) {
	iron_file_reader_t file;
	if (!iron_file_reader_open(&file, filename, IRON_FILE_TYPE_ASSET)) {
		return NULL;
	}
	int      filedata_size = (int)iron_file_reader_size(&file);
	uint8_t *filedata      = (uint8_t *)malloc(filedata_size);
	iron_file_reader_read(&file, filedata, filedata_size);
	iron_file_reader_close(&file);

	size_t           filename_length = strlen(filename);
	iron_a1_sound_t *sound           = iron_a1_sound_create_from_bytes(filedata, filedata_size, &filename[filename_length - 4]);
	free(filedata);
	return sound;
}

void iron_a1_sound_destroy(iron_a1_sound_t *sound) {
	audio_stop(sound);
	free(sound->left);
	free(sound->right);
	sound->left   = NULL;
	sound->right  = NULL;
	sound->in_use = false;
}

float iron_a1_sound_volume(iron_a1_sound_t *sound) {
	return sound->volume;
}

void iron_a1_sound_set_volume(iron_a1_sound_t *sound, float value) {
	sound->volume = value;
}

static iron_a1_sound_stream_t streams[64];
static int                    nextStream = 0;
static uint8_t                buffer[1024 * 10];
static int                    bufferIndex;

iron_a1_sound_stream_t *iron_a1_sound_stream_create(const char *filename, bool looping) {
	iron_a1_sound_stream_t *stream = &streams[nextStream];
	stream->myLooping              = looping;
	stream->myVolume               = 1;
	stream->rateDecodedHack        = false;
	stream->end                    = false;
	iron_file_reader_t file;
	iron_file_reader_open(&file, filename, IRON_FILE_TYPE_ASSET);
	stream->buffer = &buffer[bufferIndex];
	bufferIndex += (int)iron_file_reader_size(&file);
	uint8_t *filecontent = (uint8_t *)malloc(iron_file_reader_size(&file));
	iron_file_reader_read(&file, filecontent, iron_file_reader_size(&file));
	iron_file_reader_close(&file);
	memcpy(stream->buffer, filecontent, iron_file_reader_size(&file));
	free(filecontent);
	stream->vorbis = stb_vorbis_open_memory(buffer, (int)iron_file_reader_size(&file), NULL, NULL);
	if (stream->vorbis != NULL) {
		stb_vorbis_info info = stb_vorbis_get_info(stream->vorbis);
		stream->chans        = info.channels;
		stream->rate         = info.sample_rate;
	}
	else {
		stream->chans = 2;
		stream->rate  = 22050;
	}
	++nextStream;
	return stream;
}

int iron_a1_sound_stream_channels(iron_a1_sound_stream_t *stream) {
	return stream->chans;
}

int iron_a1_sound_stream_sample_rate(iron_a1_sound_stream_t *stream) {
	return stream->rate;
}

bool iron_a1_sound_stream_looping(iron_a1_sound_stream_t *stream) {
	return stream->myLooping;
}

void iron_a1_sound_stream_set_looping(iron_a1_sound_stream_t *stream, bool loop) {
	stream->myLooping = loop;
}

float iron_a1_sound_stream_volume(iron_a1_sound_stream_t *stream) {
	return stream->myVolume;
}

void iron_a1_sound_stream_set_volume(iron_a1_sound_stream_t *stream, float value) {
	stream->myVolume = value;
}

bool iron_a1_sound_stream_ended(iron_a1_sound_stream_t *stream) {
	return stream->end;
}

float iron_a1_sound_stream_length(iron_a1_sound_stream_t *stream) {
	if (stream->vorbis == NULL)
		return 0;
	return stb_vorbis_stream_length_in_seconds(stream->vorbis);
}

float iron_a1_sound_stream_position(iron_a1_sound_stream_t *stream) {
	if (stream->vorbis == NULL)
		return 0;
	return stb_vorbis_get_sample_offset(stream->vorbis) / stb_vorbis_stream_length_in_samples(stream->vorbis) * iron_a1_sound_stream_length(stream);
}

void iron_a1_sound_stream_reset(iron_a1_sound_stream_t *stream) {
	if (stream->vorbis != NULL)
		stb_vorbis_seek_start(stream->vorbis);
	stream->end             = false;
	stream->rateDecodedHack = false;
}

float *iron_a1_sound_stream_next_frame(iron_a1_sound_stream_t *stream) {
	if (stream->vorbis == NULL) {
		for (int i = 0; i < stream->chans; ++i) {
			stream->samples[i] = 0;
		}
		return stream->samples;
	}
	if (stream->rate == 22050) {
		if (stream->rateDecodedHack) {
			stream->rateDecodedHack = false;
			return stream->samples;
		}
	}

	float  left, right;
	float *samples_array[2] = {&left, &right};
	int    read             = stb_vorbis_get_samples_float(stream->vorbis, stream->chans, samples_array, 1);
	if (read == 0) {
		if (iron_a1_sound_stream_looping(stream)) {
			stb_vorbis_seek_start(stream->vorbis);
			stb_vorbis_get_samples_float(stream->vorbis, stream->chans, samples_array, 1);
		}
		else {
			stream->end = true;
			for (int i = 0; i < stream->chans; ++i) {
				stream->samples[i] = 0;
			}
			return stream->samples;
		}
	}

	stream->samples[0] = samples_array[0][0];
	stream->samples[1] = samples_array[1][0];

	stream->rateDecodedHack = true;
	return stream->samples;
}

static iron_mutex_t mutex;

static void (*a2_callback)(iron_a2_buffer_t *buffer, uint32_t samples, void *userdata) = NULL;
static void *a2_userdata                                                               = NULL;

void iron_a2_set_callback(void (*iron_a2_audio_callback)(iron_a2_buffer_t *buffer, uint32_t samples, void *userdata), void *userdata) {
	iron_mutex_lock(&mutex);
	a2_callback = iron_a2_audio_callback;
	a2_userdata = userdata;
	iron_mutex_unlock(&mutex);
}

static void (*a2_sample_rate_callback)(void *userdata) = NULL;
static void *a2_sample_rate_userdata                   = NULL;

void iron_a2_set_sample_rate_callback(void (*iron_a2_sample_rate_callback)(void *userdata), void *userdata) {
	iron_mutex_lock(&mutex);
	a2_sample_rate_callback = iron_a2_sample_rate_callback;
	a2_sample_rate_userdata = userdata;
	iron_mutex_unlock(&mutex);
}

void iron_a2_internal_init(void) {
	iron_mutex_init(&mutex);
}

bool iron_a2_internal_callback(iron_a2_buffer_t *buffer, int samples) {
	iron_mutex_lock(&mutex);
	bool has_callback = a2_callback != NULL;
	if (has_callback) {
		a2_callback(buffer, samples, a2_userdata);
	}
	iron_mutex_unlock(&mutex);
	return has_callback;
}

void iron_a2_internal_sample_rate_callback(void) {
	iron_mutex_lock(&mutex);
	if (a2_sample_rate_callback != NULL) {
		a2_sample_rate_callback(a2_sample_rate_userdata);
	}
	iron_mutex_unlock(&mutex);
}

#endif
