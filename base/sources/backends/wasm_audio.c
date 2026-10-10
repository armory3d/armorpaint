#ifdef IRON_AUDIO

#include <iron_audio.h>
#include <stdlib.h>

__attribute__((import_module("imports"), import_name("js_audio_init"))) int js_audio_init(float *left, float *right, uint32_t *read, uint32_t *write, int size);

#define BUFFER_SIZE 16384
#define BUFFER_AHEAD 4096

static iron_a2_buffer_t a2_buffer;
static bool             initialized        = false;
static uint32_t         samples_per_second = 44100;
static uint32_t         read_location      = 0;
static uint32_t         write_location     = 0;

void iron_a2_init() {
	if (initialized) {
		return;
	}

	iron_a2_internal_init();
	initialized = true;

	a2_buffer.read_location  = 0;
	a2_buffer.write_location = 0;
	a2_buffer.data_size      = BUFFER_SIZE;
	a2_buffer.channel_count  = 2;
	a2_buffer.channels[0]    = (float *)malloc(a2_buffer.data_size * sizeof(float));
	a2_buffer.channels[1]    = (float *)malloc(a2_buffer.data_size * sizeof(float));

	samples_per_second = js_audio_init(a2_buffer.channels[0], a2_buffer.channels[1], &read_location, &write_location, BUFFER_SIZE);
}

__attribute__((export_name("wasm_audio_update"))) void wasm_audio_update(void) {
	if (!initialized) {
		return;
	}
	uint32_t read     = __atomic_load_n(&read_location, __ATOMIC_SEQ_CST);
	uint32_t buffered = (a2_buffer.write_location + BUFFER_SIZE - read) % BUFFER_SIZE;
	if (buffered < BUFFER_AHEAD && iron_a2_internal_callback(&a2_buffer, BUFFER_AHEAD - buffered)) {
		__atomic_store_n(&write_location, a2_buffer.write_location, __ATOMIC_SEQ_CST);
	}
}

void iron_a2_shutdown() {}

uint32_t iron_a2_samples_per_second(void) {
	return samples_per_second;
}

#endif
