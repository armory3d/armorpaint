/*
 * Iris Main Implementation
 *
 * Main entry point for the Iris inference engine.
 * Ties together all components: tokenizer, text encoder, VAE, transformer, sampling.
 */

#include "iris.h"
#include "iris_kernels.h"
#include "iris_qwen3.h"
#include "iris_safetensors.h"
#ifdef _WIN32
#include "iris_compat.h"
#else
#include <dirent.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef USE_METAL
#include "iris_metal.h"
#endif

/* ========================================================================
 * Forward Declarations for Internal Types
 * ======================================================================== */

typedef struct iris_tokenizer        iris_tokenizer;
typedef struct iris_vae              iris_vae_t;
typedef struct iris_transformer_flux iris_transformer_flux_t;

/* Internal function declarations */
extern iris_tokenizer *iris_tokenizer_load(const char *path);
extern void            iris_tokenizer_free(iris_tokenizer *tok);
extern int            *iris_tokenize(iris_tokenizer *tok, const char *text, int *num_tokens, int max_len);

extern iris_vae_t *iris_vae_load(FILE *f);
extern iris_vae_t *iris_vae_load_safetensors(safetensors_file_t *sf);
extern iris_vae_t *iris_vae_load_safetensors_ex(safetensors_file_t *sf, int z_channels, float scaling_factor, float shift_factor);
extern void        iris_vae_free(iris_vae_t *vae);
extern float      *iris_vae_encode(iris_vae_t *vae, const float *img, int batch, int H, int W, int *out_h, int *out_w);
extern iris_image *iris_vae_decode(iris_vae_t *vae, const float *latent, int batch, int latent_h, int latent_w);
extern float      *iris_image_to_tensor(const iris_image *img);
extern iris_image *iris_image_clone(const iris_image *img);

extern iris_transformer_flux_t *iris_transformer_load_flux(FILE *f);
extern iris_transformer_flux_t *iris_transformer_load_safetensors_flux(const char *model_dir);
extern iris_transformer_flux_t *iris_transformer_load_safetensors_mmap_flux(const char *model_dir);
extern void                     iris_transformer_free_flux(iris_transformer_flux_t *tf);
extern float *iris_transformer_forward_flux(iris_transformer_flux_t *tf, const float *img_latent, int img_h, int img_w, const float *txt_emb, int txt_seq,
                                            float timestep);

extern float *iris_sample_euler_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w, const float *text_emb,
                                     int text_seq, const float *schedule, int num_steps, void (*progress_callback)(int step, int total));
extern float *iris_sample_euler_refs_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w, const float *ref_latent,
                                          int ref_h, int ref_w, int t_offset, const float *text_emb, int text_seq, const float *schedule, int num_steps,
                                          void (*progress_callback)(int step, int total));

/* Multi-reference support */
typedef struct {
	const float *latent;
	int          h, w;
	int          t_offset;
} iris_ref_t;

extern float *iris_sample_euler_multirefs_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w, const iris_ref_t *refs,
                                               int num_refs, const float *text_emb, int text_seq, const float *schedule, int num_steps,
                                               void (*progress_callback)(int step, int total));

/* CFG sampling (for base model) */
extern float *iris_sample_euler_cfg_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w, const float *text_emb_cond,
                                         int text_seq_cond, const float *text_emb_uncond, int text_seq_uncond, float guidance_scale, const float *schedule,
                                         int num_steps, void (*progress_callback)(int step, int total));
extern float *iris_sample_euler_cfg_refs_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w, const float *ref_latent,
                                              int ref_h, int ref_w, int t_offset, const float *text_emb_cond, int text_seq_cond, const float *text_emb_uncond,
                                              int text_seq_uncond, float guidance_scale, const float *schedule, int num_steps,
                                              void (*progress_callback)(int step, int total));
extern float *iris_sample_euler_cfg_multirefs_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w,
                                                   const iris_ref_t *refs, int num_refs, const float *text_emb_cond, int text_seq_cond,
                                                   const float *text_emb_uncond, int text_seq_uncond, float guidance_scale, const float *schedule,
                                                   int num_steps, void (*progress_callback)(int step, int total));

/* Inpainting samplers (masked latent diffusion) */
extern float *iris_sample_euler_inpaint_refs_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w,
                                                  const float *ref_latent, int ref_h, int ref_w, int t_offset, const float *x0, const float *mask,
                                                  const float *text_emb, int text_seq, const float *schedule, int num_steps,
                                                  void (*progress_callback)(int step, int total));
extern float *iris_sample_euler_cfg_inpaint_refs_flux(void *transformer, void *text_encoder, float *z, int batch, int channels, int h, int w,
                                                      const float *ref_latent, int ref_h, int ref_w, int t_offset, const float *x0, const float *mask,
                                                      const float *text_emb_cond, int text_seq_cond, const float *text_emb_uncond, int text_seq_uncond,
                                                      float guidance_scale, const float *schedule, int num_steps,
                                                      void (*progress_callback)(int step, int total));

extern float *iris_schedule_linear(int num_steps);
extern float *iris_schedule_power(int num_steps, float alpha);
extern float *iris_schedule_flux(int num_steps, int image_seq_len);
extern float *iris_init_noise(int batch, int channels, int h, int w, int64_t seed);

/* Return schedule for Flux models based on params.
 * Default is shifted sigmoid; overrides: linear, power. */
static float *iris_selected_schedule(const iris_params *p, int image_seq_len) {
	switch (p->schedule) {
	case IRIS_SCHEDULE_LINEAR:
		return iris_schedule_linear(p->num_steps);
	case IRIS_SCHEDULE_POWER:
		return iris_schedule_power(p->num_steps, p->power_alpha);
	default:
		return iris_schedule_flux(p->num_steps, image_seq_len);
	}
}

/* ========================================================================
 * Text Encoder (Qwen3)
 * ======================================================================== */

/* Qwen3 text encoder is implemented in iris_qwen3.c */

/* ========================================================================
 * Main Context Structure
 * ======================================================================== */

struct iris_ctx {
	/* Components */
	iris_tokenizer          *tokenizer;
	qwen3_encoder_t         *qwen3_encoder;
	iris_vae_t              *vae;
	iris_transformer_flux_t *transformer;

	/* Configuration */
	int   max_width;
	int   max_height;
	int   default_steps;
	float default_guidance;
	int   is_distilled;      /* 1 = distilled (4-step), 0 = base (50-step CFG) */
	int   text_dim;          /* Text embedding dimension (7680 for 4B, varies for 9B) */
	int   is_non_commercial; /* 1 if model has non-commercial license (9B) */
	int   num_heads;         /* Transformer attention heads (24 for 4B, 32 for 9B) */

	/* VAE config (read from vae/config.json) */
	int   vae_z_channels; /* Latent channels before patchify (32 for Flux) */
	float vae_scaling;    /* Scaling factor (0 = use batch norm) */
	float vae_shift;      /* Shift factor (0 = use batch norm) */

	/* Model info */
	char model_name[64];
	char model_version[32];
	char model_dir[512]; /* For reloading text encoder if released */

	/* Memory mode */
	int use_mmap; /* Use mmap for text encoder (lower memory, slower) */
};

/* Global error message */
static char g_error_msg[256] = {0};

const char *iris_get_error(void) {
	return g_error_msg;
}

void iris_set_step_image_callback(iris_ctx *ctx, iris_step_image_cb_t callback) {
	iris_step_image_callback = callback;
	iris_step_image_vae      = callback ? ctx->vae : NULL;
}

static void set_error(const char *msg) {
	strncpy(g_error_msg, msg, sizeof(g_error_msg) - 1);
	g_error_msg[sizeof(g_error_msg) - 1] = '\0';
}

/* ========================================================================
 * Model Loading from HuggingFace-style directory with safetensors files
 * ======================================================================== */

static int file_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

/* Main model loading entry point. Parses model_index.json to auto-detect
 * the model type (distilled vs base), then reads
 * transformer/config.json and vae/config.json for architecture parameters
 * (hidden dim, heads, layers, etc.). Only the VAE (~300MB) is loaded
 * eagerly; the text encoder and transformer are deferred to generation
 * time so they can be swapped in/out on memory-constrained systems. */
iris_ctx *iris_load_dir(const char *model_dir) {
	char path[1024];

	iris_ctx *ctx = calloc(1, sizeof(iris_ctx));
	if (!ctx) {
		set_error("Out of memory");
		return NULL;
	}

	/* Set defaults - max 2048x2048 (requires ~18GB VAE work buffers) */
	ctx->max_width  = IRIS_VAE_MAX_DIM;
	ctx->max_height = IRIS_VAE_MAX_DIM;
	strncpy(ctx->model_version, "1.0", sizeof(ctx->model_version) - 1);
	strncpy(ctx->model_dir, model_dir, sizeof(ctx->model_dir) - 1);

	/* Autodetect model type from model_index.json.
	 * Distilled model has "is_distilled": true, base model does not. */
	ctx->is_distilled = 1; /* Default to distilled */
	snprintf(path, sizeof(path), "%s/model_index.json", model_dir);
	if (file_exists(path)) {
		FILE *f = fopen(path, "r");
		if (f) {
			char   buf[4096];
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			buf[n]   = '\0';
			fclose(f);
			/* If "is_distilled" is present and true, it's distilled.
			 * If absent, it's the base model. */
			if (!strstr(buf, "\"is_distilled\": true") && !strstr(buf, "\"is_distilled\":true")) {
				ctx->is_distilled = 0;
			}
		}
	}

	/* Read transformer/config.json to determine model size and architecture. */
	int num_heads = 24;   /* default 4B */
	ctx->text_dim = 7680; /* default 4B: 3 * 2560 */
	snprintf(path, sizeof(path), "%s/transformer/config.json", model_dir);
	if (file_exists(path)) {
		FILE *f = fopen(path, "r");
		if (f) {
			char   buf[8192];
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			buf[n]   = '\0';
			fclose(f);
			char *p;
			if ((p = strstr(buf, "\"num_attention_heads\""))) {
				if ((p = strchr(p, ':')))
					num_heads = atoi(p + 1);
			}
			int joint_dim = 0;
			if ((p = strstr(buf, "\"joint_attention_dim\""))) {
				if ((p = strchr(p, ':')))
					joint_dim = atoi(p + 1);
			}
			if (joint_dim > 0)
				ctx->text_dim = joint_dim;
		}
	}

	/* Read vae/config.json for latent scaling/shift factors */
	ctx->vae_z_channels = IRIS_VAE_Z_CHANNELS; /* default: 32 */
	ctx->vae_scaling    = 0.0f;
	ctx->vae_shift      = 0.0f;
	snprintf(path, sizeof(path), "%s/vae/config.json", model_dir);
	if (file_exists(path)) {
		FILE *f = fopen(path, "r");
		if (f) {
			char   buf[4096];
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			buf[n]   = '\0';
			fclose(f);
			char *p;
			if ((p = strstr(buf, "\"latent_channels\""))) {
				char *colon = strchr(p, ':');
				if (colon) {
					int lc = atoi(colon + 1);
					if (lc > 0)
						ctx->vae_z_channels = lc;
				}
			}
			if ((p = strstr(buf, "\"scaling_factor\""))) {
				char *colon = strchr(p, ':');
				if (colon)
					ctx->vae_scaling = atof(colon + 1);
			}
			if ((p = strstr(buf, "\"shift_factor\""))) {
				char *colon = strchr(p, ':');
				if (colon)
					ctx->vae_shift = atof(colon + 1);
			}
		}
	}

	/* Determine model variant name based on architecture. */
	{
		int         hidden_size = num_heads * 128; /* head_dim is always 128 */
		const char *size_label  = (hidden_size > 3072) ? "9B" : "4B";
		ctx->is_non_commercial  = (hidden_size > 3072) ? 1 : 0;
		ctx->num_heads          = num_heads;

		if (ctx->is_distilled) {
			ctx->default_steps    = 4;
			ctx->default_guidance = 1.0f;
			snprintf(ctx->model_name, sizeof(ctx->model_name), "FLUX.2-klein-%s", size_label);
		}
		else {
			ctx->default_steps    = 50;
			ctx->default_guidance = 4.0f;
			snprintf(ctx->model_name, sizeof(ctx->model_name), "FLUX.2-klein-base-%s", size_label);
		}
	}

	/* Load VAE only at startup (~300MB).
	 * Transformer and text encoder are loaded on-demand during generation
	 * to support systems with limited RAM (e.g., 16GB). */
	snprintf(path, sizeof(path), "%s/vae.safetensors", model_dir);
	if (file_exists(path)) {
		safetensors_file_t *sf = safetensors_open(path);
		if (sf) {
			ctx->vae = iris_vae_load_safetensors_ex(sf, ctx->vae_z_channels, ctx->vae_scaling, ctx->vae_shift);
			safetensors_close(sf);
		}
	}

	/* Verify VAE is loaded */
	if (!ctx->vae) {
		set_error("Failed to load VAE - cannot generate images");
		iris_free(ctx);
		return NULL;
	}

	/* Verify a transformer is present (will be loaded on-demand). config.json
	 * is optional -- when absent, the defaults above (24 heads, text_dim 7680)
	 * describe the 4B model. We only need the weights themselves: the GGUF in
	 * the model root, a sharded safetensors index, or a single safetensors file. */
	{
		char gguf_q8[1024], gguf_bf16[1024], idx[1024], st[1024];
		snprintf(gguf_q8, sizeof(gguf_q8), "%s/flux-2-klein-4b-Q8_0.gguf", model_dir);
		snprintf(gguf_bf16, sizeof(gguf_bf16), "%s/flux-2-klein-4b-BF16.gguf", model_dir);
		snprintf(idx, sizeof(idx), "%s/transformer/diffusion_pytorch_model.safetensors.index.json", model_dir);
		snprintf(st, sizeof(st), "%s/transformer/diffusion_pytorch_model.safetensors", model_dir);
		if (!file_exists(gguf_q8) && !file_exists(gguf_bf16) && !file_exists(idx) && !file_exists(st)) {
			set_error("Transformer model not found (no gguf in model root or safetensors in transformer/)");
			iris_free(ctx);
			return NULL;
		}
	}
	/* Text encoder and transformer are loaded on-demand to reduce peak memory. */

	/* Initialize RNG */
	iris_rng_seed((uint64_t)time(NULL));

	return ctx;
}

void iris_free(iris_ctx *ctx) {
	if (!ctx)
		return;

	iris_tokenizer_free(ctx->tokenizer);
	qwen3_encoder_free(ctx->qwen3_encoder);
	iris_vae_free(ctx->vae);
	iris_transformer_free_flux(ctx->transformer);

	free(ctx);
}

void iris_set_mmap(iris_ctx *ctx, int enable) {
	if (ctx)
		ctx->use_mmap = enable;
}

int iris_is_distilled(iris_ctx *ctx) {
	return ctx ? ctx->is_distilled : 1;
}

void iris_set_base_mode(iris_ctx *ctx) {
	if (!ctx)
		return;
	ctx->is_distilled      = 0;
	ctx->default_steps     = 50;
	ctx->default_guidance  = 4.0f;
	const char *size_label = ctx->is_non_commercial ? "9B" : "4B";
	snprintf(ctx->model_name, sizeof(ctx->model_name), "FLUX.2-klein-base-%s", size_label);
}

/* Free the Qwen3 text encoder (~4-8GB) to make room for the transformer.
 * The encoder and transformer can't coexist in memory on most machines,
 * so this is called after text encoding and before denoising. On Metal,
 * also resets all GPU state (weight caches, pools) to avoid stale data
 * when the transformer loads into the same memory regions. */
void iris_release_text_encoder(iris_ctx *ctx) {
	if (!ctx || !ctx->qwen3_encoder)
		return;

	qwen3_encoder_free(ctx->qwen3_encoder);
	ctx->qwen3_encoder = NULL;

#ifdef USE_METAL
	/* Reset all GPU state to ensure clean slate for transformer.
	 * This clears weight caches, activation pools, and pending commands. */
	iris_metal_reset();
#endif
}

#ifdef USE_VULKAN
extern void iris_vulkan_release_weight_cache(void);
#endif

/* Free the transformer (~4-8GB weights) and reclaim its GPU buffers before VAE
 * decode. Iris does one-shot CLI generation, so the transformer is not needed
 * once sampling has produced the latent; releasing it here keeps the peak at
 * the decode stage from including both the transformer and the VAE work set
 * (which alone is ~6GB at 1024x1024). The next generation reloads it, which is
 * acceptable for one-shot use. On Metal the weights are unified-memory GPU
 * mirrors freed via the reset; on Vulkan they are VRAM-resident and freed via
 * the weight-cache release (the VAE re-uploads its own weights during decode). */
void iris_release_transformer(iris_ctx *ctx) {
	if (!ctx || !ctx->transformer)
		return;

	iris_transformer_free_flux(ctx->transformer);
	ctx->transformer = NULL;

#if defined(USE_METAL)
	iris_metal_reset();
#elif defined(USE_VULKAN)
	iris_vulkan_release_weight_cache();
#endif
}

/* Lazy-load the Flux transformer from safetensors files. Deferred to
 * generation time because the text encoder must be freed first -- both
 * are too large to fit in memory simultaneously. Once loaded, the
 * transformer persists across generations (no reload per image). */
static int iris_load_transformer_if_needed(iris_ctx *ctx) {
	if (ctx->transformer)
		return 1; /* Already loaded */

	if (iris_phase_callback)
		iris_phase_callback("Loading FLUX.2 transformer", 0);
	if (ctx->use_mmap) {
		ctx->transformer = iris_transformer_load_safetensors_mmap_flux(ctx->model_dir);
	}
	else {
		ctx->transformer = iris_transformer_load_safetensors_flux(ctx->model_dir);
	}
	if (iris_phase_callback)
		iris_phase_callback("Loading FLUX.2 transformer", 1);

	if (!ctx->transformer) {
		set_error("Failed to load transformer");
		return 0;
	}
	return 1;
}

/* Get transformer for debugging */
void *iris_get_transformer(iris_ctx *ctx) {
	if (!ctx)
		return NULL;
	return ctx->transformer;
}

/* ========================================================================
 * Text Encoding
 * ======================================================================== */

/* Run the prompt through Qwen3 to produce text embeddings. For Flux models,
 * hidden states from layers 8, 17, 26 are concatenated to form [512, text_dim]. */
float *iris_encode_text(iris_ctx *ctx, const char *prompt, int *out_seq_len) {
	if (!ctx || !prompt) {
		*out_seq_len = 0;
		return NULL;
	}

	/* Load encoder if not already loaded */
	if (!ctx->qwen3_encoder && ctx->model_dir[0]) {
		if (iris_phase_callback)
			iris_phase_callback("Loading Qwen3 encoder", 0);
		ctx->qwen3_encoder = qwen3_encoder_load(ctx->model_dir, ctx->use_mmap);
		if (iris_phase_callback)
			iris_phase_callback("Loading Qwen3 encoder", 1);
		if (!ctx->qwen3_encoder) {
			fprintf(stderr, "Warning: Failed to load Qwen3 text encoder\n");
		}
	}

	if (!ctx->qwen3_encoder) {
		/* Flux fallback: return zero padded embeddings. */
		*out_seq_len = QWEN3_MAX_SEQ_LEN;
		return (float *)calloc(QWEN3_MAX_SEQ_LEN * ctx->text_dim, sizeof(float));
	}

	/* Encode text using Qwen3 */
	if (iris_phase_callback)
		iris_phase_callback("encoding text", 0);

	int    num_real_tokens = 0;
	float *embeddings      = qwen3_encode_text_ex(ctx->qwen3_encoder, prompt, &num_real_tokens);
	if (iris_phase_callback)
		iris_phase_callback("encoding text", 1);

	/* Flux: return full padded sequence (512) */
	*out_seq_len = QWEN3_MAX_SEQ_LEN;
	return embeddings;
}

/* ========================================================================
 * Image Generation
 * ======================================================================== */

/* Main text-to-image entry point. For Flux: encodes text via Qwen3, frees the
 * encoder, loads the transformer, initializes Gaussian noise in latent space,
 * then runs Euler ODE denoising (4 steps distilled / 50 steps base with CFG)
 * followed by VAE decode. For base models, an empty-prompt encoding is also
 * produced for Classifier-Free Guidance (two sequential transformer passes per
 * step). */
iris_image *iris_generate(iris_ctx *ctx, const char *prompt, const iris_params *params) {
	if (!ctx || !prompt) {
		set_error("Invalid context or prompt");
		return NULL;
	}

	/* Use defaults if params is NULL */
	iris_params p;
	if (params) {
		p = *params;
	}
	else {
		p = (iris_params)IRIS_PARAMS_DEFAULT;
	}

	/* Seamless / tileable generation: enable circular conv padding */
	iris_circular = p.circular;

	/* Validate dimensions */
	if (p.width <= 0)
		p.width = IRIS_DEFAULT_WIDTH;
	if (p.height <= 0)
		p.height = IRIS_DEFAULT_HEIGHT;
	if (p.num_steps <= 0)
		p.num_steps = ctx->default_steps;
	float guidance = (p.guidance > 0) ? p.guidance : ctx->default_guidance;

	/* Ensure dimensions are divisible by 16 */
	p.width  = (p.width / 16) * 16;
	p.height = (p.height / 16) * 16;
	if (p.width < 64)
		p.width = 64;
	if (p.height < 64)
		p.height = 64;
	if (p.width > IRIS_VAE_MAX_DIM || p.height > IRIS_VAE_MAX_DIM) {
		set_error("Image dimensions exceed maximum (2048x2048)");
		return NULL;
	}

	/* Encode text (and unconditioned text for CFG in base model) */
	int    text_seq;
	float *text_emb = iris_encode_text(ctx, prompt, &text_seq);
	if (!text_emb) {
		set_error("Failed to encode prompt");
		return NULL;
	}

	float *text_emb_uncond = NULL;
	int    text_seq_uncond = 0;
	if (!ctx->is_distilled) {
		text_emb_uncond = iris_encode_text(ctx, "", &text_seq_uncond);
		if (!text_emb_uncond) {
			free(text_emb);
			set_error("Failed to encode empty prompt for CFG");
			return NULL;
		}
	}

	/* Release text encoder to free ~8GB before loading transformer */
	iris_release_text_encoder(ctx);

	/* Load transformer now (after text encoder is freed to reduce peak memory) */
	if (!iris_load_transformer_if_needed(ctx)) {
		free(text_emb);
		free(text_emb_uncond);
		return NULL;
	}

	/* Compute latent dimensions */
	int latent_h      = p.height / 16;
	int latent_w      = p.width / 16;
	int image_seq_len = latent_h * latent_w;

	/* Initialize noise */
	int64_t seed = (p.seed < 0) ? (int64_t)time(NULL) : p.seed;
	float  *z    = iris_init_noise(1, IRIS_LATENT_CHANNELS, latent_h, latent_w, seed);

	/* Get schedule */
	float *schedule = iris_selected_schedule(&p, image_seq_len);

	/* Sample */
	float *latent;
	if (ctx->is_distilled) {
		latent = iris_sample_euler_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, latent_h, latent_w, text_emb, text_seq, schedule,
		                                p.num_steps, NULL);
	}
	else {
		latent = iris_sample_euler_cfg_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, latent_h, latent_w, text_emb, text_seq,
		                                    text_emb_uncond, text_seq_uncond, guidance, schedule, p.num_steps, NULL);
	}

	free(z);
	free(schedule);
	free(text_emb);
	free(text_emb_uncond);

	if (!latent) {
		set_error("Sampling failed");
		return NULL;
	}

	/* Decode latent to image */
	iris_image *img = NULL;
	if (ctx->vae) {
		/* One-shot generation: free the transformer (and its GPU buffers)
		 * before decode so the VAE work set doesn't share peak memory. */
		iris_release_transformer(ctx);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 0);
		img = iris_vae_decode(ctx->vae, latent, 1, latent_h, latent_w);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 1);
	}

	free(latent);

	return img;
}

/* ========================================================================
 * Attention Memory Budget
 * ======================================================================== */

/* Cap on the worst-case attention matrix (num_heads * seq * seq * 4B).
 *
 * The Metal path materializes the full [seq, seq] score matrix per head, so it
 * is bound by the 4 GB MPSTemporaryNDArray hard limit; exceeding it forces
 * references to be shrunk (e.g. a 1024x1024 reference drops to 720x720).
 *
 * The Vulkan resident path uses flash-style attention (iris_vulkan_res_attn.comp,
 * online softmax) that never materializes that matrix, so the 4 GB cap does not
 * apply. Use a generous ceiling there so full-resolution references (up to the
 * VAE max) are kept; real VRAM use scales with seq*hidden, not seq*seq. */
#ifdef USE_VULKAN
#define ATTENTION_MAX_BYTES ((size_t)64ULL << 30)
#else
#define ATTENTION_MAX_BYTES ((size_t)4ULL << 30)
#endif

/* Compute worst-case attention matrix size in bytes.
 * All image dimensions are in pixels (multiples of 16).
 * ref_dims is [h0, w0, h1, w1, ...] in pixels. */
static size_t attention_bytes(int num_heads, int out_h, int out_w, const int *ref_dims, int num_refs, int txt_seq) {
	size_t total_seq = (size_t)(out_h / 16) * (out_w / 16);
	for (int i = 0; i < num_refs; i++)
		total_seq += (size_t)(ref_dims[i * 2] / 16) * (ref_dims[i * 2 + 1] / 16);
	total_seq += txt_seq;
	return (size_t)num_heads * total_seq * total_seq * sizeof(float);
}

/* Shrink reference pixel dimensions so attention fits under 4 GB.
 * ref_dims: [h0, w0, h1, w1, ...] in pixels, modified in-place.
 * Returns 1 if any reference was shrunk, 0 if already fits. */
static int fit_refs_for_attention(int num_heads, int out_h, int out_w, int *ref_dims, int num_refs, int txt_seq) {
	if (attention_bytes(num_heads, out_h, out_w, ref_dims, num_refs, txt_seq) <= ATTENTION_MAX_BYTES)
		return 0;

	int shrunk = 0;
	for (;;) {
		/* Find reference with the most latent tokens. */
		int    best     = -1;
		size_t best_tok = 0;
		for (int i = 0; i < num_refs; i++) {
			size_t tok = (size_t)(ref_dims[i * 2] / 16) * (ref_dims[i * 2 + 1] / 16);
			if (tok > best_tok) {
				best_tok = tok;
				best     = i;
			}
		}
		if (best < 0 || best_tok <= 1)
			break; /* can't shrink further */

		/* Scale both dimensions by 0.9, round down to multiple of 16. */
		int h = (int)(ref_dims[best * 2] * 0.9f) / 16 * 16;
		int w = (int)(ref_dims[best * 2 + 1] * 0.9f) / 16 * 16;
		if (h < 16)
			h = 16;
		if (w < 16)
			w = 16;

		/* No progress. */
		if (h == ref_dims[best * 2] && w == ref_dims[best * 2 + 1])
			break;

		ref_dims[best * 2]     = h;
		ref_dims[best * 2 + 1] = w;
		shrunk                 = 1;

		if (attention_bytes(num_heads, out_h, out_w, ref_dims, num_refs, txt_seq) <= ATTENTION_MAX_BYTES)
			break;
	}
	return shrunk;
}

/* ========================================================================
 * Image-to-Image Generation
 * ======================================================================== */

/* Image-to-image generation via in-context conditioning. The reference image
 * is VAE-encoded into latent tokens with a RoPE T offset (T=10), while the
 * target starts from pure noise (T=0). Both are concatenated and fed to the
 * transformer, which attends to reference tokens via joint attention -- this
 * is fundamentally different from traditional img2img that adds noise to the
 * encoded image. References are dynamically resized if the resulting attention
 * matrix would exceed the 4GB MPS memory limit. */
iris_image *iris_img2img(iris_ctx *ctx, const char *prompt, const iris_image *input, const iris_params *params) {
	if (!ctx || !prompt || !input) {
		set_error("Invalid parameters");
		return NULL;
	}
	iris_params p;
	if (params) {
		p = *params;
	}
	else {
		p = (iris_params)IRIS_PARAMS_DEFAULT;
	}

	/* Seamless / tileable generation: enable circular conv padding */
	iris_circular = p.circular;

	/* Use input image dimensions if not specified */
	if (p.width <= 0)
		p.width = input->width;
	if (p.height <= 0)
		p.height = input->height;

	/* Clamp to VAE max dimensions, preserving aspect ratio */
	if (p.width > IRIS_VAE_MAX_DIM || p.height > IRIS_VAE_MAX_DIM) {
		float scale = (float)IRIS_VAE_MAX_DIM / (p.width > p.height ? p.width : p.height);
		p.width     = (int)(p.width * scale);
		p.height    = (int)(p.height * scale);
	}

	/* Ensure divisible by 16 */
	p.width  = (p.width / 16) * 16;
	p.height = (p.height / 16) * 16;

	/* Check attention memory budget */
	int ref_w = p.width, ref_h = p.height;
	{
		int ref_dims[2] = {p.height, p.width};
		if (fit_refs_for_attention(ctx->num_heads, p.height, p.width, ref_dims, 1, IRIS_MAX_SEQ_LEN)) {
			fprintf(stderr,
			        "Note: reference image resized from %dx%d to %dx%d "
			        "(GPU attention memory limit)\n",
			        p.width, p.height, ref_dims[1], ref_dims[0]);
			ref_h = ref_dims[0];
			ref_w = ref_dims[1];
		}
	}

	/* Resize input if needed */
	iris_image       *resized    = NULL;
	const iris_image *img_to_use = input;
	if (input->width != ref_w || input->height != ref_h) {
		resized = iris_image_resize(input, ref_w, ref_h);
		if (!resized) {
			set_error("Failed to resize input image");
			return NULL;
		}
		img_to_use = resized;
	}

	/* Resolve steps and guidance */
	if (p.num_steps <= 0)
		p.num_steps = ctx->default_steps;
	float guidance = (p.guidance > 0) ? p.guidance : ctx->default_guidance;

	/* Encode text */
	int    text_seq;
	float *text_emb = iris_encode_text(ctx, prompt, &text_seq);
	if (!text_emb) {
		if (resized)
			iris_image_free(resized);
		set_error("Failed to encode prompt");
		return NULL;
	}

	float *text_emb_uncond = NULL;
	int    text_seq_uncond = 0;
	if (!ctx->is_distilled) {
		text_emb_uncond = iris_encode_text(ctx, "", &text_seq_uncond);
		if (!text_emb_uncond) {
			free(text_emb);
			if (resized)
				iris_image_free(resized);
			set_error("Failed to encode empty prompt for CFG");
			return NULL;
		}
	}

	/* Release text encoder to free ~8GB before loading transformer */
	iris_release_text_encoder(ctx);

	/* Load transformer now (after text encoder is freed to reduce peak memory) */
	if (!iris_load_transformer_if_needed(ctx)) {
		free(text_emb);
		free(text_emb_uncond);
		if (resized)
			iris_image_free(resized);
		return NULL;
	}

	/* Encode image to latent */
	if (iris_phase_callback)
		iris_phase_callback("encoding reference image", 0);
	float *img_tensor = iris_image_to_tensor(img_to_use);
	if (resized)
		iris_image_free(resized);

	int    latent_h, latent_w;
	float *img_latent = NULL;

	if (ctx->vae) {
		img_latent = iris_vae_encode(ctx->vae, img_tensor, 1, ref_h, ref_w, &latent_h, &latent_w);
	}
	else {
		/* Placeholder if no VAE */
		latent_h   = ref_h / 16;
		latent_w   = ref_w / 16;
		img_latent = (float *)calloc(IRIS_LATENT_CHANNELS * latent_h * latent_w, sizeof(float));
	}

	free(img_tensor);
	if (iris_phase_callback)
		iris_phase_callback("encoding reference image", 1);

	if (!img_latent) {
		free(text_emb);
		free(text_emb_uncond);
		set_error("Failed to encode image");
		return NULL;
	}

	/*
	 * FLUX.2 img2img uses in-context conditioning:
	 * - Reference image is encoded to latent with T offset in RoPE (T=10)
	 * - Target image starts from pure noise (T=0)
	 * - Both are concatenated as tokens in the transformer
	 * - Model attends to reference via joint attention
	 * - Only target tokens are output
	 *
	 * This is fundamentally different from traditional img2img that adds
	 * noise directly to the encoded image.
	 */
	int num_steps     = p.num_steps;
	int out_lat_h     = p.height / 16;
	int out_lat_w     = p.width / 16;
	int image_seq_len = out_lat_h * out_lat_w; /* For schedule calculation */

	/* Get schedule */
	float *schedule = iris_selected_schedule(&p, image_seq_len);

	/* Initialize target latent with pure noise */
	int64_t seed = (p.seed < 0) ? (int64_t)time(NULL) : p.seed;
	float  *z    = iris_init_noise(1, IRIS_LATENT_CHANNELS, out_lat_h, out_lat_w, seed);

	/* Reference image latent is img_latent, with T offset = 10 */
	int t_offset = 10;

	/* Sample using in-context conditioning */
	float *latent;
	if (ctx->is_distilled) {
		latent = iris_sample_euler_refs_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, out_lat_h, out_lat_w, img_latent, latent_h,
		                                     latent_w, t_offset, text_emb, text_seq, schedule, num_steps, NULL);
	}
	else {
		latent = iris_sample_euler_cfg_refs_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, out_lat_h, out_lat_w, img_latent, latent_h,
		                                         latent_w, t_offset, text_emb, text_seq, text_emb_uncond, text_seq_uncond, guidance, schedule, num_steps, NULL);
	}

	free(z);
	free(img_latent);
	free(schedule);
	free(text_emb);
	free(text_emb_uncond);

	if (!latent) {
		set_error("Sampling failed");
		return NULL;
	}

	/* Decode */
	iris_image *result = NULL;
	if (ctx->vae) {
		/* One-shot generation: free the transformer (and its GPU buffers)
		 * before decode so the VAE work set doesn't share peak memory. */
		iris_release_transformer(ctx);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 0);
		result = iris_vae_decode(ctx->vae, latent, 1, out_lat_h, out_lat_w);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 1);
	}

	free(latent);
	return result;
}

/* ========================================================================
 * Strength-based Image-to-Image (traditional denoising)
 * ======================================================================== */

/* Traditional img2img variation dial. The input image is VAE-encoded, then
 * partially re-noised to a starting timestep chosen by `strength` (0 = keep the
 * input, 1 = full noise, equivalent to txt2img), and denoised from there with
 * text guidance. Lower strength stays closer to the original; higher strength
 * permits larger changes. Unlike the in-context iris_img2img(), the reference is
 * NOT fed through attention -- this is the classic noise-and-denoise approach,
 * which gives a continuous "how close to the original" control. */
iris_image *iris_img2img_strength(iris_ctx *ctx, const char *prompt, const iris_image *input, float strength, const iris_params *params) {
	if (!ctx || !prompt || !input) {
		set_error("Invalid parameters");
		return NULL;
	}
	iris_params p = params ? *params : (iris_params)IRIS_PARAMS_DEFAULT;

	/* Seamless / tileable generation: enable circular conv padding */
	iris_circular = p.circular;

	/* Output matches the input dimensions (16-aligned). */
	p.width  = input->width;
	p.height = input->height;
	if (p.width > IRIS_VAE_MAX_DIM || p.height > IRIS_VAE_MAX_DIM) {
		float scale = (float)IRIS_VAE_MAX_DIM / (p.width > p.height ? p.width : p.height);
		p.width     = (int)(p.width * scale);
		p.height    = (int)(p.height * scale);
	}
	p.width  = (p.width / 16) * 16;
	p.height = (p.height / 16) * 16;
	if (p.width < 64)
		p.width = 64;
	if (p.height < 64)
		p.height = 64;

	if (strength < 0.0f)
		strength = 0.0f;
	if (strength > 1.0f)
		strength = 1.0f;

	if (p.num_steps <= 0)
		p.num_steps = ctx->default_steps;
	float guidance = (p.guidance > 0) ? p.guidance : ctx->default_guidance;

	/* Resize input if needed */
	iris_image       *resized    = NULL;
	const iris_image *img_to_use = input;
	if (input->width != p.width || input->height != p.height) {
		resized = iris_image_resize(input, p.width, p.height);
		if (!resized) {
			set_error("Failed to resize input image");
			return NULL;
		}
		img_to_use = resized;
	}

	/* Encode text (+ unconditioned for CFG in base model) */
	int    text_seq;
	float *text_emb = iris_encode_text(ctx, prompt, &text_seq);
	if (!text_emb) {
		if (resized)
			iris_image_free(resized);
		set_error("Failed to encode prompt");
		return NULL;
	}
	float *text_emb_uncond = NULL;
	int    text_seq_uncond = 0;
	if (!ctx->is_distilled) {
		text_emb_uncond = iris_encode_text(ctx, "", &text_seq_uncond);
		if (!text_emb_uncond) {
			free(text_emb);
			if (resized)
				iris_image_free(resized);
			set_error("Failed to encode empty prompt for CFG");
			return NULL;
		}
	}

	/* Release text encoder to free ~8GB before loading transformer */
	iris_release_text_encoder(ctx);
	if (!iris_load_transformer_if_needed(ctx)) {
		free(text_emb);
		free(text_emb_uncond);
		if (resized)
			iris_image_free(resized);
		return NULL;
	}

	/* Encode image to latent (x0) */
	if (iris_phase_callback)
		iris_phase_callback("encoding input image", 0);
	float *img_tensor = iris_image_to_tensor(img_to_use);
	if (resized)
		iris_image_free(resized);
	int    latent_h, latent_w;
	float *x0 = NULL;
	if (ctx->vae) {
		x0 = iris_vae_encode(ctx->vae, img_tensor, 1, p.height, p.width, &latent_h, &latent_w);
	}
	else {
		latent_h = p.height / 16;
		latent_w = p.width / 16;
		x0       = (float *)calloc(IRIS_LATENT_CHANNELS * latent_h * latent_w, sizeof(float));
	}
	free(img_tensor);
	if (iris_phase_callback)
		iris_phase_callback("encoding input image", 1);
	if (!x0) {
		free(text_emb);
		free(text_emb_uncond);
		set_error("Failed to encode image");
		return NULL;
	}

	int latent_size   = IRIS_LATENT_CHANNELS * latent_h * latent_w;
	int image_seq_len = latent_h * latent_w;

	/* Schedule, truncated to start partway through based on strength. */
	float *schedule     = iris_selected_schedule(&p, image_seq_len);
	int    actual_steps = (int)ceilf((float)p.num_steps * strength);
	if (actual_steps < 1)
		actual_steps = 1;
	if (actual_steps > p.num_steps)
		actual_steps = p.num_steps;
	int   skip    = p.num_steps - actual_steps;
	float t_start = schedule[skip];

	/* Noise the encoded image to t_start: z = (1 - t)*x0 + t*noise */
	int64_t seed  = (p.seed < 0) ? (int64_t)time(NULL) : p.seed;
	float  *noise = iris_init_noise(1, IRIS_LATENT_CHANNELS, latent_h, latent_w, seed);
	float  *z     = (float *)malloc(latent_size * sizeof(float));
	for (int i = 0; i < latent_size; i++)
		z[i] = (1.0f - t_start) * x0[i] + t_start * noise[i];
	free(noise);
	free(x0);

	/* Denoise from t_start down to 0 */
	float *latent;
	if (ctx->is_distilled) {
		latent = iris_sample_euler_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, latent_h, latent_w, text_emb, text_seq,
		                                schedule + skip, actual_steps, NULL);
	}
	else {
		latent = iris_sample_euler_cfg_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, latent_h, latent_w, text_emb, text_seq,
		                                    text_emb_uncond, text_seq_uncond, guidance, schedule + skip, actual_steps, NULL);
	}

	free(z);
	free(schedule);
	free(text_emb);
	free(text_emb_uncond);

	if (!latent) {
		set_error("Sampling failed");
		return NULL;
	}

	iris_image *result = NULL;
	if (ctx->vae) {
		iris_release_transformer(ctx);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 0);
		result = iris_vae_decode(ctx->vae, latent, 1, latent_h, latent_w);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 1);
	}
	free(latent);
	return result;
}

/* ========================================================================
 * Inpainting (masked img2img)
 * ======================================================================== */

/* Masked image-to-image for object removal and local edits. The mask is a
 * grayscale image: white (255) marks regions to regenerate, black (0) marks
 * regions to keep. Two things make this seamless and effective:
 *   1. The keep region is pinned to the original image's diffusion trajectory
 *      at every denoising step, so it reconstructs the original and blends in
 *      latent space (no pixel-space seams).
 *   2. The masked region of the reference fed to attention is neutralized
 *      (filled with mid-gray) so the model does not "see" and redraw the
 *      content being removed.
 * Output dimensions always match the (16-aligned) input; any width/height
 * override in params is ignored since the keep region must align with x0. */
iris_image *iris_inpaint(iris_ctx *ctx, const char *prompt, const iris_image *input, const iris_image *mask, const iris_params *params) {
	if (!ctx || !prompt || !input || !mask) {
		set_error("Invalid parameters");
		return NULL;
	}
	iris_params p = params ? *params : (iris_params)IRIS_PARAMS_DEFAULT;

	/* Seamless / tileable generation: enable circular conv padding */
	iris_circular = p.circular;

	/* Inpainting requires the output grid to match the input, so ignore any
	 * width/height override and work at the input's dimensions. */
	p.width  = input->width;
	p.height = input->height;

	/* Clamp to VAE max dimensions, preserving aspect ratio */
	if (p.width > IRIS_VAE_MAX_DIM || p.height > IRIS_VAE_MAX_DIM) {
		float scale = (float)IRIS_VAE_MAX_DIM / (p.width > p.height ? p.width : p.height);
		p.width     = (int)(p.width * scale);
		p.height    = (int)(p.height * scale);
	}
	p.width  = (p.width / 16) * 16;
	p.height = (p.height / 16) * 16;
	if (p.width < 64)
		p.width = 64;
	if (p.height < 64)
		p.height = 64;

	/* Check attention memory budget */
	int ref_w = p.width, ref_h = p.height;
	{
		int ref_dims[2] = {p.height, p.width};
		if (fit_refs_for_attention(ctx->num_heads, p.height, p.width, ref_dims, 1, IRIS_MAX_SEQ_LEN)) {
			fprintf(stderr, "Note: image resized from %dx%d to %dx%d (GPU attention memory limit)\n", p.width, p.height, ref_dims[1], ref_dims[0]);
			ref_h = ref_dims[0];
			ref_w = ref_dims[1];
		}
	}
	p.width  = ref_w;
	p.height = ref_h;

	/* Resize input and mask to the working resolution */
	iris_image *img_r  = (input->width != ref_w || input->height != ref_h) ? iris_image_resize(input, ref_w, ref_h) : iris_image_clone(input);
	iris_image *mask_r = (mask->width != ref_w || mask->height != ref_h) ? iris_image_resize(mask, ref_w, ref_h) : iris_image_clone(mask);
	if (!img_r || !mask_r) {
		if (img_r)
			iris_image_free(img_r);
		if (mask_r)
			iris_image_free(mask_r);
		set_error("Failed to resize input/mask");
		return NULL;
	}

	/* Build the neutralized reference: a copy of the input with masked pixels
	 * blended toward mid-gray so the model does not see the removed content. */
	iris_image *ref_img = iris_image_clone(img_r);
	if (!ref_img) {
		iris_image_free(img_r);
		iris_image_free(mask_r);
		set_error("Out of memory");
		return NULL;
	}
	{
		int ic = ref_img->channels;
		int mc = mask_r->channels;
		int n  = ref_w * ref_h;
		for (int i = 0; i < n; i++) {
			float m = mask_r->data[(size_t)i * mc] / 255.0f;
			if (m > 0.0f) {
				for (int c = 0; c < ic; c++) {
					uint8_t *px = &ref_img->data[(size_t)i * ic + c];
					*px         = (uint8_t)((1.0f - m) * (*px) + m * 128.0f + 0.5f);
				}
			}
		}
	}

	/* Resolve steps and guidance */
	if (p.num_steps <= 0)
		p.num_steps = ctx->default_steps;
	float guidance = (p.guidance > 0) ? p.guidance : ctx->default_guidance;

	/* Encode text */
	int    text_seq;
	float *text_emb = iris_encode_text(ctx, prompt, &text_seq);
	if (!text_emb) {
		iris_image_free(img_r);
		iris_image_free(mask_r);
		iris_image_free(ref_img);
		set_error("Failed to encode prompt");
		return NULL;
	}
	float *text_emb_uncond = NULL;
	int    text_seq_uncond = 0;
	if (!ctx->is_distilled) {
		text_emb_uncond = iris_encode_text(ctx, "", &text_seq_uncond);
		if (!text_emb_uncond) {
			free(text_emb);
			iris_image_free(img_r);
			iris_image_free(mask_r);
			iris_image_free(ref_img);
			set_error("Failed to encode empty prompt for CFG");
			return NULL;
		}
	}

	/* Release text encoder to free ~8GB before loading transformer */
	iris_release_text_encoder(ctx);
	if (!iris_load_transformer_if_needed(ctx)) {
		free(text_emb);
		free(text_emb_uncond);
		iris_image_free(img_r);
		iris_image_free(mask_r);
		iris_image_free(ref_img);
		return NULL;
	}

	/* Encode original image (x0, keep target) and the neutralized reference */
	if (iris_phase_callback)
		iris_phase_callback("encoding reference image", 0);
	int    latent_h = 0, latent_w = 0, rh2 = 0, rw2 = 0;
	float *orig_tensor = iris_image_to_tensor(img_r);
	float *ref_tensor  = iris_image_to_tensor(ref_img);
	float *x0          = NULL;
	float *ref_latent  = NULL;
	if (ctx->vae) {
		x0         = iris_vae_encode(ctx->vae, orig_tensor, 1, ref_h, ref_w, &latent_h, &latent_w);
		ref_latent = iris_vae_encode(ctx->vae, ref_tensor, 1, ref_h, ref_w, &rh2, &rw2);
	}
	else {
		latent_h = rh2 = ref_h / 16;
		latent_w = rw2 = ref_w / 16;
		x0             = (float *)calloc(IRIS_LATENT_CHANNELS * latent_h * latent_w, sizeof(float));
		ref_latent     = (float *)calloc(IRIS_LATENT_CHANNELS * latent_h * latent_w, sizeof(float));
	}
	free(orig_tensor);
	free(ref_tensor);
	iris_image_free(ref_img);
	if (iris_phase_callback)
		iris_phase_callback("encoding reference image", 1);

	if (!x0 || !ref_latent) {
		free(text_emb);
		free(text_emb_uncond);
		free(x0);
		free(ref_latent);
		iris_image_free(img_r);
		iris_image_free(mask_r);
		set_error("Failed to encode image");
		return NULL;
	}

	/* Build the latent-resolution mask (white = 1 = generate) */
	float      *mask_lat = (float *)malloc((size_t)latent_h * latent_w * sizeof(float));
	iris_image *ml       = iris_image_resize(mask_r, latent_w, latent_h);
	if (!ml || !mask_lat) {
		free(text_emb);
		free(text_emb_uncond);
		free(x0);
		free(ref_latent);
		free(mask_lat);
		if (ml)
			iris_image_free(ml);
		iris_image_free(img_r);
		iris_image_free(mask_r);
		set_error("Failed to build mask");
		return NULL;
	}
	{
		int mc = ml->channels;
		for (int i = 0; i < latent_h * latent_w; i++)
			mask_lat[i] = ml->data[(size_t)i * mc] / 255.0f;
	}
	iris_image_free(ml);
	iris_image_free(img_r);
	iris_image_free(mask_r);

	int out_lat_h     = latent_h;
	int out_lat_w     = latent_w;
	int image_seq_len = out_lat_h * out_lat_w;

	float *schedule = iris_selected_schedule(&p, image_seq_len);

	int64_t seed = (p.seed < 0) ? (int64_t)time(NULL) : p.seed;
	float  *z    = iris_init_noise(1, IRIS_LATENT_CHANNELS, out_lat_h, out_lat_w, seed);

	int t_offset = 10;

	float *latent;
	if (ctx->is_distilled) {
		latent = iris_sample_euler_inpaint_refs_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, out_lat_h, out_lat_w, ref_latent,
		                                             latent_h, latent_w, t_offset, x0, mask_lat, text_emb, text_seq, schedule, p.num_steps, NULL);
	}
	else {
		latent = iris_sample_euler_cfg_inpaint_refs_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, out_lat_h, out_lat_w, ref_latent,
		                                                 latent_h, latent_w, t_offset, x0, mask_lat, text_emb, text_seq, text_emb_uncond, text_seq_uncond,
		                                                 guidance, schedule, p.num_steps, NULL);
	}

	free(z);
	free(x0);
	free(ref_latent);
	free(mask_lat);
	free(schedule);
	free(text_emb);
	free(text_emb_uncond);

	if (!latent) {
		set_error("Sampling failed");
		return NULL;
	}

	iris_image *result = NULL;
	if (ctx->vae) {
		iris_release_transformer(ctx);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 0);
		result = iris_vae_decode(ctx->vae, latent, 1, out_lat_h, out_lat_w);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 1);
	}
	free(latent);
	return result;
}

/* ========================================================================
 * Make Tileable (turn a photo into a seamless tile)
 * ======================================================================== */

/* Turn a non-tiling photo into a seamless tile while staying close to it.
 *
 * The whole image is re-synthesized with a low-strength, circular-padded
 * img2img: the circular (wrap-around) convolutions make the output tile
 * seamlessly, and the low strength keeps every pixel close to the original
 * texture instead of inventing new content. Nothing is masked or regenerated
 * from scratch, so no unrelated content can appear at the borders.
 *
 * `strength` trades fidelity for seamlessness: lower stays closer to the
 * original but may leave a faint seam; higher closes the seam but drifts
 * further from the source. Pass a negative value to use the default. */
iris_image *iris_make_tileable(iris_ctx *ctx, const char *prompt, const iris_image *input, float strength, const iris_params *params) {
	if (!ctx || !prompt || !input) {
		set_error("Invalid parameters");
		return NULL;
	}
	iris_params p = params ? *params : (iris_params)IRIS_PARAMS_DEFAULT;
	p.circular    = 1; /* tiling requires circular conv padding */

	if (strength < 0.0f)
		strength = 0.45f; /* default: close the seam while staying close to the source */
	return iris_img2img_strength(ctx, prompt, input, strength, &p);
}

/* ========================================================================
 * Multi-Reference Generation
 * ======================================================================== */

/* Multi-reference image generation dispatcher. Zero refs routes to txt2img,
 * one ref to the optimized single-reference img2img path. For multiple refs,
 * each reference is VAE-encoded with a distinct RoPE T offset (10, 20, 30...)
 * so the transformer can distinguish them spatially. All reference latents
 * participate in joint attention alongside the noised target tokens. */
iris_image *iris_multiref(iris_ctx *ctx, const char *prompt, const iris_image **refs, int num_refs, const iris_params *params) {
	if (!ctx || !prompt) {
		set_error("Invalid parameters");
		return NULL;
	}
	/* No references - text-to-image */
	if (!refs || num_refs == 0) {
		return iris_generate(ctx, prompt, params);
	}

	/* Single reference - use optimized path */
	if (num_refs == 1) {
		return iris_img2img(ctx, prompt, refs[0], params);
	}

	iris_params p;
	if (params) {
		p = *params;
	}
	else {
		p = (iris_params)IRIS_PARAMS_DEFAULT;
	}

	/* Seamless / tileable generation: enable circular conv padding */
	iris_circular = p.circular;

	/* Use first reference dimensions if not specified */
	if (p.width <= 0)
		p.width = refs[0]->width;
	if (p.height <= 0)
		p.height = refs[0]->height;

	/* Clamp to VAE max dimensions */
	if (p.width > IRIS_VAE_MAX_DIM || p.height > IRIS_VAE_MAX_DIM) {
		float scale = (float)IRIS_VAE_MAX_DIM / (p.width > p.height ? p.width : p.height);
		p.width     = (int)(p.width * scale);
		p.height    = (int)(p.height * scale);
	}

	p.width  = (p.width / 16) * 16;
	p.height = (p.height / 16) * 16;

	/* Resolve steps and guidance */
	if (p.num_steps <= 0)
		p.num_steps = ctx->default_steps;
	float guidance = (p.guidance > 0) ? p.guidance : ctx->default_guidance;

	/* Encode text */
	int    text_seq;
	float *text_emb = iris_encode_text(ctx, prompt, &text_seq);
	if (!text_emb) {
		set_error("Failed to encode prompt");
		return NULL;
	}

	float *text_emb_uncond = NULL;
	int    text_seq_uncond = 0;
	if (!ctx->is_distilled) {
		text_emb_uncond = iris_encode_text(ctx, "", &text_seq_uncond);
		if (!text_emb_uncond) {
			free(text_emb);
			set_error("Failed to encode empty prompt for CFG");
			return NULL;
		}
	}

	iris_release_text_encoder(ctx);

	if (!iris_load_transformer_if_needed(ctx)) {
		free(text_emb);
		free(text_emb_uncond);
		return NULL;
	}

	/* Build reference pixel dimensions, clamped and rounded to 16. */
	int *ref_pixel_dims = (int *)malloc(num_refs * 2 * sizeof(int));
	for (int i = 0; i < num_refs; i++) {
		int rh = (refs[i]->height / 16) * 16;
		int rw = (refs[i]->width / 16) * 16;
		if (rh > IRIS_VAE_MAX_DIM)
			rh = IRIS_VAE_MAX_DIM;
		if (rw > IRIS_VAE_MAX_DIM)
			rw = IRIS_VAE_MAX_DIM;
		if (rh < 16)
			rh = 16;
		if (rw < 16)
			rw = 16;
		ref_pixel_dims[i * 2]     = rh;
		ref_pixel_dims[i * 2 + 1] = rw;
	}

	/* Shrink references if attention would exceed 4 GB. */
	if (fit_refs_for_attention(ctx->num_heads, p.height, p.width, ref_pixel_dims, num_refs, IRIS_MAX_SEQ_LEN)) {
		fprintf(stderr, "Note: reference images resized to fit GPU attention "
		                "memory limit\n");
	}

	/* Encode all reference images */
	iris_ref_t  *ref_latents  = (iris_ref_t *)malloc(num_refs * sizeof(iris_ref_t));
	float      **ref_data     = (float **)malloc(num_refs * sizeof(float *));
	iris_image **resized_imgs = (iris_image **)calloc(num_refs, sizeof(iris_image *));

	for (int i = 0; i < num_refs; i++) {
		const iris_image *ref        = refs[i];
		const iris_image *img_to_use = ref;

		int ref_h = ref_pixel_dims[i * 2];
		int ref_w = ref_pixel_dims[i * 2 + 1];

		/* Resize only if dimensions differ from original */
		if (ref->width != ref_w || ref->height != ref_h) {
			resized_imgs[i] = iris_image_resize(ref, ref_w, ref_h);
			if (!resized_imgs[i]) {
				for (int j = 0; j < i; j++) {
					free(ref_data[j]);
					if (resized_imgs[j])
						iris_image_free(resized_imgs[j]);
				}
				free(ref_latents);
				free(ref_data);
				free(resized_imgs);
				free(ref_pixel_dims);
				free(text_emb);
				free(text_emb_uncond);
				set_error("Failed to resize reference image");
				return NULL;
			}
			img_to_use = resized_imgs[i];
		}

		/* Encode to latent at reference's own size */
		float *tensor = iris_image_to_tensor(img_to_use);
		int    lat_h, lat_w;
		ref_data[i] = iris_vae_encode(ctx->vae, tensor, 1, img_to_use->height, img_to_use->width, &lat_h, &lat_w);
		free(tensor);

		if (!ref_data[i]) {
			for (int j = 0; j < i; j++) {
				free(ref_data[j]);
				if (resized_imgs[j])
					iris_image_free(resized_imgs[j]);
			}
			if (resized_imgs[i])
				iris_image_free(resized_imgs[i]);
			free(ref_latents);
			free(ref_data);
			free(resized_imgs);
			free(ref_pixel_dims);
			free(text_emb);
			free(text_emb_uncond);
			set_error("Failed to encode reference image");
			return NULL;
		}

		ref_latents[i].latent   = ref_data[i];
		ref_latents[i].h        = lat_h;
		ref_latents[i].w        = lat_w;
		ref_latents[i].t_offset = 10 * (i + 1); /* 10, 20, 30, ... */
	}

	/* Free resized images (latents are now encoded) */
	for (int i = 0; i < num_refs; i++) {
		if (resized_imgs[i])
			iris_image_free(resized_imgs[i]);
	}
	free(resized_imgs);
	free(ref_pixel_dims);

	int latent_h      = p.height / 16;
	int latent_w      = p.width / 16;
	int image_seq_len = latent_h * latent_w;

	float  *schedule = iris_selected_schedule(&p, image_seq_len);
	int64_t seed     = (p.seed < 0) ? (int64_t)time(NULL) : p.seed;
	float  *z        = iris_init_noise(1, IRIS_LATENT_CHANNELS, latent_h, latent_w, seed);

	/* Sample with multi-reference conditioning */
	float *latent;
	if (ctx->is_distilled) {
		latent = iris_sample_euler_multirefs_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, latent_h, latent_w, ref_latents, num_refs,
		                                          text_emb, text_seq, schedule, p.num_steps, NULL);
	}
	else {
		latent = iris_sample_euler_cfg_multirefs_flux(ctx->transformer, ctx->qwen3_encoder, z, 1, IRIS_LATENT_CHANNELS, latent_h, latent_w, ref_latents,
		                                              num_refs, text_emb, text_seq, text_emb_uncond, text_seq_uncond, guidance, schedule, p.num_steps, NULL);
	}

	/* Cleanup */
	free(z);
	for (int i = 0; i < num_refs; i++) {
		free(ref_data[i]);
	}
	free(ref_data);
	free(ref_latents);
	free(schedule);
	free(text_emb);
	free(text_emb_uncond);

	if (!latent) {
		set_error("Sampling failed");
		return NULL;
	}

	/* Decode */
	iris_image *result = NULL;
	if (ctx->vae) {
		/* One-shot generation: free the transformer (and its GPU buffers)
		 * before decode so the VAE work set doesn't share peak memory. */
		iris_release_transformer(ctx);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 0);
		result = iris_vae_decode(ctx->vae, latent, 1, latent_h, latent_w);
		if (iris_phase_callback)
			iris_phase_callback("decoding image", 1);
	}

	free(latent);
	return result;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

void iris_set_seed(int64_t seed) {
	iris_rng_seed((uint64_t)seed);
}

const char *iris_model_info(iris_ctx *ctx) {
	static char info[256];
	const char *type;
	if (!ctx) {
		return "No model loaded";
	}
	type = ctx->is_distilled ? "distilled" : "base";
	snprintf(info, sizeof(info), "%s v%s (%s, %d steps, guidance %.1f)", ctx->model_name, ctx->model_version, type, ctx->default_steps, ctx->default_guidance);
	return info;
}

int iris_text_dim(iris_ctx *ctx) {
	return ctx ? ctx->text_dim : 7680;
}

int iris_is_non_commercial(iris_ctx *ctx) {
	return ctx ? ctx->is_non_commercial : 0;
}

/* ========================================================================
 * Low-level API
 * ======================================================================== */

/* Public API: VAE-encode an RGB image to latent space. Converts the image
 * to a float tensor, runs the VAE encoder, and returns the latent buffer
 * with dimensions in out_h/out_w (each 1/16 of the pixel dimensions). */
float *iris_encode_image(iris_ctx *ctx, const iris_image *img, int *out_h, int *out_w) {
	if (!ctx || !img || !ctx->vae) {
		*out_h = *out_w = 0;
		return NULL;
	}

	float *tensor = iris_image_to_tensor(img);
	if (!tensor)
		return NULL;

	float *latent = iris_vae_encode(ctx->vae, tensor, 1, img->height, img->width, out_h, out_w);
	free(tensor);
	return latent;
}

/* Public API: VAE-decode a latent tensor back to an RGB image.
 * Latent dimensions are 1/16 of the output pixel dimensions. */
iris_image *iris_decode_latent(iris_ctx *ctx, const float *latent, int latent_h, int latent_w) {
	if (!ctx || !latent || !ctx->vae)
		return NULL;
	if (iris_phase_callback)
		iris_phase_callback("decoding image", 0);
	iris_image *img = iris_vae_decode(ctx->vae, latent, 1, latent_h, latent_w);
	if (iris_phase_callback)
		iris_phase_callback("decoding image", 1);
	return img;
}

float *iris_denoise_step(iris_ctx *ctx, const float *z, float t, const float *text_emb, int text_len, int latent_h, int latent_w) {
	if (!ctx || !z || !text_emb)
		return NULL;

	/* Load transformer if not already loaded */
	if (!iris_load_transformer_if_needed(ctx)) {
		return NULL;
	}

	return iris_transformer_forward_flux(ctx->transformer, z, latent_h, latent_w, text_emb, text_len, t);
}
