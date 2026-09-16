#pragma once

#ifdef IRON_AUDIO

#include "iron_global.h"
#include <stdbool.h>
#include <stdint.h>

#define IRON_A2_MAX_CHANNELS 8

typedef struct iron_a1_sound {
	uint8_t  channel_count;
	uint8_t  bits_per_sample;
	uint32_t samples_per_second;
	int16_t *left;
	int16_t *right;
	int      size;
	float    sample_rate_pos;
	float    volume;
	bool     in_use;
} iron_a1_sound_t;

typedef struct iron_a1_sound_stream {
	struct stb_vorbis *vorbis;
	int                chans;
	int                rate;
	bool               myLooping;
	float              myVolume;
	bool               rateDecodedHack;
	bool               end;
	float              samples[2];
	uint8_t           *buffer;
} iron_a1_sound_stream_t;

typedef struct iron_a2_buffer {
	uint8_t  channel_count;
	float   *channels[IRON_A2_MAX_CHANNELS];
	uint32_t data_size;
	uint32_t read_location;
	uint32_t write_location;
} iron_a2_buffer_t;

struct iron_a1_channel;
typedef struct iron_a1_channel iron_a1_channel_t;

struct iron_a1_stream_channel;
typedef struct iron_a1_stream_channel iron_a1_stream_channel_t;

void                    iron_a1_init(void);
iron_a1_channel_t      *audio_play(iron_a1_sound_t *sound, bool loop);
void                    audio_stop(iron_a1_sound_t *sound);
bool                    audio_is_playing(iron_a1_sound_t *sound);
void                    iron_a1_play_sound_stream(iron_a1_sound_stream_t *stream);
void                    iron_a1_stop_sound_stream(iron_a1_sound_stream_t *stream);
float                   iron_a1_channel_get_volume(iron_a1_channel_t *channel);
void                    iron_a1_channel_set_volume(iron_a1_channel_t *channel, float volume);
void                    iron_a1_channel_set_pitch(iron_a1_channel_t *channel, float pitch);
void                    iron_a1_mix(iron_a2_buffer_t *buffer, uint32_t samples);
iron_a1_sound_t        *iron_a1_sound_create(const char *filename);
iron_a1_sound_t        *iron_a1_sound_create_from_bytes(uint8_t *filedata, int filedata_size, const char *format);
void                    iron_a1_sound_destroy(iron_a1_sound_t *sound);
float                   iron_a1_sound_volume(iron_a1_sound_t *sound);
void                    iron_a1_sound_set_volume(iron_a1_sound_t *sound, float value);
iron_a1_sound_stream_t *iron_a1_sound_stream_create(const char *filename, bool looping);
float                  *iron_a1_sound_stream_next_frame(iron_a1_sound_stream_t *stream);
int                     iron_a1_sound_stream_channels(iron_a1_sound_stream_t *stream);
int                     iron_a1_sound_stream_sample_rate(iron_a1_sound_stream_t *stream);
bool                    iron_a1_sound_stream_looping(iron_a1_sound_stream_t *stream);
void                    iron_a1_sound_stream_set_looping(iron_a1_sound_stream_t *stream, bool loop);
bool                    iron_a1_sound_stream_ended(iron_a1_sound_stream_t *stream);
float                   iron_a1_sound_stream_length(iron_a1_sound_stream_t *stream);
float                   iron_a1_sound_stream_position(iron_a1_sound_stream_t *stream);
void                    iron_a1_sound_stream_reset(iron_a1_sound_stream_t *stream);
float                   iron_a1_sound_stream_volume(iron_a1_sound_stream_t *stream);
void                    iron_a1_sound_stream_set_volume(iron_a1_sound_stream_t *stream, float value);
void                    iron_a2_internal_init(void);
void                    iron_a2_shutdown(void);
bool                    iron_a2_internal_callback(iron_a2_buffer_t *buffer, int samples);
void                    iron_a2_internal_sample_rate_callback(void);

// struct iron_internal_video_channel;
// typedef struct iron_internal_video_channel iron_internal_video_channel_t;
// struct iron_internal_video_sound_stream;
// void iron_internal_play_video_sound_stream(struct iron_internal_video_sound_stream *stream);
// void iron_internal_stop_video_sound_stream(struct iron_internal_video_sound_stream *stream);

#endif
