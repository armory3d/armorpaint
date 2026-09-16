/*
 * Iris Metal Acceleration - Optimized Implementation
 *
 * Uses Metal Performance Shaders (MPS) for GPU-accelerated matrix operations.
 * Optimizations:
 * - Weight buffer caching (weights stay on GPU)
 * - Shared memory buffers (zero-copy on Apple Silicon unified memory)
 * - Buffer pooling for activations
 * - Batched command buffer execution
 */

#include "iris_metal.h"
#include "iris_kernels.h"
#include "iris_shaders_source.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <MetalPerformanceShadersGraph/MetalPerformanceShadersGraph.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bf16_debug_enabled(void) {
	static int enabled = -1;
	if (enabled < 0) {
		enabled = getenv("IRIS_BF16_DEBUG") ? 1 : 0;
	}
	return enabled;
}

static int bf16_linear_use_graph(int seq_len, int in_dim, int out_dim) {
	if (!NSClassFromString(@"MPSGraph"))
		return 0;
	if (in_dim < 256 || out_dim < 256)
		return 0;
	if (seq_len < 32)
		return 0;
	return 1;
}

/* Global Metal state */
static id<MTLDevice>       g_device      = nil;
static id<MTLCommandQueue> g_queue       = nil;
static int                 g_initialized = 0;

/* Cache for MPSGraph-based SDPA graphs (bf16) */
#define MAX_SDPA_GRAPH_CACHE 8
typedef struct {
	int                      seq_q;
	int                      seq_k;
	int                      num_heads;
	int                      head_dim;
	__strong MPSGraph       *graph;
	__strong MPSGraphTensor *qTensor;
	__strong MPSGraphTensor *kTensor;
	__strong MPSGraphTensor *vTensor;
	__strong MPSGraphTensor *outTensor;
	__strong NSArray<NSNumber *> *qShape;
	__strong NSArray<NSNumber *> *kShape;
	__strong NSArray<NSNumber *> *vShape;
	__strong NSArray<NSNumber *> *outShape;
} sdpa_graph_cache_t;

static sdpa_graph_cache_t g_sdpa_graph_cache[MAX_SDPA_GRAPH_CACHE];
static int                g_sdpa_graph_count = 0;
static pthread_mutex_t    g_sdpa_graph_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Cache for MPSGraph-based bf16 linear graphs */
#define MAX_LINEAR_GRAPH_CACHE 32
typedef struct {
	int                      seq;
	int                      in_dim;
	int                      out_dim;
	__strong MPSGraph       *graph;
	__strong MPSGraphTensor *xTensor;
	__strong MPSGraphTensor *wTensor;
	__strong MPSGraphTensor *outTensor;
	__strong NSArray<NSNumber *> *xShape;
	__strong NSArray<NSNumber *> *wShape;
	__strong NSArray<NSNumber *> *outShape;
} linear_graph_cache_t;

static linear_graph_cache_t g_linear_graph_cache[MAX_LINEAR_GRAPH_CACHE];
static int                  g_linear_graph_count = 0;
static pthread_mutex_t      g_linear_graph_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Cache for MPSGraph bf16 matmul graphs (no fp32 casts).
 * If unsupported on the current OS/driver, we transparently fall back to the
 * fp32-cast graphs above. */
static linear_graph_cache_t g_linear_graph_cache_bf16[MAX_LINEAR_GRAPH_CACHE];
static int                  g_linear_graph_bf16_count        = 0;
static pthread_mutex_t      g_linear_graph_bf16_mutex        = PTHREAD_MUTEX_INITIALIZER;
static int                  g_linear_graph_bf16_disabled     = 0;
static int                  g_linear_graph_bf16_seen_success = 0;

/* ========================================================================
 * MPSGraph Conv2D Cache
 * ======================================================================== */

#define MAX_CONV_GRAPH_CACHE 64

typedef struct {
	int                      batch;
	int                      in_ch;
	int                      out_ch;
	int                      H;
	int                      W;
	int                      kH;
	int                      kW;
	int                      stride;
	int                      padding;
	int                      circular;
	__strong MPSGraph       *graph;
	__strong MPSGraphTensor *inputTensor;
	__strong MPSGraphTensor *weightTensor;
	__strong MPSGraphTensor *biasTensor;
	__strong MPSGraphTensor *outTensor;
	__strong NSArray<NSNumber *> *inputShape;
	__strong NSArray<NSNumber *> *weightShape;
	__strong NSArray<NSNumber *> *biasShape;
	__strong NSArray<NSNumber *> *outShape;
} conv2d_graph_cache_t;

static conv2d_graph_cache_t g_conv_graph_cache[MAX_CONV_GRAPH_CACHE];
static int                  g_conv_graph_count = 0;
static int                  g_conv_graph_next  = 0;
static pthread_mutex_t      g_conv_graph_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================
 * Batch Execution State
 * When in batch mode, operations are encoded but not executed until
 * iris_metal_end_batch() is called.
 * ======================================================================== */

#define MAX_BATCH_OUTPUTS 256

typedef struct {
	id<MTLBuffer> buffer;
	float        *cpu_ptr;
	size_t        size;
	int           rows; /* M  */
	int           cols; /* N  */
	int           ldc;  /* row stride of cpu_ptr, in elements */
} pending_output_t;

static id<MTLCommandBuffer> g_batch_cmd = nil;
static int                  g_in_batch  = 0;
static pending_output_t     g_pending_outputs[MAX_BATCH_OUTPUTS];
static int                  g_pending_count = 0;

/* Copy an MPS result buffer back to a (possibly row-strided) CPU matrix.
 * MPS writes only the N valid columns of each row (rowBytes = ldc), leaving the
 * gap columns of a strided output (ldc > N) untouched. A single contiguous copy
 * is therefore only correct when the matrix is packed (ldc == N); otherwise it
 * would clobber the caller's gap columns and run past the end of C. When the
 * output is strided we copy row by row, mirroring the Vulkan sgemm semantics
 * (e.g. depth attention writes each head's head_dim-wide slice into a hidden-
 * wide row). */
static void sgemm_copy_result(float *C, const float *src, int M, int N, int ldc) {
	if (ldc == N) {
		memcpy(C, src, (size_t)M * ldc * sizeof(float));
		return;
	}
	for (int r = 0; r < M; r++)
		memcpy(C + (size_t)r * ldc, src + (size_t)r * ldc, (size_t)N * sizeof(float));
}

/* Input buffer cache - during batch mode, cache input buffers to avoid
 * redundant copies when the same tensor is used as input to multiple ops */
#define MAX_BATCH_INPUTS 64

typedef struct {
	const void   *cpu_ptr;    /* CPU pointer (key) */
	id<MTLBuffer> gpu_buffer; /* GPU buffer with copied data */
	size_t        size;       /* Buffer size */
} batch_input_entry_t;

static batch_input_entry_t g_batch_inputs[MAX_BATCH_INPUTS];
static int                 g_batch_input_count = 0;

/* Forward declaration */
static id<MTLBuffer> pool_get_buffer(size_t size);

/* Get or create an input buffer during batch mode */
static id<MTLBuffer> batch_get_input_buffer(const void *cpu_ptr, size_t size) {
	if (!g_in_batch)
		return nil;

	/* Check if already in cache */
	for (int i = 0; i < g_batch_input_count; i++) {
		if (g_batch_inputs[i].cpu_ptr == cpu_ptr && g_batch_inputs[i].size == size) {
			return g_batch_inputs[i].gpu_buffer;
		}
	}

	/* Not in cache - create new entry */
	if (g_batch_input_count >= MAX_BATCH_INPUTS) {
		return nil; /* Cache full */
	}

	id<MTLBuffer> buffer = pool_get_buffer(size);
	if (!buffer)
		return nil;

	memcpy([buffer contents], cpu_ptr, size);

	g_batch_inputs[g_batch_input_count].cpu_ptr    = cpu_ptr;
	g_batch_inputs[g_batch_input_count].gpu_buffer = buffer;
	g_batch_inputs[g_batch_input_count].size       = size;
	g_batch_input_count++;

	return buffer;
}

/* Forward declarations for compute shaders (defined later) */
static id<MTLComputePipelineState> g_softmax_pipeline;
static id<MTLComputePipelineState> g_bmm_half_qkt_pipeline;
static id<MTLComputePipelineState> g_bmm_half_sv_pipeline;
static id<MTLComputePipelineState> g_softmax_half_pipeline;
/* BF16 native pipelines (forward declarations) */
static id<MTLComputePipelineState> g_rms_norm_bf16_pipeline;
static id<MTLComputePipelineState> g_qk_rms_norm_bf16_pipeline;
static id<MTLComputePipelineState> g_adaln_norm_bf16_pipeline;
static id<MTLComputePipelineState> g_silu_bf16_pipeline;
static id<MTLComputePipelineState> g_silu_mul_bf16_pipeline;
static id<MTLComputePipelineState> g_add_bf16_pipeline;
static id<MTLComputePipelineState> g_gated_add_bf16_pipeline;
static id<MTLComputePipelineState> g_rope_unified_bf16_pipeline;
static id<MTLComputePipelineState> g_rope_2d_bf16_pipeline;
static id<MTLComputePipelineState> g_bmm_bf16_qkt_pipeline;
static id<MTLComputePipelineState> g_bmm_bf16_sv_pipeline;
static id<MTLComputePipelineState> g_softmax_bf16_pipeline;
static id<MTLComputePipelineState> g_f32_to_bf16_pipeline;
static id<MTLComputePipelineState> g_bf16_to_f32_pipeline;
static id<MTLComputePipelineState> g_linear_bf16_pipeline;
static id<MTLComputePipelineState> g_split_qkv_mlp_bf16_pipeline;
static id<MTLComputePipelineState> g_concat_attn_mlp_bf16_pipeline;
static id<MTLComputePipelineState> g_concat_seq_bf16_pipeline;
static id<MTLComputePipelineState> g_slice_seq_bf16_pipeline;
static id<MTLComputePipelineState> g_transpose_to_heads_bf16_pipeline;
static id<MTLComputePipelineState> g_transpose_from_heads_bf16_pipeline;
static id<MTLComputePipelineState> g_attention_fused_bf16_pipeline;
/* F32 VAE pipelines */
static id<MTLComputePipelineState> g_group_norm_f32_pipeline;
static id<MTLComputePipelineState> g_swish_f32_pipeline;
static id<MTLComputePipelineState> g_add_f32_pipeline;
static id<MTLComputePipelineState> g_upsample_nearest_2x_f32_pipeline;
static id<MTLComputePipelineState> g_leaky_relu_f32_pipeline;
static id<MTLComputePipelineState> g_scale_add_f32_pipeline;
static int                         g_shaders_initialized;

/* ========================================================================
 * Weight Buffer Cache
 * Cache GPU buffers for weight matrices to avoid repeated allocations.
 * Weights are identified by their CPU pointer address.
 * ======================================================================== */

#define WEIGHT_CACHE_SIZE 512

typedef struct {
	const void   *cpu_ptr;    /* CPU pointer (key) */
	id<MTLBuffer> gpu_buffer; /* Cached GPU buffer */
	size_t        size;       /* Buffer size */
} weight_cache_entry_t;

static weight_cache_entry_t g_weight_cache[WEIGHT_CACHE_SIZE];
static int                  g_weight_cache_count = 0;
static pthread_mutex_t      g_cache_mutex        = PTHREAD_MUTEX_INITIALIZER;

static id<MTLBuffer> get_cached_weight_buffer(const float *weights, size_t size) {
	pthread_mutex_lock(&g_cache_mutex);

	/* Look for existing entry */
	for (int i = 0; i < g_weight_cache_count; i++) {
		if (g_weight_cache[i].cpu_ptr == weights && g_weight_cache[i].size == size) {
			id<MTLBuffer> buf = g_weight_cache[i].gpu_buffer;
			pthread_mutex_unlock(&g_cache_mutex);
			return buf;
		}
	}

	/* Not found - create new buffer */
	if (g_weight_cache_count >= WEIGHT_CACHE_SIZE) {
		/* Cache full - just create without caching */
		pthread_mutex_unlock(&g_cache_mutex);
		return [g_device newBufferWithBytes:weights length:size options:MTLResourceStorageModeShared];
	}

	/* Create and cache */
	id<MTLBuffer> buf                               = [g_device newBufferWithBytes:weights length:size options:MTLResourceStorageModeShared];
	g_weight_cache[g_weight_cache_count].cpu_ptr    = weights;
	g_weight_cache[g_weight_cache_count].gpu_buffer = buf;
	g_weight_cache[g_weight_cache_count].size       = size;
	g_weight_cache_count++;

	pthread_mutex_unlock(&g_cache_mutex);
	return buf;
}

static void clear_weight_cache(void) {
	pthread_mutex_lock(&g_cache_mutex);
	for (int i = 0; i < g_weight_cache_count; i++) {
		g_weight_cache[i].gpu_buffer = nil;
		g_weight_cache[i].cpu_ptr    = NULL;
	}
	g_weight_cache_count = 0;
	pthread_mutex_unlock(&g_cache_mutex);
}

/* ========================================================================
 * Activation Buffer Pool
 * Reusable GPU buffers for activation tensors to avoid per-operation allocation.
 * Buffers use shared memory mode for zero-copy CPU access on Apple Silicon.
 * ======================================================================== */

#define ACTIVATION_POOL_SIZE 64

typedef struct {
	id<MTLBuffer> buffer;
	size_t        size;
	int           in_use;
} pool_buffer_t;

static pool_buffer_t   g_activation_pool[ACTIVATION_POOL_SIZE];
static int             g_pool_count = 0;
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static int tensor_zero_init_enabled(void) {
	return 1;
}

/* Forward declarations for tensor batch/chain mode checks. */
static int tensor_batch_active(void);
static int tensor_chain_active(void);

/* Deferred pool releases to avoid buffer reuse while command buffers are in-flight. */
#define DEFERRED_POOL_RELEASE_MAX 16384
static id<MTLBuffer> g_deferred_pool_buffers[DEFERRED_POOL_RELEASE_MAX];
static int           g_deferred_pool_count = 0;

/* Get a buffer from pool, or create new one if needed */
static id<MTLBuffer> pool_get_buffer(size_t size) {
	pthread_mutex_lock(&g_pool_mutex);

	/* Look for existing buffer of sufficient size */
	for (int i = 0; i < g_pool_count; i++) {
		if (!g_activation_pool[i].in_use && g_activation_pool[i].size >= size) {
			g_activation_pool[i].in_use = 1;
			id<MTLBuffer> buf           = g_activation_pool[i].buffer;
			pthread_mutex_unlock(&g_pool_mutex);
			return buf;
		}
	}

	/* No suitable buffer found - create new one */
	if (g_pool_count < ACTIVATION_POOL_SIZE) {
		/* Round up size to reduce fragmentation */
		size_t alloc_size = size;
		if (alloc_size < 1024 * 1024) {
			alloc_size = ((alloc_size + 65535) / 65536) * 65536; /* 64KB alignment */
		}
		else {
			alloc_size = ((alloc_size + 1048575) / 1048576) * 1048576; /* 1MB alignment */
		}

		id<MTLBuffer> buf = [g_device newBufferWithLength:alloc_size options:MTLResourceStorageModeShared];
		if (buf) {
			g_activation_pool[g_pool_count].buffer = buf;
			g_activation_pool[g_pool_count].size   = alloc_size;
			g_activation_pool[g_pool_count].in_use = 1;
			g_pool_count++;
			pthread_mutex_unlock(&g_pool_mutex);
			return buf;
		}
	}

	pthread_mutex_unlock(&g_pool_mutex);

	/* Pool full or allocation failed - create temporary buffer */
	return [g_device newBufferWithLength:size options:MTLResourceStorageModeShared];
}

/* Return buffer to pool immediately (mark as available). */
static void pool_release_buffer_immediate(id<MTLBuffer> buffer) {
	if (!buffer)
		return;

	pthread_mutex_lock(&g_pool_mutex);
	for (int i = 0; i < g_pool_count; i++) {
		if (g_activation_pool[i].buffer == buffer) {
			g_activation_pool[i].in_use = 0;
			break;
		}
	}
	pthread_mutex_unlock(&g_pool_mutex);
}

/* Return buffer to pool (mark as available) */
static void pool_release_buffer(id<MTLBuffer> buffer) {
	if (!buffer)
		return;

	if (tensor_batch_active() || tensor_chain_active()) {
		pthread_mutex_lock(&g_pool_mutex);
		if (g_deferred_pool_count < DEFERRED_POOL_RELEASE_MAX) {
			g_deferred_pool_buffers[g_deferred_pool_count++] = buffer;
			pthread_mutex_unlock(&g_pool_mutex);
			return;
		}
		pthread_mutex_unlock(&g_pool_mutex);
	}

	pool_release_buffer_immediate(buffer);
}

/* Release all buffers back to pool (call at end of batch) */
static void pool_release_all(void) {
	pthread_mutex_lock(&g_pool_mutex);
	for (int i = 0; i < g_pool_count; i++) {
		g_activation_pool[i].in_use = 0;
	}
	pthread_mutex_unlock(&g_pool_mutex);
}

/* Flush deferred pool releases after command buffer completion. */
static void pool_flush_deferred(void) {
	pthread_mutex_lock(&g_pool_mutex);
	for (int i = 0; i < g_deferred_pool_count; i++) {
		id<MTLBuffer> buffer = g_deferred_pool_buffers[i];
		for (int j = 0; j < g_pool_count; j++) {
			if (g_activation_pool[j].buffer == buffer) {
				g_activation_pool[j].in_use = 0;
				break;
			}
		}
		g_deferred_pool_buffers[i] = nil;
	}
	g_deferred_pool_count = 0;
	pthread_mutex_unlock(&g_pool_mutex);
}

/* Clear the entire pool */
static void clear_activation_pool(void) {
	pthread_mutex_lock(&g_pool_mutex);
	for (int i = 0; i < g_pool_count; i++) {
		g_activation_pool[i].buffer = nil;
		g_activation_pool[i].in_use = 0;
		g_activation_pool[i].size   = 0;
	}
	g_pool_count          = 0;
	g_deferred_pool_count = 0;
	pthread_mutex_unlock(&g_pool_mutex);
}

/* ========================================================================
 * Metal Initialization
 * ======================================================================== */

int iris_metal_init(void) {
	if (g_initialized)
		return 1;

	@autoreleasepool {
		/* Get default Metal device */
		g_device = MTLCreateSystemDefaultDevice();
		if (!g_device) {
			return 0;
		}

		/* Check if this is Apple Silicon */
		if (![g_device supportsFamily:MTLGPUFamilyApple7]) {
			if (![g_device supportsFamily:MTLGPUFamilyApple6]) {
				g_device = nil;
				return 0;
			}
		}

		/* Create command queue */
		g_queue = [g_device newCommandQueue];
		if (!g_queue) {
			g_device = nil;
			return 0;
		}

		/* Initialize weight cache */
		memset(g_weight_cache, 0, sizeof(g_weight_cache));

		g_initialized = 1;

		/* Load compute shaders (high thresholds keep them dormant for small ops) */
		iris_metal_init_shaders();
	}

	return 1;
}

int iris_metal_available(void) {
	return g_initialized;
}

static void clear_bf16_cache(void);
static void clear_f16_cache(void);
static void clear_rope_cache(void);
static void clear_q8_registry(void);
void        iris_metal_rope_cache_begin(void);

void iris_metal_cleanup(void) {
	if (!g_initialized)
		return;

	@autoreleasepool {
		/* End any pending batch */
		if (g_in_batch) {
			iris_metal_end_batch();
		}
		clear_weight_cache();
		clear_bf16_cache();
		clear_f16_cache();
		clear_rope_cache();
		clear_activation_pool();
		clear_q8_registry();
		g_queue       = nil;
		g_device      = nil;
		g_initialized = 0;
	}
}

void iris_metal_reset(void) {
	if (!g_initialized)
		return;

	@autoreleasepool {
		/* Wait for any pending GPU work */
		iris_metal_sync();

		/* End any pending batch */
		if (g_in_batch) {
			iris_metal_end_batch();
		}

		/* End any pending tensor batch or chain via the public APIs
		 * (they check mode flags internally and are safe to call) */
		iris_gpu_batch_end();
		iris_gpu_chain_end();

		/* Clear all caches */
		clear_weight_cache();
		clear_bf16_cache();
		clear_f16_cache();
		clear_rope_cache();
		clear_activation_pool();

		/* Clear batch input cache */
		pthread_mutex_lock(&g_cache_mutex);
		g_batch_input_count = 0;
		pthread_mutex_unlock(&g_cache_mutex);

		/* Clear pending outputs */
		g_pending_count = 0;

		/* Note: Device, queue, and pipelines are preserved */
	}
}

void iris_metal_reset_transient(void) {
	if (!g_initialized)
		return;

	@autoreleasepool {
		/* Drain in-flight work and close any open batch/chain scopes. */
		iris_gpu_sync();
		iris_gpu_batch_end();
		iris_gpu_chain_end();

		/* Drop transient execution state, but keep model/weight caches hot. */
		clear_rope_cache();
		clear_activation_pool();

		pthread_mutex_lock(&g_cache_mutex);
		g_batch_input_count = 0;
		pthread_mutex_unlock(&g_cache_mutex);
		g_pending_count = 0;
	}
}

/* Debug: Clear only specific caches (for isolating issues) */
void iris_metal_clear_weight_cache_only(void) {
	if (!g_initialized)
		return;
	iris_metal_sync();
	clear_weight_cache();
}

void iris_metal_clear_bf16_cache_only(void) {
	if (!g_initialized)
		return;
	iris_metal_sync();
	clear_bf16_cache();
}

void iris_metal_clear_f16_cache_only(void) {
	if (!g_initialized)
		return;
	iris_metal_sync();
	clear_f16_cache();
}

void iris_metal_clear_activation_pool_only(void) {
	if (!g_initialized)
		return;
	iris_metal_sync();
	clear_activation_pool();
}

void iris_metal_rope_cache_begin(void) {
	if (!g_initialized)
		return;
	clear_rope_cache();
}

/* ========================================================================
 * Batch Execution Functions
 * ======================================================================== */

void iris_metal_begin_batch(void) {
	if (!g_initialized || g_in_batch)
		return;

	@autoreleasepool {
		g_batch_cmd     = [g_queue commandBuffer];
		g_in_batch      = 1;
		g_pending_count = 0;
	}
}

void iris_metal_end_batch(void) {
	if (!g_initialized || !g_in_batch)
		return;

	@autoreleasepool {
		if (g_batch_cmd) {
			[g_batch_cmd commit];
			[g_batch_cmd waitUntilCompleted];

			/* Copy all pending outputs back to CPU (respecting row stride) */
			for (int i = 0; i < g_pending_count; i++) {
				sgemm_copy_result(g_pending_outputs[i].cpu_ptr, [g_pending_outputs[i].buffer contents], g_pending_outputs[i].rows, g_pending_outputs[i].cols,
				                  g_pending_outputs[i].ldc);
				/* Don't nil the buffer - it's from the pool */
			}

			g_batch_cmd = nil;
		}
		g_in_batch          = 0;
		g_pending_count     = 0;
		g_batch_input_count = 0; /* Clear input cache */

		/* Release all pooled buffers back to pool */
		pool_release_all();
	}
}

int iris_metal_in_batch(void) {
	return g_in_batch;
}

/* ========================================================================
 * Optimized Matrix Multiplication
 * ======================================================================== */

static void iris_metal_sgemm_impl(int transpose_a, int transpose_b, int M, int N, int K, float alpha, const float *A, int lda, const float *B, int ldb,
                                  float beta, float *C, int ldc, int cache_B) {
	if (!g_initialized)
		return;

	@autoreleasepool {
		/* Compute dimensions */
		int rowsA = transpose_a ? K : M;
		int colsA = transpose_a ? M : K;
		int rowsB = transpose_b ? N : K;
		int colsB = transpose_b ? K : N;

		size_t sizeA = (size_t)rowsA * lda * sizeof(float);
		size_t sizeB = (size_t)rowsB * ldb * sizeof(float);
		size_t sizeC = (size_t)M * ldc * sizeof(float);

		/* Get or create buffers.
		 * A (input) and C (output) use pooled buffers.
		 * B uses the cache only when explicitly requested (static weights).
		 *
		 * Why this split exists:
		 * We observed intermittent decode corruption (hue shift + border)
		 * in long-lived sessions. Root cause was pointer-based B caching in the
		 * generic SGEMM path: dynamic matrices (for example VAE attention K/V)
		 * can be reallocated at the same CPU address, so stale cached GPU data
		 * was reused. The generic path must not cache B by pointer. */
		id<MTLBuffer> bufferB = nil;
		if (cache_B) {
			/* Static immutable weights: safe to cache by pointer. */
			bufferB = get_cached_weight_buffer(B, sizeB);
		}
		else if (g_in_batch) {
			/* Dynamic B in batch mode: keep data alive via pool, not pointer cache. */
			bufferB = pool_get_buffer(sizeB);
			if (bufferB)
				memcpy([bufferB contents], B, sizeB);
		}
		else {
			/* Dynamic B outside batch: one-shot upload, no pointer-based cache. */
			bufferB = [g_device newBufferWithBytes:B length:sizeB options:MTLResourceStorageModeShared];
		}

		/* Use cached input buffer if in batch mode, otherwise allocate fresh */
		id<MTLBuffer> bufferA            = nil;
		int           bufferA_from_cache = 0;
		if (g_in_batch) {
			bufferA            = batch_get_input_buffer(A, sizeA);
			bufferA_from_cache = (bufferA != nil);
		}
		if (!bufferA) {
			bufferA = pool_get_buffer(sizeA);
			if (bufferA) {
				memcpy([bufferA contents], A, sizeA);
			}
		}

		id<MTLBuffer> bufferC = pool_get_buffer(sizeC);

		if (!bufferA || !bufferB || !bufferC) {
			/* Fallback if buffer creation fails */
			if (bufferA && !bufferA_from_cache)
				pool_release_buffer(bufferA);
			if (bufferC)
				pool_release_buffer(bufferC);
			return;
		}

		/* Initialize C if beta != 0 */
		if (beta != 0.0f) {
			memcpy([bufferC contents], C, sizeC);
		}

		/* Create matrix descriptors */
		MPSMatrixDescriptor *descA = [MPSMatrixDescriptor matrixDescriptorWithRows:rowsA
		                                                                   columns:colsA
		                                                                  rowBytes:lda * sizeof(float)
		                                                                  dataType:MPSDataTypeFloat32];

		MPSMatrixDescriptor *descB = [MPSMatrixDescriptor matrixDescriptorWithRows:rowsB
		                                                                   columns:colsB
		                                                                  rowBytes:ldb * sizeof(float)
		                                                                  dataType:MPSDataTypeFloat32];

		MPSMatrixDescriptor *descC = [MPSMatrixDescriptor matrixDescriptorWithRows:M columns:N rowBytes:ldc * sizeof(float) dataType:MPSDataTypeFloat32];

		/* Create MPS matrices */
		MPSMatrix *matrixA = [[MPSMatrix alloc] initWithBuffer:bufferA descriptor:descA];
		MPSMatrix *matrixB = [[MPSMatrix alloc] initWithBuffer:bufferB descriptor:descB];
		MPSMatrix *matrixC = [[MPSMatrix alloc] initWithBuffer:bufferC descriptor:descC];

		/* Create and configure matrix multiplication */
		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:transpose_a ? YES : NO
		                                                                   transposeRight:transpose_b ? YES : NO
		                                                                       resultRows:M
		                                                                    resultColumns:N
		                                                                  interiorColumns:K
		                                                                            alpha:alpha
		                                                                             beta:beta];

		/* Use batch command buffer if in batch mode, otherwise create new one */
		id<MTLCommandBuffer> cmdBuffer = g_in_batch ? g_batch_cmd : [g_queue commandBuffer];

		[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matrixA rightMatrix:matrixB resultMatrix:matrixC];

		if (g_in_batch) {
			/* In batch mode: defer result copy until end_batch */
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufferC;
				g_pending_outputs[g_pending_count].cpu_ptr = C;
				g_pending_outputs[g_pending_count].size    = sizeC;
				g_pending_outputs[g_pending_count].rows    = M;
				g_pending_outputs[g_pending_count].cols    = N;
				g_pending_outputs[g_pending_count].ldc     = ldc;
				g_pending_count++;
				/* Don't release bufferA if it came from batch input cache */
				if (!bufferA_from_cache) {
					pool_release_buffer(bufferA);
				}
			}
			else {
				/* Too many pending outputs - fall back to immediate sync */
				[cmdBuffer commit];
				[cmdBuffer waitUntilCompleted];
				sgemm_copy_result(C, [bufferC contents], M, N, ldc);
				if (!bufferA_from_cache) {
					pool_release_buffer(bufferA);
				}
				pool_release_buffer(bufferC);
			}
		}
		else {
			/* Not in batch mode: execute immediately */
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			sgemm_copy_result(C, [bufferC contents], M, N, ldc);

			/* Release pooled buffers */
			pool_release_buffer(bufferA);
			pool_release_buffer(bufferC);
		}
	}
}

void iris_metal_sgemm(int transpose_a, int transpose_b, int M, int N, int K, float alpha, const float *A, int lda, const float *B, int ldb, float beta,
                      float *C, int ldc) {
	iris_metal_sgemm_impl(transpose_a, transpose_b, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc, 0);
}

void iris_metal_sgemm_cached(int transpose_a, int transpose_b, int M, int N, int K, float alpha, const float *A, int lda, const float *B, int ldb, float beta,
                             float *C, int ldc) {
	iris_metal_sgemm_impl(transpose_a, transpose_b, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc, 1);
}

/* Convert f32 to f16 for MPS matmuls */
static inline uint16_t f32_to_f16(float f32) {
	uint32_t bits = *(uint32_t *)&f32;
	uint32_t sign = (bits >> 16) & 0x8000;
	int32_t  exp  = ((bits >> 23) & 0xFF) - 127 + 15;
	uint32_t mant = (bits >> 13) & 0x3FF;

	if (exp <= 0) {
		return (uint16_t)sign; /* Underflow to zero */
	}
	else if (exp >= 31) {
		return (uint16_t)(sign | 0x7C00); /* Overflow to infinity */
	}
	return (uint16_t)(sign | (exp << 10) | mant);
}

/* Convert f16 to f32 */
static inline float f16_to_f32(uint16_t f16) {
	uint32_t sign = (f16 >> 15) & 0x1;
	uint32_t exp  = (f16 >> 10) & 0x1F;
	uint32_t mant = f16 & 0x3FF;

	uint32_t f32_bits;
	if (exp == 0) {
		f32_bits = sign << 31; /* Zero */
	}
	else if (exp == 31) {
		f32_bits = (sign << 31) | 0x7F800000 | (mant << 13); /* Inf/NaN */
	}
	else {
		f32_bits = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
	}
	return *(float *)&f32_bits;
}

/* Convert bf16 to f16 for MPS compatibility
 * MPS only supports mixed precision with f16, not bf16 */
static inline uint16_t bf16_to_f16(uint16_t bf16) {
	/* bf16: sign(1) + exp(8) + mant(7)
	 * f16:  sign(1) + exp(5) + mant(10)
	 * We need to rebias the exponent and handle special cases */
	uint32_t sign = (bf16 >> 15) & 0x1;
	int32_t  exp  = (bf16 >> 7) & 0xFF; /* bf16 exponent (bias 127) */
	uint32_t mant = bf16 & 0x7F;        /* bf16 mantissa (7 bits) */

	if (exp == 0) {
		/* Zero or denormal -> zero in f16 (denormals too small) */
		return (uint16_t)(sign << 15);
	}
	else if (exp == 0xFF) {
		/* Inf or NaN */
		return (uint16_t)((sign << 15) | 0x7C00 | (mant ? 0x200 : 0));
	}

	/* Rebias: bf16 bias=127, f16 bias=15 */
	int32_t new_exp = exp - 127 + 15;

	if (new_exp <= 0) {
		/* Underflow to zero */
		return (uint16_t)(sign << 15);
	}
	else if (new_exp >= 31) {
		/* Overflow to infinity */
		return (uint16_t)((sign << 15) | 0x7C00);
	}

	/* Normal case: shift mantissa from 7 bits to 10 bits */
	uint32_t new_mant = mant << 3; /* 7 -> 10 bits */
	return (uint16_t)((sign << 15) | (new_exp << 10) | new_mant);
}

/* ============================================================================
 * GGML Q8_0 quantized weight support
 *
 * A "Q8_0" checkpoint stores its large projection weights as GGML Q8_0 block
 * streams (34 bytes per 32 elements: an fp16 scale followed by 32 signed int8
 * quants) rather than bf16. The model passes the raw block-stream pointer
 * through the same bf16 weight APIs (iris_gpu_linear_bf16_*, iris_metal_sgemm_bf16),
 * so the Metal backend keeps a registry of which weight pointers are Q8_0 and
 * dequantizes them to bf16/f16 the first time they are uploaded - then caches
 * the dequantized GPU buffer exactly like a bf16 weight. This mirrors the
 * Vulkan backend, which dequantizes Q8_0 in-shader; here we pay the dequant
 * once per weight at upload instead of on every matmul.
 * ========================================================================== */
#define Q8_REGISTRY_SIZE 8192
static const void     *g_q8_registry[Q8_REGISTRY_SIZE];
static int             g_q8_registry_count = 0;
static pthread_mutex_t g_q8_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Mark a weight pointer as a GGML Q8_0 block stream (called by the model
 * loader). The pointers are stable mmap/offset addresses that live for the
 * model's lifetime; the set is deduped and only consulted on weight-cache
 * misses, so the linear scan never shows up in inference. */
void iris_metal_register_q8_weight(const void *ptr) {
	if (!ptr)
		return;
	pthread_mutex_lock(&g_q8_registry_mutex);
	for (int i = 0; i < g_q8_registry_count; i++) {
		if (g_q8_registry[i] == ptr) {
			pthread_mutex_unlock(&g_q8_registry_mutex);
			return;
		}
	}
	if (g_q8_registry_count < Q8_REGISTRY_SIZE)
		g_q8_registry[g_q8_registry_count++] = ptr;
	pthread_mutex_unlock(&g_q8_registry_mutex);
}

static int is_q8_weight(const void *ptr) {
	if (!ptr)
		return 0;
	pthread_mutex_lock(&g_q8_registry_mutex);
	int found = 0;
	for (int i = 0; i < g_q8_registry_count; i++) {
		if (g_q8_registry[i] == ptr) {
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&g_q8_registry_mutex);
	return found;
}

static void clear_q8_registry(void) {
	pthread_mutex_lock(&g_q8_registry_mutex);
	memset(g_q8_registry, 0, sizeof(g_q8_registry));
	g_q8_registry_count = 0;
	pthread_mutex_unlock(&g_q8_registry_mutex);
}

/* Dequantize a GGML Q8_0 block stream of `num_elements` logical values into
 * `out`. out_is_f16 = 0 writes bf16 (high 16 bits of the f32), 1 writes IEEE
 * f16. num_elements is a multiple of 32 for every Q8_0 weight matrix (the
 * contraction dim K is block-aligned), so blocks never straddle the buffer. */
static void dequant_q8_0_block_stream(const void *q8, size_t num_elements, uint16_t *out, int out_is_f16) {
	const uint8_t *src = (const uint8_t *)q8;
	size_t         nb  = num_elements / 32;
	for (size_t b = 0; b < nb; b++) {
		const uint8_t *blk = src + b * 34;
		uint16_t       scale_bits;
		memcpy(&scale_bits, blk, sizeof(scale_bits));
		float         scale = f16_to_f32(scale_bits);
		const int8_t *qs    = (const int8_t *)(blk + 2);
		uint16_t     *o     = out + b * 32;
		for (int j = 0; j < 32; j++) {
			float v = (float)qs[j] * scale;
			if (out_is_f16) {
				o[j] = f32_to_f16(v);
			}
			else {
				uint32_t bits;
				memcpy(&bits, &v, sizeof(bits));
				o[j] = (uint16_t)(bits >> 16); /* f32 -> bf16 (truncate) */
			}
		}
	}
}

/* BF16 weight cache (stores bf16 weights directly for MPS bf16 matmul) */
#define BF16_WEIGHT_CACHE_SIZE 512

typedef struct {
	const void   *cpu_ptr;
	id<MTLBuffer> gpu_buffer;
	size_t        size;
} bf16_cache_entry_t;

static bf16_cache_entry_t g_bf16_cache[BF16_WEIGHT_CACHE_SIZE];
static int                g_bf16_cache_count = 0;
static pthread_mutex_t    g_bf16_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/* RoPE cache (per forward pass) */
#define ROPE_CACHE_SIZE 8
typedef struct {
	const void   *cpu_ptr;
	size_t        size;
	id<MTLBuffer> gpu_buffer;
} rope_cache_entry_t;

static rope_cache_entry_t g_rope_cache[ROPE_CACHE_SIZE];
static int                g_rope_cache_count = 0;
static pthread_mutex_t    g_rope_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static id<MTLBuffer> get_rope_buffer(const float *data, size_t size) {
	if (!data || size == 0)
		return nil;

	pthread_mutex_lock(&g_rope_cache_mutex);
	for (int i = 0; i < g_rope_cache_count; i++) {
		if (g_rope_cache[i].cpu_ptr == data && g_rope_cache[i].size == size) {
			id<MTLBuffer> buf = g_rope_cache[i].gpu_buffer;
			pthread_mutex_unlock(&g_rope_cache_mutex);
			return buf;
		}
	}

	id<MTLBuffer> buf = [g_device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
	if (!buf) {
		pthread_mutex_unlock(&g_rope_cache_mutex);
		return nil;
	}

	if (g_rope_cache_count < ROPE_CACHE_SIZE) {
		g_rope_cache[g_rope_cache_count].cpu_ptr    = data;
		g_rope_cache[g_rope_cache_count].size       = size;
		g_rope_cache[g_rope_cache_count].gpu_buffer = buf;
		g_rope_cache_count++;
	}

	pthread_mutex_unlock(&g_rope_cache_mutex);
	return buf;
}

static void clear_rope_cache(void) {
	pthread_mutex_lock(&g_rope_cache_mutex);
	for (int i = 0; i < g_rope_cache_count; i++) {
		g_rope_cache[i].gpu_buffer = nil;
		g_rope_cache[i].cpu_ptr    = NULL;
		g_rope_cache[i].size       = 0;
	}
	g_rope_cache_count = 0;
	pthread_mutex_unlock(&g_rope_cache_mutex);
}

/* Get bf16 weights buffer (no conversion - native bf16 for MPS) */
static id<MTLBuffer> get_cached_bf16_buffer(const uint16_t *weights, size_t num_elements) {
	pthread_mutex_lock(&g_bf16_cache_mutex);

	/* Look for existing entry */
	for (int i = 0; i < g_bf16_cache_count; i++) {
		if (g_bf16_cache[i].cpu_ptr == weights) {
			id<MTLBuffer> buf = g_bf16_cache[i].gpu_buffer;
			pthread_mutex_unlock(&g_bf16_cache_mutex);
			return buf;
		}
	}

	size_t size = num_elements * sizeof(uint16_t);

	/* Q8_0 weight: dequantize the block stream to bf16 once, then cache and use
	 * the bf16 buffer exactly like a native bf16 weight. */
	uint16_t   *bf16_tmp  = NULL;
	const void *bytes_src = weights;
	if (is_q8_weight(weights)) {
		bf16_tmp = malloc(size);
		if (!bf16_tmp) {
			pthread_mutex_unlock(&g_bf16_cache_mutex);
			return nil;
		}
		dequant_q8_0_block_stream(weights, num_elements, bf16_tmp, /*out_is_f16=*/0);
		bytes_src = bf16_tmp;
	}

	/* Cache is full - just create buffer without caching */
	if (g_bf16_cache_count >= BF16_WEIGHT_CACHE_SIZE) {
		id<MTLBuffer> buf = [g_device newBufferWithBytes:bytes_src length:size options:MTLResourceStorageModeShared];
		free(bf16_tmp);
		pthread_mutex_unlock(&g_bf16_cache_mutex);
		return buf;
	}

	/* Create and cache */
	id<MTLBuffer> buf = [g_device newBufferWithBytes:bytes_src length:size options:MTLResourceStorageModeShared];
	free(bf16_tmp);

	g_bf16_cache[g_bf16_cache_count].cpu_ptr    = weights;
	g_bf16_cache[g_bf16_cache_count].gpu_buffer = buf;
	g_bf16_cache[g_bf16_cache_count].size       = size;
	g_bf16_cache_count++;

	pthread_mutex_unlock(&g_bf16_cache_mutex);
	return buf;
}

/* Clear bf16 weight cache */
static void clear_bf16_cache(void) {
	pthread_mutex_lock(&g_bf16_cache_mutex);
	for (int i = 0; i < g_bf16_cache_count; i++) {
		g_bf16_cache[i].gpu_buffer = nil;
		g_bf16_cache[i].cpu_ptr    = NULL;
	}
	g_bf16_cache_count = 0;
	pthread_mutex_unlock(&g_bf16_cache_mutex);
}

/* F16 weight cache (stores bf16 weights converted to f16 for older MPS) */
#define F16_WEIGHT_CACHE_SIZE 512

typedef struct {
	const void   *cpu_ptr;
	id<MTLBuffer> gpu_buffer;
	size_t        size;
} f16_cache_entry_t;

static f16_cache_entry_t g_f16_cache[F16_WEIGHT_CACHE_SIZE];
static int               g_f16_cache_count = 0;
static pthread_mutex_t   g_f16_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Clear f16 weight cache (bf16 converted to f16) */
static void clear_f16_cache(void) {
	pthread_mutex_lock(&g_f16_cache_mutex);
	for (int i = 0; i < g_f16_cache_count; i++) {
		g_f16_cache[i].gpu_buffer = nil;
		g_f16_cache[i].cpu_ptr    = NULL;
	}
	g_f16_cache_count = 0;
	pthread_mutex_unlock(&g_f16_cache_mutex);
}

/* Get bf16 weights as f16 buffer for MPS */
static id<MTLBuffer> get_cached_bf16_as_f16_buffer(const uint16_t *weights, size_t num_elements) {
	pthread_mutex_lock(&g_f16_cache_mutex);

	/* Look for existing entry */
	for (int i = 0; i < g_f16_cache_count; i++) {
		if (g_f16_cache[i].cpu_ptr == weights) {
			id<MTLBuffer> buf = g_f16_cache[i].gpu_buffer;
			pthread_mutex_unlock(&g_f16_cache_mutex);
			return buf;
		}
	}

	/* Convert weights to f16. A Q8_0 weight is dequantized straight to f16;
	 * a bf16 weight is rebiased to f16. */
	uint16_t *f16_data = malloc(num_elements * sizeof(uint16_t));
	if (!f16_data) {
		pthread_mutex_unlock(&g_f16_cache_mutex);
		return nil;
	}
	if (is_q8_weight(weights)) {
		dequant_q8_0_block_stream(weights, num_elements, f16_data, /*out_is_f16=*/1);
	}
	else {
		for (size_t i = 0; i < num_elements; i++) {
			f16_data[i] = bf16_to_f16(weights[i]);
		}
	}

	size_t size = num_elements * sizeof(uint16_t);

	/* Cache is full - just create buffer without caching */
	if (g_f16_cache_count >= F16_WEIGHT_CACHE_SIZE) {
		id<MTLBuffer> buf = [g_device newBufferWithBytes:f16_data length:size options:MTLResourceStorageModeShared];
		free(f16_data);
		pthread_mutex_unlock(&g_f16_cache_mutex);
		return buf;
	}

	/* Create and cache */
	id<MTLBuffer> buf = [g_device newBufferWithBytes:f16_data length:size options:MTLResourceStorageModeShared];
	free(f16_data);

	g_f16_cache[g_f16_cache_count].cpu_ptr    = weights;
	g_f16_cache[g_f16_cache_count].gpu_buffer = buf;
	g_f16_cache[g_f16_cache_count].size       = size;
	g_f16_cache_count++;

	pthread_mutex_unlock(&g_f16_cache_mutex);
	return buf;
}

/*
 * Pre-warm the bf16→f16 cache for a weight tensor.
 * This triggers the conversion and caching so it doesn't happen during inference.
 */
void iris_metal_warmup_bf16(const uint16_t *bf16_weights, size_t num_elements) {
	if (!g_initialized || !bf16_weights || num_elements == 0)
		return;
	/* Just calling this function triggers the conversion and caching */
	(void)get_cached_bf16_as_f16_buffer(bf16_weights, num_elements);
}

void iris_metal_warmup_bf16_buffer(const uint16_t *bf16_weights, size_t num_elements) {
	if (!g_initialized || !bf16_weights || num_elements == 0)
		return;
	(void)get_cached_bf16_buffer(bf16_weights, num_elements);
}

/*
 * BF16 matrix multiplication: C = alpha * A @ B + beta * C
 * A is f32, B is bf16 (weights, converted to f16 for MPS), C is f32
 * This provides 2x memory bandwidth for weights.
 * Note: bf16 is converted to f16 because MPS only supports mixed f32/f16 matmul.
 */
void iris_metal_sgemm_bf16(int transpose_a, int transpose_b, int M, int N, int K, float alpha, const float *A, int lda, const uint16_t *B_bf16, int ldb,
                           float beta, float *C, int ldc) {
	if (!g_initialized)
		return;

	@autoreleasepool {
		int rowsA = transpose_a ? K : M;
		int colsA = transpose_a ? M : K;
		int rowsB = transpose_b ? N : K;
		int colsB = transpose_b ? K : N;

		size_t sizeA = (size_t)rowsA * lda * sizeof(float);
		size_t numB  = (size_t)rowsB * ldb; /* Number of bf16 elements */
		size_t sizeC = (size_t)M * ldc * sizeof(float);

		/* Get cached f16 weight buffer (bf16 converted to f16) */
		id<MTLBuffer> bufferB = get_cached_bf16_as_f16_buffer(B_bf16, numB);

		/* Use cached input buffer if in batch mode, otherwise allocate fresh */
		id<MTLBuffer> bufferA            = nil;
		int           bufferA_from_cache = 0;
		if (g_in_batch) {
			bufferA            = batch_get_input_buffer(A, sizeA);
			bufferA_from_cache = (bufferA != nil);
		}
		if (!bufferA) {
			bufferA = pool_get_buffer(sizeA);
			if (bufferA) {
				memcpy([bufferA contents], A, sizeA);
			}
		}

		id<MTLBuffer> bufferC = pool_get_buffer(sizeC);

		if (!bufferA || !bufferB || !bufferC) {
			if (bufferA && !bufferA_from_cache)
				pool_release_buffer(bufferA);
			if (bufferC)
				pool_release_buffer(bufferC);
			return;
		}

		if (beta != 0.0f) {
			memcpy([bufferC contents], C, sizeC);
		}

		/* Create matrix descriptors - B uses Float16 (converted from bf16) */
		MPSMatrixDescriptor *descA = [MPSMatrixDescriptor matrixDescriptorWithRows:rowsA
		                                                                   columns:colsA
		                                                                  rowBytes:lda * sizeof(float)
		                                                                  dataType:MPSDataTypeFloat32];

		MPSMatrixDescriptor *descB = [MPSMatrixDescriptor matrixDescriptorWithRows:rowsB
		                                                                   columns:colsB
		                                                                  rowBytes:ldb * sizeof(uint16_t)
		                                                                  dataType:MPSDataTypeFloat16];

		MPSMatrixDescriptor *descC = [MPSMatrixDescriptor matrixDescriptorWithRows:M columns:N rowBytes:ldc * sizeof(float) dataType:MPSDataTypeFloat32];

		MPSMatrix *matrixA = [[MPSMatrix alloc] initWithBuffer:bufferA descriptor:descA];
		MPSMatrix *matrixB = [[MPSMatrix alloc] initWithBuffer:bufferB descriptor:descB];
		MPSMatrix *matrixC = [[MPSMatrix alloc] initWithBuffer:bufferC descriptor:descC];

		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:transpose_a ? YES : NO
		                                                                   transposeRight:transpose_b ? YES : NO
		                                                                       resultRows:M
		                                                                    resultColumns:N
		                                                                  interiorColumns:K
		                                                                            alpha:alpha
		                                                                             beta:beta];

		id<MTLCommandBuffer> cmdBuffer = g_in_batch ? g_batch_cmd : [g_queue commandBuffer];

		[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matrixA rightMatrix:matrixB resultMatrix:matrixC];

		if (g_in_batch) {
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufferC;
				g_pending_outputs[g_pending_count].cpu_ptr = C;
				g_pending_outputs[g_pending_count].size    = sizeC;
				g_pending_count++;
				/* Don't release bufferA if it came from batch input cache */
				if (!bufferA_from_cache) {
					pool_release_buffer(bufferA);
				}
			}
			else {
				[cmdBuffer commit];
				[cmdBuffer waitUntilCompleted];
				memcpy(C, [bufferC contents], sizeC);
				if (!bufferA_from_cache) {
					pool_release_buffer(bufferA);
				}
				pool_release_buffer(bufferC);
			}
		}
		else {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			memcpy(C, [bufferC contents], sizeC);
			pool_release_buffer(bufferA);
			pool_release_buffer(bufferC);
		}
	}
}

static conv2d_graph_cache_t *get_conv2d_graph_cache(int batch, int in_ch, int out_ch, int H, int W, int kH, int kW, int stride, int padding, int circular) {
	/* Circular padding only matters when there is padding to wrap. */
	circular = (circular && padding > 0) ? 1 : 0;

	pthread_mutex_lock(&g_conv_graph_mutex);
	for (int i = 0; i < g_conv_graph_count && i < MAX_CONV_GRAPH_CACHE; i++) {
		conv2d_graph_cache_t *entry = &g_conv_graph_cache[i];
		if (entry->batch == batch && entry->in_ch == in_ch && entry->out_ch == out_ch && entry->H == H && entry->W == W && entry->kH == kH && entry->kW == kW &&
		    entry->stride == stride && entry->padding == padding && entry->circular == circular) {
			pthread_mutex_unlock(&g_conv_graph_mutex);
			return entry;
		}
	}

	int slot = 0;
	if (g_conv_graph_count < MAX_CONV_GRAPH_CACHE) {
		slot = g_conv_graph_count++;
	}
	else {
		slot = g_conv_graph_next++ % MAX_CONV_GRAPH_CACHE;
	}

	conv2d_graph_cache_t *entry = &g_conv_graph_cache[slot];
	entry->batch                = batch;
	entry->in_ch                = in_ch;
	entry->out_ch               = out_ch;
	entry->H                    = H;
	entry->W                    = W;
	entry->kH                   = kH;
	entry->kW                   = kW;
	entry->stride               = stride;
	entry->padding              = padding;
	entry->circular             = circular;

	int outH = (H + 2 * padding - kH) / stride + 1;
	int outW = (W + 2 * padding - kW) / stride + 1;
	if (outH <= 0 || outW <= 0) {
		pthread_mutex_unlock(&g_conv_graph_mutex);
		return NULL;
	}

	@autoreleasepool {
		MPSGraph *graph = [[MPSGraph alloc] init];
		if (!graph) {
			pthread_mutex_unlock(&g_conv_graph_mutex);
			return NULL;
		}

		NSArray<NSNumber *> *inputShape  = @[ @(batch), @(in_ch), @(H), @(W) ];
		NSArray<NSNumber *> *weightShape = @[ @(out_ch), @(in_ch), @(kH), @(kW) ];
		NSArray<NSNumber *> *biasShape   = @[ @1, @(out_ch), @1, @1 ];
		NSArray<NSNumber *> *outShape    = @[ @(batch), @(out_ch), @(outH), @(outW) ];

		MPSGraphTensor *input  = [graph placeholderWithShape:inputShape dataType:MPSDataTypeFloat32 name:nil];
		MPSGraphTensor *weight = [graph placeholderWithShape:weightShape dataType:MPSDataTypeFloat32 name:nil];
		MPSGraphTensor *bias   = [graph placeholderWithShape:biasShape dataType:MPSDataTypeFloat32 name:nil];

		/* Seamless tiling: wrap the input around a torus (circular padding), then
		 * run a VALID conv. MPSGraph's convolution only offers zero padding, so the
		 * circular wrap must precede it. MPSGraphPaddingModePeriodic exists in the
		 * enum but is not implemented by the underlying MPS pad kernel (it asserts
		 * "Unsupported paddingMode"), so we build the wrap by hand from slices: take
		 * the trailing `padding` elements to the front and the leading ones to the
		 * back, concatenated around the original tensor. Layout is NCHW, so H is
		 * dimension 2 and W is dimension 3. */
		MPSGraphTensor *convSource = input;
		NSUInteger      convPad    = (NSUInteger)padding;
		if (circular) {
			/* Wrap width (dimension 3). */
			MPSGraphTensor *wLeft  = [graph sliceTensor:input dimension:3 start:(W - padding) length:padding name:nil];
			MPSGraphTensor *wRight = [graph sliceTensor:input dimension:3 start:0 length:padding name:nil];
			MPSGraphTensor *wWrap  = [graph concatTensors:@[ wLeft, input, wRight ] dimension:3 name:nil];

			/* Wrap height (dimension 2) on the width-wrapped tensor. */
			MPSGraphTensor *hTop    = [graph sliceTensor:wWrap dimension:2 start:(H - padding) length:padding name:nil];
			MPSGraphTensor *hBottom = [graph sliceTensor:wWrap dimension:2 start:0 length:padding name:nil];
			convSource              = [graph concatTensors:@[ hTop, wWrap, hBottom ] dimension:2 name:nil];

			convPad = 0; /* padding already applied via the circular wrap */
		}

		MPSGraphConvolution2DOpDescriptor *desc = [MPSGraphConvolution2DOpDescriptor descriptorWithStrideInX:(NSUInteger)stride
		                                                                                           strideInY:(NSUInteger)stride
		                                                                                     dilationRateInX:1
		                                                                                     dilationRateInY:1
		                                                                                              groups:1
		                                                                                         paddingLeft:convPad
		                                                                                        paddingRight:convPad
		                                                                                          paddingTop:convPad
		                                                                                       paddingBottom:convPad
		                                                                                        paddingStyle:MPSGraphPaddingStyleExplicit
		                                                                                          dataLayout:MPSGraphTensorNamedDataLayoutNCHW
		                                                                                       weightsLayout:MPSGraphTensorNamedDataLayoutOIHW];

		MPSGraphTensor *conv = [graph convolution2DWithSourceTensor:convSource weightsTensor:weight descriptor:desc name:nil];
		MPSGraphTensor *out  = [graph additionWithPrimaryTensor:conv secondaryTensor:bias name:nil];

		entry->graph        = graph;
		entry->inputTensor  = input;
		entry->weightTensor = weight;
		entry->biasTensor   = bias;
		entry->outTensor    = out;
		entry->inputShape   = inputShape;
		entry->weightShape  = weightShape;
		entry->biasShape    = biasShape;
		entry->outShape     = outShape;
	}

	pthread_mutex_unlock(&g_conv_graph_mutex);
	return entry;
}

int iris_metal_conv2d(float *out, const float *in, const float *weight, const float *bias, int batch, int in_ch, int out_ch, int H, int W, int kH, int kW,
                      int stride, int padding) {
	if (!g_initialized || !g_device || !g_queue)
		return 0;
	if (!out || !in || !weight || !bias)
		return 0;
	if (batch <= 0 || in_ch <= 0 || out_ch <= 0 || H <= 0 || W <= 0 || kH <= 0 || kW <= 0 || stride <= 0) {
		return 0;
	}

	conv2d_graph_cache_t *cache = get_conv2d_graph_cache(batch, in_ch, out_ch, H, W, kH, kW, stride, padding, iris_circular);
	if (!cache || !cache->graph)
		return 0;

	int outH = (H + 2 * padding - kH) / stride + 1;
	int outW = (W + 2 * padding - kW) / stride + 1;
	if (outH <= 0 || outW <= 0)
		return 0;

	size_t in_bytes  = (size_t)batch * in_ch * H * W * sizeof(float);
	size_t out_bytes = (size_t)batch * out_ch * outH * outW * sizeof(float);
	size_t w_bytes   = (size_t)out_ch * in_ch * kH * kW * sizeof(float);
	size_t b_bytes   = (size_t)out_ch * sizeof(float);

	@autoreleasepool {
		/* Use copy-based buffers for input and output to avoid alignment issues */
		id<MTLBuffer> in_buf  = [g_device newBufferWithBytes:in length:in_bytes options:MTLResourceStorageModeShared];
		id<MTLBuffer> out_buf = [g_device newBufferWithLength:out_bytes options:MTLResourceStorageModeShared];
		id<MTLBuffer> w_buf   = get_cached_weight_buffer(weight, w_bytes);
		id<MTLBuffer> b_buf   = get_cached_weight_buffer(bias, b_bytes);
		if (!in_buf || !out_buf || !w_buf || !b_buf)
			return 0;

		MPSGraphTensorData *in_data  = [[MPSGraphTensorData alloc] initWithMTLBuffer:in_buf shape:cache->inputShape dataType:MPSDataTypeFloat32];
		MPSGraphTensorData *w_data   = [[MPSGraphTensorData alloc] initWithMTLBuffer:w_buf shape:cache->weightShape dataType:MPSDataTypeFloat32];
		MPSGraphTensorData *b_data   = [[MPSGraphTensorData alloc] initWithMTLBuffer:b_buf shape:cache->biasShape dataType:MPSDataTypeFloat32];
		MPSGraphTensorData *out_data = [[MPSGraphTensorData alloc] initWithMTLBuffer:out_buf shape:cache->outShape dataType:MPSDataTypeFloat32];
		if (!in_data || !w_data || !b_data || !out_data)
			return 0;

		MPSCommandBuffer *mps_cmd = [MPSCommandBuffer commandBufferFromCommandQueue:g_queue];
		if (!mps_cmd)
			return 0;

		NSDictionary *feeds   = @{cache->inputTensor : in_data, cache->weightTensor : w_data, cache->biasTensor : b_data};
		NSDictionary *results = @{cache->outTensor : out_data};

		@try {
			[cache->graph encodeToCommandBuffer:mps_cmd feeds:feeds targetOperations:nil resultsDictionary:results executionDescriptor:nil];
		} @catch (NSException *exception) {
			return 0;
		}

		[mps_cmd commit];
		[mps_cmd waitUntilCompleted];

		/* Copy result back to output */
		memcpy(out, [out_buf contents], out_bytes);
		return 1;
	}
}

void iris_metal_sgemm_batch(int transpose_a, int transpose_b, int M, int N, int K, float alpha, const float *A, int lda, int stride_a, const float *B, int ldb,
                            int stride_b, float beta, float *C, int ldc, int stride_c, int batch_count) {
	if (!g_initialized || batch_count <= 0)
		return;

	@autoreleasepool {
		/* For batched ops, encode all into single command buffer */
		id<MTLCommandBuffer> cmdBuffer = [g_queue commandBuffer];

		int rowsA = transpose_a ? K : M;
		int colsA = transpose_a ? M : K;
		int rowsB = transpose_b ? N : K;
		int colsB = transpose_b ? K : N;

		/* Create descriptors once */
		MPSMatrixDescriptor *descA = [MPSMatrixDescriptor matrixDescriptorWithRows:rowsA
		                                                                   columns:colsA
		                                                                  rowBytes:lda * sizeof(float)
		                                                                  dataType:MPSDataTypeFloat32];
		MPSMatrixDescriptor *descB = [MPSMatrixDescriptor matrixDescriptorWithRows:rowsB
		                                                                   columns:colsB
		                                                                  rowBytes:ldb * sizeof(float)
		                                                                  dataType:MPSDataTypeFloat32];
		MPSMatrixDescriptor *descC = [MPSMatrixDescriptor matrixDescriptorWithRows:M columns:N rowBytes:ldc * sizeof(float) dataType:MPSDataTypeFloat32];

		/* Create kernel once */
		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:transpose_a ? YES : NO
		                                                                   transposeRight:transpose_b ? YES : NO
		                                                                       resultRows:M
		                                                                    resultColumns:N
		                                                                  interiorColumns:K
		                                                                            alpha:alpha
		                                                                             beta:beta];

		size_t sizeA_elem = (size_t)rowsA * lda * sizeof(float);
		size_t sizeB_elem = (size_t)rowsB * ldb * sizeof(float);
		size_t sizeC_elem = (size_t)M * ldc * sizeof(float);

		/* Store C buffers so we can copy results back after GPU completes */
		__strong id<MTLBuffer> *cBuffers = (__strong id<MTLBuffer> *)calloc(batch_count, sizeof(id<MTLBuffer>));
		float                 **cPtrs    = (float **)malloc(batch_count * sizeof(float *));

		for (int i = 0; i < batch_count; i++) {
			const float *Ai = A + i * stride_a;
			const float *Bi = B + i * stride_b;
			float       *Ci = C + i * stride_c;

			/* Use copy-based buffers to avoid alignment issues */
			id<MTLBuffer> bufA = [g_device newBufferWithBytes:Ai length:sizeA_elem options:MTLResourceStorageModeShared];
			id<MTLBuffer> bufB = get_cached_weight_buffer(Bi, sizeB_elem);
			id<MTLBuffer> bufC = [g_device newBufferWithLength:sizeC_elem options:MTLResourceStorageModeShared];

			cBuffers[i] = bufC;
			cPtrs[i]    = Ci;

			MPSMatrix *matA = [[MPSMatrix alloc] initWithBuffer:bufA descriptor:descA];
			MPSMatrix *matB = [[MPSMatrix alloc] initWithBuffer:bufB descriptor:descB];
			MPSMatrix *matC = [[MPSMatrix alloc] initWithBuffer:bufC descriptor:descC];

			[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matA rightMatrix:matB resultMatrix:matC];
		}

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		/* Copy results back and release buffers */
		for (int i = 0; i < batch_count; i++) {
			memcpy(cPtrs[i], [cBuffers[i] contents], sizeC_elem);
			cBuffers[i] = nil; /* Release under ARC */
		}

		free(cBuffers);
		free(cPtrs);
	}
}

void iris_metal_sync(void) {
	/* This is a legacy no-op function. Actual GPU synchronization is handled by:
	 * - iris_gpu_sync() for tensor operations (g_tensor_cmd)
	 * - iris_metal_end_batch() for batch operations (g_batch_cmd)
	 * - iris_gpu_chain_end() for chain operations (g_chain_cmd)
	 * These are called explicitly where needed (e.g., at end of transformer forward). */
}

void iris_metal_wait_idle(void) {
	if (!g_initialized || !g_queue)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer> cmdBuffer = [g_queue commandBuffer];
		if (!cmdBuffer)
			return;
		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

size_t iris_metal_memory_used(void) {
	if (!g_initialized || !g_device)
		return 0;
	return [g_device currentAllocatedSize];
}

/*
 * GPU-accelerated attention with batched heads.
 * Does: out = softmax(Q @ K^T * scale) @ V for all heads in parallel.
 * Uses GPU softmax shader to avoid CPU roundtrip.
 *
 * Q: [heads, seq_q, head_dim]
 * K: [heads, seq_k, head_dim]
 * V: [heads, seq_k, head_dim]
 * scores_scratch: [heads * seq_q * seq_k] (unused when GPU softmax available)
 * out: [heads, seq_q, head_dim]
 */
void iris_metal_attention(float *out, const float *Q, const float *K, const float *V, float *scores_scratch, int heads, int seq_q, int seq_k, int head_dim,
                          float scale) {
	if (!g_initialized || heads <= 0)
		return;

	@autoreleasepool {
		size_t q_stride      = (size_t)seq_q * head_dim;
		size_t k_stride      = (size_t)seq_k * head_dim;
		size_t v_stride      = (size_t)seq_k * head_dim;
		size_t scores_stride = (size_t)seq_q * seq_k;
		size_t out_stride    = (size_t)seq_q * head_dim;

		size_t sizeQ      = heads * q_stride * sizeof(float);
		size_t sizeK      = heads * k_stride * sizeof(float);
		size_t sizeV      = heads * v_stride * sizeof(float);
		size_t sizeScores = heads * scores_stride * sizeof(float);
		size_t sizeOut    = heads * out_stride * sizeof(float);

		/* Use pooled buffers for efficiency (avoids allocation overhead) */
		id<MTLBuffer> bufQ      = pool_get_buffer(sizeQ);
		id<MTLBuffer> bufK      = pool_get_buffer(sizeK);
		id<MTLBuffer> bufV      = pool_get_buffer(sizeV);
		id<MTLBuffer> bufScores = pool_get_buffer(sizeScores);
		id<MTLBuffer> bufOut    = pool_get_buffer(sizeOut);

		if (!bufQ || !bufK || !bufV || !bufScores || !bufOut) {
			if (bufQ)
				pool_release_buffer(bufQ);
			if (bufK)
				pool_release_buffer(bufK);
			if (bufV)
				pool_release_buffer(bufV);
			if (bufScores)
				pool_release_buffer(bufScores);
			if (bufOut)
				pool_release_buffer(bufOut);
			return;
		}

		/* Copy input data to GPU buffers */
		memcpy([bufQ contents], Q, sizeQ);
		memcpy([bufK contents], K, sizeK);
		memcpy([bufV contents], V, sizeV);

		/* Check if GPU softmax is available */
		int use_gpu_softmax = g_shaders_initialized && g_softmax_pipeline;

		/* Single command buffer for all operations when GPU softmax is available */
		id<MTLCommandBuffer> cmdBuffer = [g_queue commandBuffer];

		/* === Phase 1: Batched Q @ K^T === */
		{
			/* Descriptors for single head matrices */
			MPSMatrixDescriptor *descQ      = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
                                                                               columns:head_dim
                                                                              rowBytes:head_dim * sizeof(float)
                                                                              dataType:MPSDataTypeFloat32];
			MPSMatrixDescriptor *descK      = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_k
                                                                               columns:head_dim
                                                                              rowBytes:head_dim * sizeof(float)
                                                                              dataType:MPSDataTypeFloat32];
			MPSMatrixDescriptor *descScores = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
			                                                                        columns:seq_k
			                                                                       rowBytes:seq_k * sizeof(float)
			                                                                       dataType:MPSDataTypeFloat32];

			/* Create matmul kernel: scores = scale * Q @ K^T */
			MPSMatrixMultiplication *matmul_qk = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
			                                                                       transposeLeft:NO
			                                                                      transposeRight:YES
			                                                                          resultRows:seq_q
			                                                                       resultColumns:seq_k
			                                                                     interiorColumns:head_dim
			                                                                               alpha:scale
			                                                                                beta:0.0f];

			/* Encode all heads */
			for (int h = 0; h < heads; h++) {
				size_t offsetQ = h * q_stride * sizeof(float);
				size_t offsetK = h * k_stride * sizeof(float);
				size_t offsetS = h * scores_stride * sizeof(float);

				MPSMatrix *matQ      = [[MPSMatrix alloc] initWithBuffer:bufQ offset:offsetQ descriptor:descQ];
				MPSMatrix *matK      = [[MPSMatrix alloc] initWithBuffer:bufK offset:offsetK descriptor:descK];
				MPSMatrix *matScores = [[MPSMatrix alloc] initWithBuffer:bufScores offset:offsetS descriptor:descScores];

				[matmul_qk encodeToCommandBuffer:cmdBuffer leftMatrix:matQ rightMatrix:matK resultMatrix:matScores];
			}
		}

		/* === Phase 2: Softmax === */
		if (use_gpu_softmax) {
			/* GPU softmax - encode to same command buffer */
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];

			[encoder setComputePipelineState:g_softmax_pipeline];
			[encoder setBuffer:bufScores offset:0 atIndex:0];

			/* Process each head's scores (rows = seq_q, cols = seq_k) */
			int total_rows = heads * seq_q;
			[encoder setBytes:&total_rows length:sizeof(int) atIndex:1];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:2];

			NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
			[encoder dispatchThreadgroups:MTLSizeMake(total_rows, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

			[encoder endEncoding];
		}
		else {
			/* CPU softmax fallback - sync required */
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];

			/* Copy, softmax on CPU, copy back */
			memcpy(scores_scratch, [bufScores contents], sizeScores);
			extern void iris_softmax(float *x, int rows, int cols);
			for (int h = 0; h < heads; h++) {
				iris_softmax(scores_scratch + h * scores_stride, seq_q, seq_k);
			}
			memcpy([bufScores contents], scores_scratch, sizeScores);

			/* New command buffer for phase 3 */
			cmdBuffer = [g_queue commandBuffer];
		}

		/* === Phase 3: Batched scores @ V === */
		{
			MPSMatrixDescriptor *descScores = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
			                                                                        columns:seq_k
			                                                                       rowBytes:seq_k * sizeof(float)
			                                                                       dataType:MPSDataTypeFloat32];
			MPSMatrixDescriptor *descV      = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_k
                                                                               columns:head_dim
                                                                              rowBytes:head_dim * sizeof(float)
                                                                              dataType:MPSDataTypeFloat32];
			MPSMatrixDescriptor *descOut    = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
                                                                                 columns:head_dim
                                                                                rowBytes:head_dim * sizeof(float)
                                                                                dataType:MPSDataTypeFloat32];

			/* Create matmul kernel: out = scores @ V */
			MPSMatrixMultiplication *matmul_sv = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
			                                                                       transposeLeft:NO
			                                                                      transposeRight:NO
			                                                                          resultRows:seq_q
			                                                                       resultColumns:head_dim
			                                                                     interiorColumns:seq_k
			                                                                               alpha:1.0f
			                                                                                beta:0.0f];

			/* Encode all heads */
			for (int h = 0; h < heads; h++) {
				size_t offsetS = h * scores_stride * sizeof(float);
				size_t offsetV = h * v_stride * sizeof(float);
				size_t offsetO = h * out_stride * sizeof(float);

				MPSMatrix *matScores = [[MPSMatrix alloc] initWithBuffer:bufScores offset:offsetS descriptor:descScores];
				MPSMatrix *matV      = [[MPSMatrix alloc] initWithBuffer:bufV offset:offsetV descriptor:descV];
				MPSMatrix *matOut    = [[MPSMatrix alloc] initWithBuffer:bufOut offset:offsetO descriptor:descOut];

				[matmul_sv encodeToCommandBuffer:cmdBuffer leftMatrix:matScores rightMatrix:matV resultMatrix:matOut];
			}
		}

		/* Execute and copy output back to CPU */
		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
		memcpy(out, [bufOut contents], sizeOut);

		/* Release pooled buffers */
		pool_release_buffer(bufQ);
		pool_release_buffer(bufK);
		pool_release_buffer(bufV);
		pool_release_buffer(bufScores);
		pool_release_buffer(bufOut);
	}
}

/* ========================================================================
 * Half-Precision Attention with Custom Metal Compute Shaders
 *
 * Same interface as iris_metal_attention but uses half-precision compute
 * shaders with f32 accumulation. This provides
 * ~2x memory bandwidth savings while maintaining numerical stability.
 *
 * Layout (same as iris_metal_attention):
 * Q: [heads, seq_q, head_dim]
 * K: [heads, seq_k, head_dim]
 * V: [heads, seq_k, head_dim]
 * scores_scratch: unused (kept for interface compatibility)
 * out: [heads, seq_q, head_dim]
 * ======================================================================== */
void iris_metal_attention_bf16(float *out, const float *Q, const float *K, const float *V, float *scores_scratch, int heads, int seq_q, int seq_k, int head_dim,
                               float scale) {
	if (!g_initialized || heads <= 0)
		return;
	if (!g_shaders_initialized || !g_bmm_half_qkt_pipeline || !g_bmm_half_sv_pipeline || !g_softmax_half_pipeline) {
		/* Fall back to f32 attention if shaders not available */
		iris_metal_attention(out, Q, K, V, scores_scratch, heads, seq_q, seq_k, head_dim, scale);
		return;
	}
	(void)scores_scratch; /* Unused - kept for interface compatibility */

	@autoreleasepool {
		size_t q_elements      = (size_t)heads * seq_q * head_dim;
		size_t k_elements      = (size_t)heads * seq_k * head_dim;
		size_t v_elements      = (size_t)heads * seq_k * head_dim;
		size_t out_elements    = (size_t)heads * seq_q * head_dim;
		size_t scores_elements = (size_t)heads * seq_q * seq_k;

		size_t q_size_f16      = q_elements * sizeof(uint16_t);
		size_t k_size_f16      = k_elements * sizeof(uint16_t);
		size_t v_size_f16      = v_elements * sizeof(uint16_t);
		size_t out_size_f16    = out_elements * sizeof(uint16_t);
		size_t scores_size_f16 = scores_elements * sizeof(uint16_t);

		/* Allocate f16 buffers */
		id<MTLBuffer> bufQ      = pool_get_buffer(q_size_f16);
		id<MTLBuffer> bufK      = pool_get_buffer(k_size_f16);
		id<MTLBuffer> bufV      = pool_get_buffer(v_size_f16);
		id<MTLBuffer> bufScores = pool_get_buffer(scores_size_f16);
		id<MTLBuffer> bufOut    = pool_get_buffer(out_size_f16);

		if (!bufQ || !bufK || !bufV || !bufScores || !bufOut) {
			if (bufQ)
				pool_release_buffer(bufQ);
			if (bufK)
				pool_release_buffer(bufK);
			if (bufV)
				pool_release_buffer(bufV);
			if (bufScores)
				pool_release_buffer(bufScores);
			if (bufOut)
				pool_release_buffer(bufOut);
			return;
		}

		/* Convert f32 inputs to f16 */
		uint16_t *q_f16 = (uint16_t *)[bufQ contents];
		uint16_t *k_f16 = (uint16_t *)[bufK contents];
		uint16_t *v_f16 = (uint16_t *)[bufV contents];

		for (size_t i = 0; i < q_elements; i++) {
			q_f16[i] = f32_to_f16(Q[i]);
		}
		for (size_t i = 0; i < k_elements; i++) {
			k_f16[i] = f32_to_f16(K[i]);
		}
		for (size_t i = 0; i < v_elements; i++) {
			v_f16[i] = f32_to_f16(V[i]);
		}

		id<MTLCommandBuffer> cmdBuffer = [g_queue commandBuffer];

		/* === Phase 1: Q @ K^T using custom half compute shader === */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_bmm_half_qkt_pipeline];

			[encoder setBuffer:bufQ offset:0 atIndex:0];
			[encoder setBuffer:bufK offset:0 atIndex:1];
			[encoder setBuffer:bufScores offset:0 atIndex:2];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:3];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:4];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
			[encoder setBytes:&heads length:sizeof(int) atIndex:6];
			[encoder setBytes:&scale length:sizeof(float) atIndex:7];

			/* Dispatch: threadgroups cover output [batch, M, N] */
			uint    TILE_SIZE       = 16;
			MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
			MTLSize numGroups       = MTLSizeMake((seq_k + TILE_SIZE - 1) / TILE_SIZE, (seq_q + TILE_SIZE - 1) / TILE_SIZE, heads);

			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
			[encoder endEncoding];
		}

		/* === Phase 2: Softmax on GPU === */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_softmax_half_pipeline];

			[encoder setBuffer:bufScores offset:0 atIndex:0];
			int total_rows = heads * seq_q;
			[encoder setBytes:&total_rows length:sizeof(int) atIndex:1];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:2];

			NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
			MTLSize    numGroups       = MTLSizeMake(total_rows, 1, 1);
			MTLSize    groupSize       = MTLSizeMake(threadsPerGroup, 1, 1);

			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:groupSize];
			[encoder endEncoding];
		}

		/* === Phase 3: scores @ V using custom half compute shader === */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_bmm_half_sv_pipeline];

			[encoder setBuffer:bufScores offset:0 atIndex:0];
			[encoder setBuffer:bufV offset:0 atIndex:1];
			[encoder setBuffer:bufOut offset:0 atIndex:2];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:3];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:4];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
			[encoder setBytes:&heads length:sizeof(int) atIndex:6];

			/* Dispatch: threadgroups cover output [batch, M, N] */
			uint    TILE_SIZE       = 16;
			MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
			MTLSize numGroups       = MTLSizeMake((head_dim + TILE_SIZE - 1) / TILE_SIZE, (seq_q + TILE_SIZE - 1) / TILE_SIZE, heads);

			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
			[encoder endEncoding];
		}

		/* Execute all phases */
		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		/* Convert f16 output back to f32 */
		uint16_t *out_f16 = (uint16_t *)[bufOut contents];
		for (size_t i = 0; i < out_elements; i++) {
			out[i] = f16_to_f32(out_f16[i]);
		}

		/* Release pooled buffers */
		pool_release_buffer(bufQ);
		pool_release_buffer(bufK);
		pool_release_buffer(bufV);
		pool_release_buffer(bufScores);
		pool_release_buffer(bufOut);
	}
}

/* ========================================================================
 * Native BF16 Attention (no conversion overhead)
 *
 * This function works with native bf16 GPU tensors - no f32<->bf16 conversion
 * is performed. The bf16 data stays in bf16 format throughout, with f32
 * accumulation only in the compute shaders.
 *
 * Q, K, V: GPU buffers containing bf16 data [heads, seq_q/seq_k, head_dim]
 * out: GPU buffer to write bf16 result [heads, seq_q, head_dim]
 * ======================================================================== */
void iris_metal_attention_bf16_native(id<MTLBuffer> bufQ, id<MTLBuffer> bufK, id<MTLBuffer> bufV, id<MTLBuffer> bufOut, int heads, int seq_q, int seq_k,
                                      int head_dim, float scale) {
	if (!g_initialized || heads <= 0)
		return;
	if (!g_shaders_initialized || !g_bmm_bf16_qkt_pipeline || !g_bmm_bf16_sv_pipeline || !g_softmax_bf16_pipeline) {
		fprintf(stderr, "BF16 attention: shaders not available\n");
		return;
	}

	@autoreleasepool {
		size_t scores_elements = (size_t)heads * seq_q * seq_k;
		size_t scores_size     = scores_elements * sizeof(uint16_t); /* bf16 = 2 bytes */

		id<MTLBuffer> bufScores = pool_get_buffer(scores_size);
		if (!bufScores)
			return;

		id<MTLCommandBuffer> cmdBuffer = [g_queue commandBuffer];

		/* === Phase 1: Q @ K^T using bf16 compute shader === */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_bmm_bf16_qkt_pipeline];

			[encoder setBuffer:bufQ offset:0 atIndex:0];
			[encoder setBuffer:bufK offset:0 atIndex:1];
			[encoder setBuffer:bufScores offset:0 atIndex:2];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:3];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:4];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
			[encoder setBytes:&heads length:sizeof(int) atIndex:6];
			[encoder setBytes:&scale length:sizeof(float) atIndex:7];

			uint    TILE_SIZE       = 16;
			MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
			MTLSize numGroups       = MTLSizeMake((seq_k + TILE_SIZE - 1) / TILE_SIZE, (seq_q + TILE_SIZE - 1) / TILE_SIZE, heads);

			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
			[encoder endEncoding];
		}

		/* === Phase 2: Softmax on GPU (bf16) === */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_softmax_bf16_pipeline];

			[encoder setBuffer:bufScores offset:0 atIndex:0];
			int total_rows = heads * seq_q;
			[encoder setBytes:&total_rows length:sizeof(int) atIndex:1];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:2];

			NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
			MTLSize    numGroups       = MTLSizeMake(total_rows, 1, 1);
			MTLSize    groupSize       = MTLSizeMake(threadsPerGroup, 1, 1);

			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:groupSize];
			[encoder endEncoding];
		}

		/* === Phase 3: scores @ V using bf16 compute shader === */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_bmm_bf16_sv_pipeline];

			[encoder setBuffer:bufScores offset:0 atIndex:0];
			[encoder setBuffer:bufV offset:0 atIndex:1];
			[encoder setBuffer:bufOut offset:0 atIndex:2];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:3];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:4];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
			[encoder setBytes:&heads length:sizeof(int) atIndex:6];

			uint    TILE_SIZE       = 16;
			MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
			MTLSize numGroups       = MTLSizeMake((head_dim + TILE_SIZE - 1) / TILE_SIZE, (seq_q + TILE_SIZE - 1) / TILE_SIZE, heads);

			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
			[encoder endEncoding];
		}

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		pool_release_buffer(bufScores);
	}
}

/* ========================================================================
 * BF16 Pipeline API
 *
 * These functions work with native bf16 GPU tensors to implement the full
 * bf16 pipeline. All operations keep data in bf16
 * with f32 accumulation internally for numerical stability.
 * ======================================================================== */

/* Check if bf16 pipeline is available */
int iris_bf16_pipeline_available(void) {
	int ok = g_shaders_initialized && g_rms_norm_bf16_pipeline && g_qk_rms_norm_bf16_pipeline && g_adaln_norm_bf16_pipeline && g_silu_mul_bf16_pipeline &&
	         g_gated_add_bf16_pipeline && g_rope_unified_bf16_pipeline && g_rope_2d_bf16_pipeline && g_bmm_bf16_qkt_pipeline && g_bmm_bf16_sv_pipeline &&
	         g_softmax_bf16_pipeline && g_linear_bf16_pipeline && g_concat_seq_bf16_pipeline && g_slice_seq_bf16_pipeline && g_attention_fused_bf16_pipeline &&
	         g_f32_to_bf16_pipeline && g_bf16_to_f32_pipeline;

	if (!ok && bf16_debug_enabled()) {
		static int reported = 0;
		if (!reported) {
			reported = 1;
			if (!g_shaders_initialized)
				fprintf(stderr, "[BF16] shaders not initialized\n");
			if (!g_rms_norm_bf16_pipeline)
				fprintf(stderr, "[BF16] missing rms_norm_bf16\n");
			if (!g_qk_rms_norm_bf16_pipeline)
				fprintf(stderr, "[BF16] missing qk_rms_norm_bf16\n");
			if (!g_adaln_norm_bf16_pipeline)
				fprintf(stderr, "[BF16] missing adaln_norm_bf16\n");
			if (!g_silu_mul_bf16_pipeline)
				fprintf(stderr, "[BF16] missing silu_mul_bf16\n");
			if (!g_gated_add_bf16_pipeline)
				fprintf(stderr, "[BF16] missing gated_add_bf16\n");
			if (!g_rope_unified_bf16_pipeline)
				fprintf(stderr, "[BF16] missing rope_unified_bf16\n");
			if (!g_rope_2d_bf16_pipeline)
				fprintf(stderr, "[BF16] missing rope_2d_bf16\n");
			if (!g_bmm_bf16_qkt_pipeline)
				fprintf(stderr, "[BF16] missing bmm_bf16_qkt\n");
			if (!g_bmm_bf16_sv_pipeline)
				fprintf(stderr, "[BF16] missing bmm_bf16_sv\n");
			if (!g_softmax_bf16_pipeline)
				fprintf(stderr, "[BF16] missing softmax_bf16\n");
			if (!g_linear_bf16_pipeline)
				fprintf(stderr, "[BF16] missing linear_bf16\n");
			if (!g_concat_seq_bf16_pipeline)
				fprintf(stderr, "[BF16] missing concat_seq_bf16\n");
			if (!g_slice_seq_bf16_pipeline)
				fprintf(stderr, "[BF16] missing slice_seq_bf16\n");
			if (!g_attention_fused_bf16_pipeline) {
				fprintf(stderr, "[BF16] missing fused attention pipeline\n");
			}
			if (!g_f32_to_bf16_pipeline)
				fprintf(stderr, "[BF16] missing f32_to_bf16\n");
			if (!g_bf16_to_f32_pipeline)
				fprintf(stderr, "[BF16] missing bf16_to_f32\n");
		}
	}

	return ok;
}

/* Convert f32 tensor to bf16 on GPU */
void iris_bf16_convert_f32_to_bf16(id<MTLBuffer> input_f32, id<MTLBuffer> output_bf16, int n) {
	if (!g_shaders_initialized || !g_f32_to_bf16_pipeline)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_f32_to_bf16_pipeline];
		[encoder setBuffer:input_f32 offset:0 atIndex:0];
		[encoder setBuffer:output_bf16 offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* Convert bf16 tensor to f32 on GPU */
void iris_bf16_convert_bf16_to_f32(id<MTLBuffer> input_bf16, id<MTLBuffer> output_f32, int n) {
	if (!g_shaders_initialized || !g_bf16_to_f32_pipeline)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_bf16_to_f32_pipeline];
		[encoder setBuffer:input_bf16 offset:0 atIndex:0];
		[encoder setBuffer:output_f32 offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* RMSNorm on bf16 tensors */
void iris_bf16_rms_norm(id<MTLBuffer> out, id<MTLBuffer> x, id<MTLBuffer> weight, int seq_len, int hidden, float eps) {
	if (!g_shaders_initialized || !g_rms_norm_bf16_pipeline)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rms_norm_bf16_pipeline];
		[encoder setBuffer:x offset:0 atIndex:0];
		[encoder setBuffer:weight offset:0 atIndex:1];
		[encoder setBuffer:out offset:0 atIndex:2];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:3];
		[encoder setBytes:&eps length:sizeof(float) atIndex:4];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq_len, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* QK RMSNorm on bf16 tensors (in-place) */
void iris_bf16_qk_rms_norm(id<MTLBuffer> q, id<MTLBuffer> k, id<MTLBuffer> q_weight, id<MTLBuffer> k_weight, int seq, int heads, int head_dim, float eps) {
	if (!g_shaders_initialized || !g_qk_rms_norm_bf16_pipeline)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_qk_rms_norm_bf16_pipeline];
		[encoder setBuffer:q offset:0 atIndex:0];
		[encoder setBuffer:k offset:0 atIndex:1];
		[encoder setBuffer:q_weight offset:0 atIndex:2];
		[encoder setBuffer:k_weight offset:0 atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&eps length:sizeof(float) atIndex:6];

		[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* SiLU on bf16 tensors (in-place) */
void iris_bf16_silu(id<MTLBuffer> x, int n) {
	if (!g_shaders_initialized || !g_silu_bf16_pipeline)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_silu_bf16_pipeline];
		[encoder setBuffer:x offset:0 atIndex:0];
		[encoder setBytes:&n length:sizeof(int) atIndex:1];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* SiLU with multiply on bf16 tensors: gate = silu(gate) * up */
void iris_bf16_silu_mul(id<MTLBuffer> gate, id<MTLBuffer> up, int n) {
	if (!g_shaders_initialized || !g_silu_mul_bf16_pipeline)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_silu_mul_bf16_pipeline];
		[encoder setBuffer:gate offset:0 atIndex:0];
		[encoder setBuffer:up offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* RoPE on bf16 tensors */
void iris_bf16_rope_unified(id<MTLBuffer> x, const float *txt_cos, const float *txt_sin, const float *img_cos, const float *img_sin, int seq, int img_offset,
                            int heads, int head_dim) {
	if (!g_shaders_initialized || !g_rope_unified_bf16_pipeline)
		return;

	@autoreleasepool {
		/* Get cached frequency buffers */
		int    txt_len  = img_offset;
		int    img_len  = seq - img_offset;
		size_t txt_size = (size_t)txt_len * head_dim * sizeof(float);
		size_t img_size = (size_t)img_len * head_dim * sizeof(float);

		id<MTLBuffer> bufTxtCos = get_rope_buffer(txt_cos, txt_size);
		id<MTLBuffer> bufTxtSin = get_rope_buffer(txt_sin, txt_size);
		id<MTLBuffer> bufImgCos = get_rope_buffer(img_cos, img_size);
		id<MTLBuffer> bufImgSin = get_rope_buffer(img_sin, img_size);

		if (!bufTxtCos || !bufTxtSin || !bufImgCos || !bufImgSin)
			return;

		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		int axis_dim = 32; /* FLUX uses 32 */
		[encoder setComputePipelineState:g_rope_unified_bf16_pipeline];
		[encoder setBuffer:x offset:0 atIndex:0];
		[encoder setBuffer:bufTxtCos offset:0 atIndex:1];
		[encoder setBuffer:bufTxtSin offset:0 atIndex:2];
		[encoder setBuffer:bufImgCos offset:0 atIndex:3];
		[encoder setBuffer:bufImgSin offset:0 atIndex:4];
		[encoder setBytes:&seq length:sizeof(int) atIndex:5];
		[encoder setBytes:&img_offset length:sizeof(int) atIndex:6];
		[encoder setBytes:&heads length:sizeof(int) atIndex:7];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
		[encoder setBytes:&axis_dim length:sizeof(int) atIndex:9];

		[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];
	}
}

/* GPU Tensor API Implementation
 * ======================================================================== */

/* Internal tensor structure */
struct iris_gpu_tensor {
	id<MTLBuffer> buffer;
	size_t        num_elements;
	int           has_pending_work; /* Flag to track if GPU work is pending */
	int           persistent;       /* If set, don't release to pool on free */
	int           is_f16;           /* 1 if float16, 0 if float32 */
};

/* Pending command buffer for batched operations */
static id<MTLCommandBuffer> g_tensor_cmd        = nil;
static int                  g_tensor_batch_mode = 0;

/* Chain mode - keep data on GPU between operations */
static id<MTLCommandBuffer> g_chain_cmd  = nil;
static int                  g_chain_mode = 0;

static int tensor_batch_active(void) {
	return g_tensor_batch_mode;
}

static int tensor_chain_active(void) {
	return g_chain_mode;
}

iris_gpu_tensor_t iris_gpu_tensor_create(const float *data, size_t num_elements) {
	if (!g_initialized || !data || num_elements == 0)
		return NULL;

	@autoreleasepool {
		size_t size = num_elements * sizeof(float);

		/* Get buffer from pool */
		id<MTLBuffer> buf = pool_get_buffer(size);
		if (!buf)
			return NULL;

		/* Copy data to buffer (shared memory - this is fast on Apple Silicon) */
		memcpy([buf contents], data, size);

		/* Allocate tensor structure */
		iris_gpu_tensor_t tensor = (iris_gpu_tensor_t)malloc(sizeof(struct iris_gpu_tensor));
		if (!tensor) {
			pool_release_buffer(buf);
			return NULL;
		}

		tensor->buffer           = buf;
		tensor->num_elements     = num_elements;
		tensor->has_pending_work = 0;
		tensor->persistent       = 0;
		tensor->is_f16           = 0;

		return tensor;
	}
}

iris_gpu_tensor_t iris_gpu_tensor_alloc(size_t num_elements) {
	if (!g_initialized || num_elements == 0)
		return NULL;

	@autoreleasepool {
		size_t size = num_elements * sizeof(float);

		/* Get buffer from pool */
		id<MTLBuffer> buf = pool_get_buffer(size);
		if (!buf)
			return NULL;

		memset([buf contents], 0, size);

		/* Allocate tensor structure */
		iris_gpu_tensor_t tensor = (iris_gpu_tensor_t)malloc(sizeof(struct iris_gpu_tensor));
		if (!tensor) {
			pool_release_buffer(buf);
			return NULL;
		}

		tensor->buffer           = buf;
		tensor->num_elements     = num_elements;
		tensor->has_pending_work = 0;
		tensor->persistent       = 0;
		tensor->is_f16           = 0;

		return tensor;
	}
}

/* Allocate f16 GPU tensor (half the memory of f32) */
iris_gpu_tensor_t iris_gpu_tensor_alloc_f16(size_t num_elements) {
	if (!g_initialized || num_elements == 0)
		return NULL;

	@autoreleasepool {
		size_t size = num_elements * sizeof(uint16_t); /* f16 = 2 bytes */

		id<MTLBuffer> buf = pool_get_buffer(size);
		if (!buf)
			return NULL;

		if (tensor_zero_init_enabled()) {
			memset([buf contents], 0, size);
		}

		iris_gpu_tensor_t tensor = (iris_gpu_tensor_t)malloc(sizeof(struct iris_gpu_tensor));
		if (!tensor) {
			pool_release_buffer(buf);
			return NULL;
		}

		tensor->buffer           = buf;
		tensor->num_elements     = num_elements;
		tensor->has_pending_work = 0;
		tensor->persistent       = 0;
		tensor->is_f16           = 1;

		return tensor;
	}
}

iris_gpu_tensor_t iris_gpu_tensor_alloc_persistent(size_t num_elements) {
	if (!g_initialized || num_elements == 0)
		return NULL;

	@autoreleasepool {
		size_t size = num_elements * sizeof(float);

		/* Allocate directly (not from pool) so it's not reclaimed */
		id<MTLBuffer> buf = [g_device newBufferWithLength:size options:MTLResourceStorageModeShared];
		if (!buf)
			return NULL;

		if (tensor_zero_init_enabled()) {
			memset([buf contents], 0, size);
		}

		/* Allocate tensor structure */
		iris_gpu_tensor_t tensor = (iris_gpu_tensor_t)malloc(sizeof(struct iris_gpu_tensor));
		if (!tensor) {
			return NULL;
		}

		tensor->buffer           = buf;
		tensor->num_elements     = num_elements;
		tensor->has_pending_work = 0;
		tensor->persistent       = 1; /* Mark as persistent */
		tensor->is_f16           = 0;

		return tensor;
	}
}

void iris_gpu_tensor_set_persistent(iris_gpu_tensor_t tensor, int persistent) {
	if (tensor) {
		tensor->persistent = persistent;
	}
}

void iris_gpu_tensor_read(iris_gpu_tensor_t tensor, float *out) {
	if (!tensor || !out)
		return;

	/* If there's pending work, sync first */
	if (tensor->has_pending_work) {
		iris_gpu_sync();
		tensor->has_pending_work = 0;
	}

	/* Copy from shared memory buffer */
	size_t size = tensor->num_elements * sizeof(float);
	memcpy(out, [tensor->buffer contents], size);
}

void iris_gpu_tensor_write(iris_gpu_tensor_t tensor, const float *data) {
	if (!tensor || !data)
		return;

	/* If there's pending work that reads this tensor, sync first */
	if (tensor->has_pending_work) {
		iris_gpu_sync();
		tensor->has_pending_work = 0;
	}

	/* Copy to shared memory buffer */
	size_t elem_size = tensor->is_f16 ? sizeof(uint16_t) : sizeof(float);
	size_t size      = tensor->num_elements * elem_size;
	memcpy([tensor->buffer contents], data, size);
}

float *iris_gpu_tensor_data(iris_gpu_tensor_t tensor) {
	if (!tensor)
		return NULL;
	return (float *)[tensor->buffer contents];
}

void iris_gpu_tensor_free(iris_gpu_tensor_t tensor) {
	if (!tensor)
		return;

	if (tensor->persistent) {
		/* Persistent tensors are not pooled - just release under ARC */
		tensor->buffer = nil;
	}
	else {
		/* Release buffer back to pool */
		pool_release_buffer(tensor->buffer);
		tensor->buffer = nil;
	}

	free(tensor);
}

size_t iris_gpu_tensor_size(iris_gpu_tensor_t tensor) {
	if (!tensor)
		return 0;
	return tensor->num_elements;
}

/* Get the underlying Metal buffer from a GPU tensor (for use with bf16 API) */
id<MTLBuffer> iris_gpu_tensor_get_buffer(iris_gpu_tensor_t tensor) {
	if (!tensor)
		return nil;
	return tensor->buffer;
}

/* Check if tensor is bf16/f16 format */
int iris_gpu_tensor_is_f16(iris_gpu_tensor_t tensor) {
	if (!tensor)
		return 0;
	return tensor->is_f16;
}

void iris_gpu_sync(void) {
	if (!g_initialized)
		return;

	@autoreleasepool {
		if (g_tensor_cmd) {
			[g_tensor_cmd commit];
			[g_tensor_cmd waitUntilCompleted];
			g_tensor_cmd = nil;
			pool_flush_deferred();
		}
		/* If in batch mode, create a new command buffer so subsequent
		 * operations continue to be batched properly. Without this,
		 * ops after sync would create orphan buffers that never execute. */
		if (g_tensor_batch_mode) {
			g_tensor_cmd = [g_queue commandBuffer];
		}
	}
}

void iris_gpu_batch_begin(void) {
	if (!g_initialized || g_tensor_batch_mode)
		return;

	@autoreleasepool {
		pool_flush_deferred();
		g_tensor_cmd        = [g_queue commandBuffer];
		g_tensor_batch_mode = 1;
	}
}

void iris_gpu_batch_end(void) {
	if (!g_initialized || !g_tensor_batch_mode)
		return;

	@autoreleasepool {
		if (g_tensor_cmd) {
			if (g_tensor_cmd.status < MTLCommandBufferStatusCommitted) {
				[g_tensor_cmd commit];
			}
			[g_tensor_cmd waitUntilCompleted];
			g_tensor_cmd = nil;
		}
		pool_flush_deferred();
		g_tensor_batch_mode = 0;
	}
}

/* ========================================================================
 * Operation Chain API - Keep data on GPU between operations
 * ======================================================================== */

void iris_gpu_chain_begin(void) {
	if (!g_initialized || g_chain_mode)
		return;

	@autoreleasepool {
		g_chain_cmd  = [g_queue commandBuffer];
		g_chain_mode = 1;
	}
}

void iris_gpu_chain_end(void) {
	if (!g_initialized || !g_chain_mode)
		return;

	@autoreleasepool {
		if (g_chain_cmd) {
			[g_chain_cmd commit];
			[g_chain_cmd waitUntilCompleted];
			g_chain_cmd = nil;
		}
		pool_flush_deferred();
		g_chain_mode = 0;
	}
}

int iris_gpu_in_chain(void) {
	return g_chain_mode;
}

/* Get or create command buffer for tensor operations */
static id<MTLCommandBuffer> get_tensor_cmd(void) {
	/* Prefer chain mode command buffer if active */
	if (g_chain_mode && g_chain_cmd) {
		return g_chain_cmd;
	}
	if (g_tensor_batch_mode) {
		/* MPSGraph may commit our buffer internally. Detect and replace. */
		if (g_tensor_cmd && g_tensor_cmd.status < MTLCommandBufferStatusCommitted) {
			return g_tensor_cmd;
		}
		g_tensor_cmd = [g_queue commandBuffer];
		return g_tensor_cmd;
	}
	return [g_queue commandBuffer];
}

iris_gpu_tensor_t iris_gpu_linear(iris_gpu_tensor_t x, const float *W, const float *b, int seq_len, int in_dim, int out_dim) {
	if (!g_initialized || !x || !W)
		return NULL;

	@autoreleasepool {
		size_t            out_elements = (size_t)seq_len * out_dim;
		iris_gpu_tensor_t out          = iris_gpu_tensor_alloc(out_elements);
		if (!out)
			return NULL;

		/* Get weight buffer (cached) */
		size_t        sizeW = (size_t)out_dim * in_dim * sizeof(float);
		id<MTLBuffer> bufW  = get_cached_weight_buffer(W, sizeW);
		if (!bufW) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		/* Create matrix descriptors
		 * x: [seq_len, in_dim]
		 * W: [out_dim, in_dim] (need to transpose for x @ W^T)
		 * out: [seq_len, out_dim]
		 */
		MPSMatrixDescriptor *descX   = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(float)
                                                                          dataType:MPSDataTypeFloat32];
		MPSMatrixDescriptor *descW   = [MPSMatrixDescriptor matrixDescriptorWithRows:out_dim
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(float)
                                                                          dataType:MPSDataTypeFloat32];
		MPSMatrixDescriptor *descOut = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
		                                                                     columns:out_dim
		                                                                    rowBytes:out_dim * sizeof(float)
		                                                                    dataType:MPSDataTypeFloat32];

		MPSMatrix *matX   = [[MPSMatrix alloc] initWithBuffer:x->buffer descriptor:descX];
		MPSMatrix *matW   = [[MPSMatrix alloc] initWithBuffer:bufW descriptor:descW];
		MPSMatrix *matOut = [[MPSMatrix alloc] initWithBuffer:out->buffer descriptor:descOut];

		/* Create matmul: out = x @ W^T */
		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:NO
		                                                                   transposeRight:YES
		                                                                       resultRows:seq_len
		                                                                    resultColumns:out_dim
		                                                                  interiorColumns:in_dim
		                                                                            alpha:1.0f
		                                                                             beta:0.0f];

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matX rightMatrix:matW resultMatrix:matOut];

		/* Add bias if present */
		if (b != NULL) {
			/* For now, sync and add bias on CPU (can optimize later with compute shader) */
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];

			float *out_data = (float *)[out->buffer contents];
			for (int i = 0; i < seq_len; i++) {
				for (int j = 0; j < out_dim; j++) {
					out_data[i * out_dim + j] += b[j];
				}
			}
			out->has_pending_work = 0;
		}
		else {
			/* Mark output as having pending work */
			out->has_pending_work = 1;

			if (!g_tensor_batch_mode) {
				/* Not in batch mode - sync immediately */
				[cmdBuffer commit];
				[cmdBuffer waitUntilCompleted];
				out->has_pending_work = 0;
			}
		}

		/* Mark input as having pending work if in batch mode */
		if (g_tensor_batch_mode) {
			x->has_pending_work = 1;
		}

		return out;
	}
}

/* GPU linear with bf16 weights - returns GPU tensor (stays on GPU) */
iris_gpu_tensor_t iris_gpu_linear_bf16(iris_gpu_tensor_t x, const uint16_t *W_bf16, int seq_len, int in_dim, int out_dim) {
	if (!g_initialized || !x || !W_bf16)
		return NULL;

	@autoreleasepool {
		size_t            out_elements = (size_t)seq_len * out_dim;
		iris_gpu_tensor_t out          = iris_gpu_tensor_alloc(out_elements);
		if (!out)
			return NULL;

		/* Get cached f16 weight buffer (bf16 converted to f16) */
		size_t        numW = (size_t)out_dim * in_dim;
		id<MTLBuffer> bufW = get_cached_bf16_as_f16_buffer(W_bf16, numW);
		if (!bufW) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		/* Create matrix descriptors
		 * x: [seq_len, in_dim] (f32)
		 * W: [out_dim, in_dim] (f16, need to transpose for x @ W^T)
		 * out: [seq_len, out_dim] (f32)
		 */
		MPSMatrixDescriptor *descX   = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(float)
                                                                          dataType:MPSDataTypeFloat32];
		MPSMatrixDescriptor *descW   = [MPSMatrixDescriptor matrixDescriptorWithRows:out_dim
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(uint16_t)
                                                                          dataType:MPSDataTypeFloat16];
		MPSMatrixDescriptor *descOut = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
		                                                                     columns:out_dim
		                                                                    rowBytes:out_dim * sizeof(float)
		                                                                    dataType:MPSDataTypeFloat32];

		MPSMatrix *matX   = [[MPSMatrix alloc] initWithBuffer:x->buffer descriptor:descX];
		MPSMatrix *matW   = [[MPSMatrix alloc] initWithBuffer:bufW descriptor:descW];
		MPSMatrix *matOut = [[MPSMatrix alloc] initWithBuffer:out->buffer descriptor:descOut];

		/* Create matmul: out = x @ W^T */
		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:NO
		                                                                   transposeRight:YES
		                                                                       resultRows:seq_len
		                                                                    resultColumns:out_dim
		                                                                  interiorColumns:in_dim
		                                                                            alpha:1.0f
		                                                                             beta:0.0f];

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matX rightMatrix:matW resultMatrix:matOut];

		/* Mark output as having pending work */
		out->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			/* Not in batch mode - sync immediately */
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
		}

		/* Mark input as having pending work if in batch mode */
		if (g_tensor_batch_mode) {
			x->has_pending_work = 1;
		}

		return out;
	}
}

/* GPU linear with bf16 weights - writes to pre-allocated output tensor.
 * Same as iris_gpu_linear_bf16 but avoids allocation overhead by reusing buffers. */
int iris_gpu_linear_bf16_into(iris_gpu_tensor_t out, iris_gpu_tensor_t x, const uint16_t *W_bf16, int seq_len, int in_dim, int out_dim) {
	if (!g_initialized || !out || !x || !W_bf16)
		return 0;

	@autoreleasepool {
		/* Verify output buffer is large enough */
		size_t out_elements = (size_t)seq_len * out_dim;
		if (out->num_elements < out_elements)
			return 0;

		/* Get cached f16 weight buffer (bf16 converted to f16) */
		size_t        numW = (size_t)out_dim * in_dim;
		id<MTLBuffer> bufW = get_cached_bf16_as_f16_buffer(W_bf16, numW);
		if (!bufW)
			return 0;

		/* Create matrix descriptors */
		MPSMatrixDescriptor *descX   = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(float)
                                                                          dataType:MPSDataTypeFloat32];
		MPSMatrixDescriptor *descW   = [MPSMatrixDescriptor matrixDescriptorWithRows:out_dim
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(uint16_t)
                                                                          dataType:MPSDataTypeFloat16];
		MPSMatrixDescriptor *descOut = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
		                                                                     columns:out_dim
		                                                                    rowBytes:out_dim * sizeof(float)
		                                                                    dataType:MPSDataTypeFloat32];

		MPSMatrix *matX   = [[MPSMatrix alloc] initWithBuffer:x->buffer descriptor:descX];
		MPSMatrix *matW   = [[MPSMatrix alloc] initWithBuffer:bufW descriptor:descW];
		MPSMatrix *matOut = [[MPSMatrix alloc] initWithBuffer:out->buffer descriptor:descOut];

		/* Create matmul: out = x @ W^T */
		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:NO
		                                                                   transposeRight:YES
		                                                                       resultRows:seq_len
		                                                                    resultColumns:out_dim
		                                                                  interiorColumns:in_dim
		                                                                            alpha:1.0f
		                                                                             beta:0.0f];

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matX rightMatrix:matW resultMatrix:matOut];

		/* Mark tensors as having pending work */
		out->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
		}

		if (g_tensor_batch_mode) {
			x->has_pending_work = 1;
		}

		return 1;
	}
}

/* GPU linear with bf16 weights - outputs bf16 tensor for full bf16 pipeline
 * Uses native bf16 (MPSDataTypeBFloat16). */
iris_gpu_tensor_t iris_gpu_linear_bf16_bf16out(iris_gpu_tensor_t x, const uint16_t *W_bf16, int seq_len, int in_dim, int out_dim) {
	if (!g_initialized || !x || !W_bf16)
		return NULL;

	@autoreleasepool {
		size_t            out_elements = (size_t)seq_len * out_dim;
		iris_gpu_tensor_t out          = iris_gpu_tensor_alloc_f16(out_elements); /* Same size as bf16 */
		if (!out)
			return NULL;

		/* Get cached bf16 weight buffer (native, no conversion) */
		size_t        numW = (size_t)out_dim * in_dim;
		id<MTLBuffer> bufW = get_cached_bf16_buffer(W_bf16, numW);
		if (!bufW) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		/* Determine input dtype - for bf16 pipeline, input should also be bf16 */
		int x_is_f16 = x->is_f16; /* In bf16 mode, this means bf16 */

		/* Create matrix descriptors using native bf16
		 * x: [seq_len, in_dim] (f32 or bf16)
		 * W: [out_dim, in_dim] (bf16)
		 * out: [seq_len, out_dim] (bf16)
		 */
		MPSMatrixDescriptor *descX   = [MPSMatrixDescriptor
            matrixDescriptorWithRows:seq_len
                             columns:in_dim
                            rowBytes:in_dim * (x_is_f16 ? sizeof(uint16_t) : sizeof(float))dataType:x_is_f16 ? MPSDataTypeBFloat16 : MPSDataTypeFloat32];
		MPSMatrixDescriptor *descW   = [MPSMatrixDescriptor matrixDescriptorWithRows:out_dim
                                                                           columns:in_dim
                                                                          rowBytes:in_dim * sizeof(uint16_t)
                                                                          dataType:MPSDataTypeBFloat16];
		MPSMatrixDescriptor *descOut = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_len
		                                                                     columns:out_dim
		                                                                    rowBytes:out_dim * sizeof(uint16_t)
		                                                                    dataType:MPSDataTypeBFloat16];

		MPSMatrix *matX   = [[MPSMatrix alloc] initWithBuffer:x->buffer descriptor:descX];
		MPSMatrix *matW   = [[MPSMatrix alloc] initWithBuffer:bufW descriptor:descW];
		MPSMatrix *matOut = [[MPSMatrix alloc] initWithBuffer:out->buffer descriptor:descOut];

		MPSMatrixMultiplication *matmul = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
		                                                                    transposeLeft:NO
		                                                                   transposeRight:YES
		                                                                       resultRows:seq_len
		                                                                    resultColumns:out_dim
		                                                                  interiorColumns:in_dim
		                                                                            alpha:1.0f
		                                                                             beta:0.0f];

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		[matmul encodeToCommandBuffer:cmdBuffer leftMatrix:matX rightMatrix:matW resultMatrix:matOut];

		out->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
		}

		if (g_tensor_batch_mode) {
			x->has_pending_work = 1;
		}

		return out;
	}
}

/* ========================================================================
 * Half-Precision MPS Attention (for GPU tensors)
 *
 * MPS attention for f16 GPU tensors. Uses MPSDataTypeFloat16 for
 * reduced memory bandwidth.
 *
 * Q: [seq_q, heads * head_dim] (f16)
 * K: [seq_k, heads * head_dim] (f16)
 * V: [seq_k, heads * head_dim] (f16)
 * out: [seq_q, heads * head_dim] (f16)
 * ======================================================================== */
int iris_gpu_attention_mps_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, int seq_q, int seq_k, int num_heads,
                                int head_dim, float scale) {
	if (!g_initialized || !out || !Q || !K || !V)
		return 0;
	if (!Q->is_f16 || !K->is_f16 || !V->is_f16 || !out->is_f16)
		return 0; /* is_f16 means 16-bit */

	@autoreleasepool {
		size_t hidden          = num_heads * head_dim;
		size_t scores_elements = (size_t)num_heads * seq_q * seq_k;
		size_t scores_size     = scores_elements * sizeof(uint16_t);

		/* Allocate scores buffer (f16) */
		id<MTLBuffer> bufScores = pool_get_buffer(scores_size);
		if (!bufScores)
			return 0;

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();

		/* === Phase 1: Q @ K^T for each head === */
		/* Q: [seq_q, heads * head_dim] -> treat as heads matrices of [seq_q, head_dim]
		 * K: [seq_k, heads * head_dim] -> treat as heads matrices of [seq_k, head_dim]
		 * scores: [heads, seq_q, seq_k] */
		{
			MPSMatrixDescriptor *descQ      = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
                                                                               columns:head_dim
                                                                              rowBytes:hidden * sizeof(uint16_t) /* stride to next row */
                                                                              dataType:MPSDataTypeFloat16];
			MPSMatrixDescriptor *descK      = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_k
                                                                               columns:head_dim
                                                                              rowBytes:hidden * sizeof(uint16_t)
                                                                              dataType:MPSDataTypeFloat16];
			MPSMatrixDescriptor *descScores = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
			                                                                        columns:seq_k
			                                                                       rowBytes:seq_k * sizeof(uint16_t)
			                                                                       dataType:MPSDataTypeFloat16];

			MPSMatrixMultiplication *matmul_qk = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
			                                                                       transposeLeft:NO
			                                                                      transposeRight:YES
			                                                                          resultRows:seq_q
			                                                                       resultColumns:seq_k
			                                                                     interiorColumns:head_dim
			                                                                               alpha:scale
			                                                                                beta:0.0f];

			for (int h = 0; h < num_heads; h++) {
				size_t q_offset = h * head_dim * sizeof(uint16_t);
				size_t k_offset = h * head_dim * sizeof(uint16_t);
				size_t s_offset = h * seq_q * seq_k * sizeof(uint16_t);

				MPSMatrix *matQ      = [[MPSMatrix alloc] initWithBuffer:Q->buffer offset:q_offset descriptor:descQ];
				MPSMatrix *matK      = [[MPSMatrix alloc] initWithBuffer:K->buffer offset:k_offset descriptor:descK];
				MPSMatrix *matScores = [[MPSMatrix alloc] initWithBuffer:bufScores offset:s_offset descriptor:descScores];

				[matmul_qk encodeToCommandBuffer:cmdBuffer leftMatrix:matQ rightMatrix:matK resultMatrix:matScores];
			}
		}

		/* === Phase 2: Softmax (convert f16->f32, softmax, f32->f16) === */
		/* Softmax needs higher precision internally, so we convert to f32 */
		{
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];

			/* Read f16 scores, convert to f32, softmax, convert back to f16 */
			size_t    f32_size   = scores_elements * sizeof(float);
			float    *scores_f32 = malloc(f32_size);
			uint16_t *scores_f16 = (uint16_t *)[bufScores contents];

			/* f16 to f32 conversion */
			for (size_t i = 0; i < scores_elements; i++) {
				scores_f32[i] = f16_to_f32(scores_f16[i]);
			}

			/* Softmax on CPU (f32 precision) */
			extern void iris_softmax(float *x, int rows, int cols);
			int         total_rows = num_heads * seq_q;
			iris_softmax(scores_f32, total_rows, seq_k);

			/* f32 to f16 conversion */
			for (size_t i = 0; i < scores_elements; i++) {
				scores_f16[i] = f32_to_f16(scores_f32[i]);
			}

			free(scores_f32);
			cmdBuffer = get_tensor_cmd();
		}

		/* === Phase 3: scores @ V for each head === */
		{
			MPSMatrixDescriptor *descScores = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
			                                                                        columns:seq_k
			                                                                       rowBytes:seq_k * sizeof(uint16_t)
			                                                                       dataType:MPSDataTypeFloat16];
			MPSMatrixDescriptor *descV      = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_k
                                                                               columns:head_dim
                                                                              rowBytes:hidden * sizeof(uint16_t)
                                                                              dataType:MPSDataTypeFloat16];
			MPSMatrixDescriptor *descOut    = [MPSMatrixDescriptor matrixDescriptorWithRows:seq_q
                                                                                 columns:head_dim
                                                                                rowBytes:hidden * sizeof(uint16_t)
                                                                                dataType:MPSDataTypeFloat16];

			MPSMatrixMultiplication *matmul_sv = [[MPSMatrixMultiplication alloc] initWithDevice:g_device
			                                                                       transposeLeft:NO
			                                                                      transposeRight:NO
			                                                                          resultRows:seq_q
			                                                                       resultColumns:head_dim
			                                                                     interiorColumns:seq_k
			                                                                               alpha:1.0f
			                                                                                beta:0.0f];

			for (int h = 0; h < num_heads; h++) {
				size_t s_offset = h * seq_q * seq_k * sizeof(uint16_t);
				size_t v_offset = h * head_dim * sizeof(uint16_t);
				size_t o_offset = h * head_dim * sizeof(uint16_t);

				MPSMatrix *matScores = [[MPSMatrix alloc] initWithBuffer:bufScores offset:s_offset descriptor:descScores];
				MPSMatrix *matV      = [[MPSMatrix alloc] initWithBuffer:V->buffer offset:v_offset descriptor:descV];
				MPSMatrix *matOut    = [[MPSMatrix alloc] initWithBuffer:out->buffer offset:o_offset descriptor:descOut];

				[matmul_sv encodeToCommandBuffer:cmdBuffer leftMatrix:matScores rightMatrix:matV resultMatrix:matOut];
			}
		}

		out->has_pending_work = 1;
		Q->has_pending_work   = 1;
		K->has_pending_work   = 1;
		V->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			Q->has_pending_work   = 0;
			K->has_pending_work   = 0;
			V->has_pending_work   = 0;
		}

		pool_release_buffer(bufScores);
		return 1;
	}
}

/* ========================================================================
 * Half-Precision Attention with f32 Tensor Interface
 *
 * Wrapper that takes f32 GPU tensors, converts to f16, does f16 attention,
 * and converts output back to f32. Provides 2x memory bandwidth savings
 * on attention matmuls while keeping the rest of the pipeline in f32.
 * ======================================================================== */
int iris_gpu_attention_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, int seq_q, int seq_k, int num_heads,
                            int head_dim, float scale) {
	if (!g_initialized || !out || !Q || !K || !V)
		return 0;

	@autoreleasepool {
		size_t q_elements   = (size_t)seq_q * num_heads * head_dim;
		size_t k_elements   = (size_t)seq_k * num_heads * head_dim;
		size_t v_elements   = (size_t)seq_k * num_heads * head_dim;
		size_t out_elements = (size_t)seq_q * num_heads * head_dim;

		/* Allocate f16 tensors */
		iris_gpu_tensor_t q_f16   = iris_gpu_tensor_alloc_f16(q_elements);
		iris_gpu_tensor_t k_f16   = iris_gpu_tensor_alloc_f16(k_elements);
		iris_gpu_tensor_t v_f16   = iris_gpu_tensor_alloc_f16(v_elements);
		iris_gpu_tensor_t out_f16 = iris_gpu_tensor_alloc_f16(out_elements);

		if (!q_f16 || !k_f16 || !v_f16 || !out_f16) {
			if (q_f16)
				iris_gpu_tensor_free(q_f16);
			if (k_f16)
				iris_gpu_tensor_free(k_f16);
			if (v_f16)
				iris_gpu_tensor_free(v_f16);
			if (out_f16)
				iris_gpu_tensor_free(out_f16);
			return 0;
		}

		/* Sync to ensure f32 tensors are ready */
		if (Q->has_pending_work || K->has_pending_work || V->has_pending_work) {
			iris_gpu_sync();
		}

		/* Convert f32 to f16 on CPU (fast on unified memory) */
		float    *q_f32      = (float *)[Q->buffer contents];
		float    *k_f32      = (float *)[K->buffer contents];
		float    *v_f32      = (float *)[V->buffer contents];
		uint16_t *q_f16_data = (uint16_t *)[q_f16->buffer contents];
		uint16_t *k_f16_data = (uint16_t *)[k_f16->buffer contents];
		uint16_t *v_f16_data = (uint16_t *)[v_f16->buffer contents];

		for (size_t i = 0; i < q_elements; i++) {
			q_f16_data[i] = f32_to_f16(q_f32[i]);
		}
		for (size_t i = 0; i < k_elements; i++) {
			k_f16_data[i] = f32_to_f16(k_f32[i]);
		}
		for (size_t i = 0; i < v_elements; i++) {
			v_f16_data[i] = f32_to_f16(v_f32[i]);
		}

		/* Do f16 attention */
		int success = iris_gpu_attention_mps_bf16(out_f16, q_f16, k_f16, v_f16, seq_q, seq_k, num_heads, head_dim, scale);

		if (success) {
			/* Sync and convert f16 output back to f32 */
			if (out_f16->has_pending_work) {
				iris_gpu_sync();
			}

			uint16_t *out_f16_data = (uint16_t *)[out_f16->buffer contents];
			float    *out_f32      = (float *)[out->buffer contents];

			for (size_t i = 0; i < out_elements; i++) {
				out_f32[i] = f16_to_f32(out_f16_data[i]);
			}
		}

		iris_gpu_tensor_free(q_f16);
		iris_gpu_tensor_free(k_f16);
		iris_gpu_tensor_free(v_f16);
		iris_gpu_tensor_free(out_f16);

		return success;
	}
}

/* ========================================================================
 * Compute Shader Support
 * Custom Metal compute shaders for element-wise operations.
 * ======================================================================== */

/* Compute pipeline states */
static id<MTLLibrary>              g_shader_library                 = nil;
static id<MTLComputePipelineState> g_rms_norm_pipeline              = nil;
static id<MTLComputePipelineState> g_qk_rms_norm_pipeline           = nil;
static id<MTLComputePipelineState> g_adaln_norm_pipeline            = nil;
static id<MTLComputePipelineState> g_silu_pipeline                  = nil;
static id<MTLComputePipelineState> g_silu_mul_pipeline              = nil;
static id<MTLComputePipelineState> g_softmax_pipeline               = nil;
static id<MTLComputePipelineState> g_rope_2d_pipeline               = nil;
static id<MTLComputePipelineState> g_rope_unified_pipeline          = nil;
static id<MTLComputePipelineState> g_causal_attention_pipeline      = nil;
static id<MTLComputePipelineState> g_causal_attention_bf16_pipeline = nil;
static id<MTLComputePipelineState> g_rope_bf16_pipeline             = nil;
static id<MTLComputePipelineState> g_head_rms_norm_bf16_pipeline    = nil;
static id<MTLComputePipelineState> g_attention_fused_pipeline       = nil;
static id<MTLComputePipelineState> g_gated_add_pipeline             = nil;
static id<MTLComputePipelineState> g_split_qkv_mlp_pipeline         = nil;
static id<MTLComputePipelineState> g_concat_attn_mlp_pipeline       = nil;
static id<MTLComputePipelineState> g_bmm_half_qkt_pipeline          = nil;
static id<MTLComputePipelineState> g_bmm_half_sv_pipeline           = nil;
static id<MTLComputePipelineState> g_softmax_half_pipeline          = nil;
/* Note: BF16 pipelines are forward-declared at top of file */

int iris_metal_shaders_available(void) {
	return g_shaders_initialized;
}

int iris_metal_init_shaders(void) {
	if (g_shaders_initialized)
		return 1;
	if (!g_initialized)
		return 0;

	@autoreleasepool {
		NSError *error = nil;

		/* Load shader source from embedded data */
		NSString *shaderSource = [[NSString alloc] initWithBytes:iris_shaders_metal length:iris_shaders_metal_len encoding:NSUTF8StringEncoding];
		if (!shaderSource) {
			fprintf(stderr, "Metal shaders: failed to decode embedded source\n");
			return 0;
		}

		/* Compile shader library */
		MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
		if (@available(macOS 15.0, *)) {
			options.mathMode = MTLMathModeFast;
		}
		else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			options.fastMathEnabled = YES;
#pragma clang diagnostic pop
		}
#else
		options.fastMathEnabled = YES;
#endif

		g_shader_library = [g_device newLibraryWithSource:shaderSource options:options error:&error];
		if (!g_shader_library) {
			fprintf(stderr, "Metal shaders: compilation failed: %s\n", [[error localizedDescription] UTF8String]);
			return 0;
		}

		/* Create compute pipeline states for each kernel */
		id<MTLFunction> func;

		func = [g_shader_library newFunctionWithName:@"rms_norm"];
		if (func) {
			g_rms_norm_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_rms_norm_pipeline) {
				fprintf(stderr, "Metal shaders: rms_norm pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"qk_rms_norm"];
		if (func) {
			g_qk_rms_norm_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_qk_rms_norm_pipeline) {
				fprintf(stderr, "Metal shaders: qk_rms_norm pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"adaln_norm"];
		if (func) {
			g_adaln_norm_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_adaln_norm_pipeline) {
				fprintf(stderr, "Metal shaders: adaln_norm pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"silu"];
		if (func) {
			g_silu_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_silu_pipeline) {
				fprintf(stderr, "Metal shaders: silu pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"silu_mul"];
		if (func) {
			g_silu_mul_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_silu_mul_pipeline) {
				fprintf(stderr, "Metal shaders: silu_mul pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"softmax"];
		if (func) {
			g_softmax_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_softmax_pipeline) {
				fprintf(stderr, "Metal shaders: softmax pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"apply_rope_2d"];
		if (func) {
			g_rope_2d_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_rope_2d_pipeline) {
				fprintf(stderr, "Metal shaders: apply_rope_2d pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"apply_rope_unified"];
		if (func) {
			g_rope_unified_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_rope_unified_pipeline) {
				fprintf(stderr, "Metal shaders: apply_rope_unified pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"causal_attention_fused"];
		if (func) {
			g_causal_attention_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_causal_attention_pipeline) {
				fprintf(stderr, "Metal shaders: causal_attention_fused pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"causal_attention_fused_bf16"];
		if (func) {
			g_causal_attention_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_causal_attention_bf16_pipeline) {
				fprintf(stderr, "Metal shaders: causal_attention_fused_bf16 pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"rope_bf16"];
		if (func) {
			g_rope_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_rope_bf16_pipeline) {
				fprintf(stderr, "Metal shaders: rope_bf16 pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"head_rms_norm_bf16"];
		if (func) {
			g_head_rms_norm_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_head_rms_norm_bf16_pipeline) {
				fprintf(stderr, "Metal shaders: head_rms_norm_bf16 pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"attention_fused"];
		if (func) {
			g_attention_fused_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_attention_fused_pipeline) {
				fprintf(stderr, "Metal shaders: attention_fused pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"gated_add"];
		if (func) {
			g_gated_add_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_gated_add_pipeline) {
				fprintf(stderr, "Metal shaders: gated_add pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"split_qkv_mlp"];
		if (func) {
			g_split_qkv_mlp_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_split_qkv_mlp_pipeline) {
				fprintf(stderr, "Metal shaders: split_qkv_mlp pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"concat_attn_mlp"];
		if (func) {
			g_concat_attn_mlp_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_concat_attn_mlp_pipeline) {
				fprintf(stderr, "Metal shaders: concat_attn_mlp pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"batched_matmul_half_qkt"];
		if (func) {
			g_bmm_half_qkt_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_bmm_half_qkt_pipeline) {
				fprintf(stderr, "Metal shaders: batched_matmul_half_qkt pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"batched_matmul_half_sv"];
		if (func) {
			g_bmm_half_sv_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_bmm_half_sv_pipeline) {
				fprintf(stderr, "Metal shaders: batched_matmul_half_sv pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		func = [g_shader_library newFunctionWithName:@"softmax_half"];
		if (func) {
			g_softmax_half_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
			if (!g_softmax_half_pipeline) {
				fprintf(stderr, "Metal shaders: softmax_half pipeline failed: %s\n", [[error localizedDescription] UTF8String]);
			}
		}

		/* BF16 native shaders */
		func = [g_shader_library newFunctionWithName:@"rms_norm_bf16"];
		if (func) {
			g_rms_norm_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"qk_rms_norm_bf16"];
		if (func) {
			g_qk_rms_norm_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"adaln_norm_bf16"];
		if (func) {
			g_adaln_norm_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"silu_bf16"];
		if (func) {
			g_silu_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"silu_mul_bf16"];
		if (func) {
			g_silu_mul_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"add_bf16"];
		if (func) {
			g_add_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"gated_add_bf16"];
		if (func) {
			g_gated_add_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"apply_rope_unified_bf16"];
		if (func) {
			g_rope_unified_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"apply_rope_2d_bf16"];
		if (func) {
			g_rope_2d_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"batched_matmul_bf16_qkt"];
		if (func) {
			g_bmm_bf16_qkt_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"batched_matmul_bf16_sv"];
		if (func) {
			g_bmm_bf16_sv_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"softmax_bf16"];
		if (func) {
			g_softmax_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"f32_to_bf16_convert"];
		if (func) {
			g_f32_to_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"bf16_to_f32_convert"];
		if (func) {
			g_bf16_to_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"linear_bf16"];
		if (func) {
			g_linear_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"split_qkv_mlp_bf16"];
		if (func) {
			g_split_qkv_mlp_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"concat_attn_mlp_bf16"];
		if (func) {
			g_concat_attn_mlp_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"concat_seq_bf16"];
		if (func) {
			g_concat_seq_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"slice_seq_bf16"];
		if (func) {
			g_slice_seq_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"transpose_to_heads_bf16"];
		if (func) {
			g_transpose_to_heads_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"transpose_from_heads_bf16"];
		if (func) {
			g_transpose_from_heads_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		func = [g_shader_library newFunctionWithName:@"attention_fused_bf16"];
		if (func) {
			g_attention_fused_bf16_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		/* F32 VAE shaders */
		func = [g_shader_library newFunctionWithName:@"group_norm_f32"];
		if (func) {
			g_group_norm_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}
		func = [g_shader_library newFunctionWithName:@"swish_f32"];
		if (func) {
			g_swish_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}
		func = [g_shader_library newFunctionWithName:@"add_f32"];
		if (func) {
			g_add_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}
		func = [g_shader_library newFunctionWithName:@"upsample_nearest_2x_f32"];
		if (func) {
			g_upsample_nearest_2x_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}
		func = [g_shader_library newFunctionWithName:@"leaky_relu_f32"];
		if (func) {
			g_leaky_relu_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}
		func = [g_shader_library newFunctionWithName:@"scale_add_f32"];
		if (func) {
			g_scale_add_f32_pipeline = [g_device newComputePipelineStateWithFunction:func error:&error];
		}

		g_shaders_initialized = 1;
		if (iris_verbose)
			fprintf(stderr, "Metal shaders: compute kernels loaded\n");
		return 1;
	}
}

/* Helper to encode a compute shader with common setup */
static id<MTLCommandBuffer> encode_compute_shader(void) {
	return g_in_batch ? g_batch_cmd : [g_queue commandBuffer];
}

void iris_metal_rms_norm(float *out, const float *x, const float *weight, int seq_len, int hidden, float eps) {
	if (!g_shaders_initialized || !g_rms_norm_pipeline)
		return;

	@autoreleasepool {
		size_t data_size   = (size_t)seq_len * hidden * sizeof(float);
		size_t weight_size = (size_t)hidden * sizeof(float);

		/* Create buffers */
		id<MTLBuffer> bufX      = pool_get_buffer(data_size);
		id<MTLBuffer> bufWeight = get_cached_weight_buffer(weight, weight_size);
		id<MTLBuffer> bufOut    = pool_get_buffer(data_size);

		if (!bufX || !bufWeight || !bufOut) {
			if (bufX)
				pool_release_buffer(bufX);
			if (bufOut)
				pool_release_buffer(bufOut);
			return;
		}

		memcpy([bufX contents], x, data_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rms_norm_pipeline];
		[encoder setBuffer:bufX offset:0 atIndex:0];
		[encoder setBuffer:bufWeight offset:0 atIndex:1];
		[encoder setBuffer:bufOut offset:0 atIndex:2];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:3];
		[encoder setBytes:&eps length:sizeof(float) atIndex:4];

		/* One threadgroup per row, 256 threads per group */
		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq_len, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		if (!g_in_batch) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			memcpy(out, [bufOut contents], data_size);
			pool_release_buffer(bufX);
			pool_release_buffer(bufOut);
		}
		else {
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufOut;
				g_pending_outputs[g_pending_count].cpu_ptr = out;
				g_pending_outputs[g_pending_count].size    = data_size;
				g_pending_count++;
			}
			pool_release_buffer(bufX);
		}
	}
}

void iris_metal_qk_rms_norm(float *q, float *k, const float *q_weight, const float *k_weight, int seq, int heads, int head_dim, float eps) {
	if (!g_shaders_initialized || !g_qk_rms_norm_pipeline)
		return;

	@autoreleasepool {
		size_t data_size   = (size_t)seq * heads * head_dim * sizeof(float);
		size_t weight_size = (size_t)head_dim * sizeof(float);

		/* Create buffers - Q and K are modified in-place */
		id<MTLBuffer> bufQ       = pool_get_buffer(data_size);
		id<MTLBuffer> bufK       = pool_get_buffer(data_size);
		id<MTLBuffer> bufQWeight = get_cached_weight_buffer(q_weight, weight_size);
		id<MTLBuffer> bufKWeight = get_cached_weight_buffer(k_weight, weight_size);

		if (!bufQ || !bufK || !bufQWeight || !bufKWeight) {
			if (bufQ)
				pool_release_buffer(bufQ);
			if (bufK)
				pool_release_buffer(bufK);
			return;
		}

		memcpy([bufQ contents], q, data_size);
		memcpy([bufK contents], k, data_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_qk_rms_norm_pipeline];
		[encoder setBuffer:bufQ offset:0 atIndex:0];
		[encoder setBuffer:bufK offset:0 atIndex:1];
		[encoder setBuffer:bufQWeight offset:0 atIndex:2];
		[encoder setBuffer:bufKWeight offset:0 atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&eps length:sizeof(float) atIndex:6];

		/* One thread per (seq_idx, head_idx) pair */
		[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];

		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		memcpy(q, [bufQ contents], data_size);
		memcpy(k, [bufK contents], data_size);

		pool_release_buffer(bufQ);
		pool_release_buffer(bufK);
	}
}

void iris_metal_adaln_norm(float *out, const float *x, const float *shift, const float *scale, int seq_len, int hidden, float eps) {
	if (!g_shaders_initialized || !g_adaln_norm_pipeline)
		return;

	@autoreleasepool {
		size_t data_size  = (size_t)seq_len * hidden * sizeof(float);
		size_t param_size = (size_t)hidden * sizeof(float);

		id<MTLBuffer> bufX     = pool_get_buffer(data_size);
		id<MTLBuffer> bufShift = pool_get_buffer(param_size);
		id<MTLBuffer> bufScale = pool_get_buffer(param_size);
		id<MTLBuffer> bufOut   = pool_get_buffer(data_size);

		if (!bufX || !bufShift || !bufScale || !bufOut) {
			if (bufX)
				pool_release_buffer(bufX);
			if (bufShift)
				pool_release_buffer(bufShift);
			if (bufScale)
				pool_release_buffer(bufScale);
			if (bufOut)
				pool_release_buffer(bufOut);
			return;
		}

		memcpy([bufX contents], x, data_size);
		memcpy([bufShift contents], shift, param_size);
		memcpy([bufScale contents], scale, param_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_adaln_norm_pipeline];
		[encoder setBuffer:bufX offset:0 atIndex:0];
		[encoder setBuffer:bufShift offset:0 atIndex:1];
		[encoder setBuffer:bufScale offset:0 atIndex:2];
		[encoder setBuffer:bufOut offset:0 atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];
		[encoder setBytes:&eps length:sizeof(float) atIndex:5];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq_len, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		if (!g_in_batch) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			memcpy(out, [bufOut contents], data_size);
			pool_release_buffer(bufX);
			pool_release_buffer(bufShift);
			pool_release_buffer(bufScale);
			pool_release_buffer(bufOut);
		}
		else {
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufOut;
				g_pending_outputs[g_pending_count].cpu_ptr = out;
				g_pending_outputs[g_pending_count].size    = data_size;
				g_pending_count++;
			}
			pool_release_buffer(bufX);
			pool_release_buffer(bufShift);
			pool_release_buffer(bufScale);
		}
	}
}

void iris_metal_silu(float *x, int n) {
	if (!g_shaders_initialized || !g_silu_pipeline || n <= 0)
		return;

	@autoreleasepool {
		size_t data_size = (size_t)n * sizeof(float);

		id<MTLBuffer> bufX = pool_get_buffer(data_size);
		if (!bufX)
			return;

		memcpy([bufX contents], x, data_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_silu_pipeline];
		[encoder setBuffer:bufX offset:0 atIndex:0];
		[encoder setBytes:&n length:sizeof(int) atIndex:1];

		NSUInteger threadsPerGroup = [g_silu_pipeline maxTotalThreadsPerThreadgroup];
		NSUInteger threadGroups    = (n + threadsPerGroup - 1) / threadsPerGroup;
		[encoder dispatchThreadgroups:MTLSizeMake(threadGroups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		if (!g_in_batch) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			memcpy(x, [bufX contents], data_size);
			pool_release_buffer(bufX);
		}
		else {
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufX;
				g_pending_outputs[g_pending_count].cpu_ptr = x;
				g_pending_outputs[g_pending_count].size    = data_size;
				g_pending_count++;
			}
		}
	}
}

void iris_metal_silu_mul(float *gate, const float *up, int n) {
	if (!g_shaders_initialized || !g_silu_mul_pipeline || n <= 0)
		return;

	@autoreleasepool {
		size_t data_size = (size_t)n * sizeof(float);

		id<MTLBuffer> bufGate = pool_get_buffer(data_size);
		id<MTLBuffer> bufUp   = pool_get_buffer(data_size);
		if (!bufGate || !bufUp) {
			if (bufGate)
				pool_release_buffer(bufGate);
			if (bufUp)
				pool_release_buffer(bufUp);
			return;
		}

		memcpy([bufGate contents], gate, data_size);
		memcpy([bufUp contents], up, data_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_silu_mul_pipeline];
		[encoder setBuffer:bufGate offset:0 atIndex:0];
		[encoder setBuffer:bufUp offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threadsPerGroup = [g_silu_mul_pipeline maxTotalThreadsPerThreadgroup];
		NSUInteger threadGroups    = (n + threadsPerGroup - 1) / threadsPerGroup;
		[encoder dispatchThreadgroups:MTLSizeMake(threadGroups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		if (!g_in_batch) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			memcpy(gate, [bufGate contents], data_size);
			pool_release_buffer(bufGate);
			pool_release_buffer(bufUp);
		}
		else {
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufGate;
				g_pending_outputs[g_pending_count].cpu_ptr = gate;
				g_pending_outputs[g_pending_count].size    = data_size;
				g_pending_count++;
			}
			pool_release_buffer(bufUp);
		}
	}
}

void iris_metal_softmax(float *x, int rows, int cols) {
	if (!g_shaders_initialized || !g_softmax_pipeline || rows <= 0 || cols <= 0)
		return;

	@autoreleasepool {
		size_t data_size = (size_t)rows * cols * sizeof(float);

		id<MTLBuffer> bufX = pool_get_buffer(data_size);
		if (!bufX)
			return;

		memcpy([bufX contents], x, data_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_softmax_pipeline];
		[encoder setBuffer:bufX offset:0 atIndex:0];
		[encoder setBytes:&rows length:sizeof(int) atIndex:1];
		[encoder setBytes:&cols length:sizeof(int) atIndex:2];

		/* One threadgroup per row */
		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)cols);
		[encoder dispatchThreadgroups:MTLSizeMake(rows, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		if (!g_in_batch) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			memcpy(x, [bufX contents], data_size);
			pool_release_buffer(bufX);
		}
		else {
			if (g_pending_count < MAX_BATCH_OUTPUTS) {
				g_pending_outputs[g_pending_count].buffer  = bufX;
				g_pending_outputs[g_pending_count].cpu_ptr = x;
				g_pending_outputs[g_pending_count].size    = data_size;
				g_pending_count++;
			}
		}
	}
}

void iris_metal_rope_2d(float *x, const float *cos_freq, const float *sin_freq, int seq, int heads, int head_dim, int axis_dim) {
	if (!g_shaders_initialized || !g_rope_2d_pipeline)
		return;

	@autoreleasepool {
		size_t data_size = (size_t)seq * heads * head_dim * sizeof(float);
		size_t freq_size = (size_t)seq * head_dim * sizeof(float);

		id<MTLBuffer> bufX   = pool_get_buffer(data_size);
		id<MTLBuffer> bufCos = pool_get_buffer(freq_size);
		id<MTLBuffer> bufSin = pool_get_buffer(freq_size);

		if (!bufX || !bufCos || !bufSin) {
			if (bufX)
				pool_release_buffer(bufX);
			if (bufCos)
				pool_release_buffer(bufCos);
			if (bufSin)
				pool_release_buffer(bufSin);
			return;
		}

		memcpy([bufX contents], x, data_size);
		memcpy([bufCos contents], cos_freq, freq_size);
		memcpy([bufSin contents], sin_freq, freq_size);

		id<MTLCommandBuffer>         cmdBuffer = encode_compute_shader();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rope_2d_pipeline];
		[encoder setBuffer:bufX offset:0 atIndex:0];
		[encoder setBuffer:bufCos offset:0 atIndex:1];
		[encoder setBuffer:bufSin offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&axis_dim length:sizeof(int) atIndex:6];

		/* One thread per (seq, head) pair */
		[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];

		[encoder endEncoding];

		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		memcpy(x, [bufX contents], data_size);

		pool_release_buffer(bufX);
		pool_release_buffer(bufCos);
		pool_release_buffer(bufSin);
	}
}

/* ========================================================================
 * GPU Tensor Operations - Keep data on GPU between operations
 * These functions take GPU tensors and return GPU tensors.
 * ======================================================================== */

/* GPU tensor version of AdaLN normalization */
void iris_gpu_adaln_norm(iris_gpu_tensor_t out, iris_gpu_tensor_t x, const float *shift, const float *scale, int seq, int hidden, float eps) {
	if (!g_shaders_initialized || !g_adaln_norm_pipeline || !out || !x)
		return;

	@autoreleasepool {
		size_t param_size = (size_t)hidden * sizeof(float);

		/* Allocate new buffers with data - shift/scale are timestep-dependent
		 * and change between denoising steps, so they CANNOT use the weight cache.
		 * ARC will release these after the command buffer completes. */
		id<MTLBuffer> bufShift = [g_device newBufferWithBytes:shift length:param_size options:MTLResourceStorageModeShared];
		id<MTLBuffer> bufScale = [g_device newBufferWithBytes:scale length:param_size options:MTLResourceStorageModeShared];

		if (!bufShift || !bufScale)
			return;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_adaln_norm_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufShift offset:0 atIndex:1];
		[encoder setBuffer:bufScale offset:0 atIndex:2];
		[encoder setBuffer:out->buffer offset:0 atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];
		[encoder setBytes:&eps length:sizeof(float) atIndex:5];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
		/* ARC will release bufShift and bufScale after command completes */
	}
}

/* GPU tensor version of QK RMSNorm */
void iris_gpu_qk_rms_norm(iris_gpu_tensor_t q, iris_gpu_tensor_t k, const float *q_weight, const float *k_weight, int seq, int heads, int head_dim, float eps) {
	if (!g_shaders_initialized || !g_qk_rms_norm_pipeline || !q || !k)
		return;

	@autoreleasepool {
		size_t weight_size = (size_t)head_dim * sizeof(float);

		id<MTLBuffer> bufQWeight = get_cached_weight_buffer(q_weight, weight_size);
		id<MTLBuffer> bufKWeight = get_cached_weight_buffer(k_weight, weight_size);

		if (!bufQWeight || !bufKWeight)
			return;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_qk_rms_norm_pipeline];
		[encoder setBuffer:q->buffer offset:0 atIndex:0];
		[encoder setBuffer:k->buffer offset:0 atIndex:1];
		[encoder setBuffer:bufQWeight offset:0 atIndex:2];
		[encoder setBuffer:bufKWeight offset:0 atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&eps length:sizeof(float) atIndex:6];

		[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];

		[encoder endEncoding];

		q->has_pending_work = 1;
		k->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			q->has_pending_work = 0;
			k->has_pending_work = 0;
		}
	}
}

/* GPU tensor version of f32 RMSNorm: out = rms_norm(x) * weight
 * Uses pool buffers (not weight cache) because callers may pass temporary
 * fused weights that change per block/step. */
void iris_gpu_rms_norm_f32(iris_gpu_tensor_t out, iris_gpu_tensor_t x, const float *weight, int seq, int hidden, float eps) {
	if (!g_shaders_initialized || !g_rms_norm_pipeline || !out || !x)
		return;

	@autoreleasepool {
		size_t        weight_size = (size_t)hidden * sizeof(float);
		id<MTLBuffer> bufWeight   = pool_get_buffer(weight_size);
		if (!bufWeight)
			return;
		memcpy([bufWeight contents], weight, weight_size);

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rms_norm_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufWeight offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:3];
		[encoder setBytes:&eps length:sizeof(float) atIndex:4];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}

		pool_release_buffer(bufWeight);
	}
}

/* GPU tensor version of RoPE 2D */
void iris_gpu_rope_2d(iris_gpu_tensor_t x, const float *cos_freq, const float *sin_freq, int seq, int heads, int head_dim, int axis_dim) {
	if (!g_shaders_initialized || !g_rope_2d_pipeline || !x)
		return;

	@autoreleasepool {
		size_t freq_size = (size_t)seq * head_dim * sizeof(float);

		id<MTLBuffer> bufCos = pool_get_buffer(freq_size);
		id<MTLBuffer> bufSin = pool_get_buffer(freq_size);

		if (!bufCos || !bufSin) {
			if (bufCos)
				pool_release_buffer(bufCos);
			if (bufSin)
				pool_release_buffer(bufSin);
			return;
		}

		memcpy([bufCos contents], cos_freq, freq_size);
		memcpy([bufSin contents], sin_freq, freq_size);

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rope_2d_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufCos offset:0 atIndex:1];
		[encoder setBuffer:bufSin offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&axis_dim length:sizeof(int) atIndex:6];

		[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];

		[encoder endEncoding];

		x->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			x->has_pending_work = 0;
		}

		pool_release_buffer(bufCos);
		pool_release_buffer(bufSin);
	}
}

/* GPU tensor version of unified RoPE for text+image */
void iris_gpu_rope_unified(iris_gpu_tensor_t q, iris_gpu_tensor_t k, const float *txt_cos, const float *txt_sin, const float *img_cos, const float *img_sin,
                           int seq, int img_offset, int heads, int head_dim, int axis_dim) {
	if (!g_shaders_initialized || !g_rope_unified_pipeline || !q || !k)
		return;

	int txt_seq = img_offset;
	int img_seq = seq - img_offset;

	@autoreleasepool {
		size_t txt_freq_size = (size_t)txt_seq * head_dim * sizeof(float);
		size_t img_freq_size = (size_t)img_seq * head_dim * sizeof(float);

		id<MTLBuffer> bufTxtCos = pool_get_buffer(txt_freq_size);
		id<MTLBuffer> bufTxtSin = pool_get_buffer(txt_freq_size);
		id<MTLBuffer> bufImgCos = pool_get_buffer(img_freq_size);
		id<MTLBuffer> bufImgSin = pool_get_buffer(img_freq_size);

		if (!bufTxtCos || !bufTxtSin || !bufImgCos || !bufImgSin) {
			if (bufTxtCos)
				pool_release_buffer(bufTxtCos);
			if (bufTxtSin)
				pool_release_buffer(bufTxtSin);
			if (bufImgCos)
				pool_release_buffer(bufImgCos);
			if (bufImgSin)
				pool_release_buffer(bufImgSin);
			return;
		}

		memcpy([bufTxtCos contents], txt_cos, txt_freq_size);
		memcpy([bufTxtSin contents], txt_sin, txt_freq_size);
		memcpy([bufImgCos contents], img_cos, img_freq_size);
		memcpy([bufImgSin contents], img_sin, img_freq_size);

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();

		/* Apply unified RoPE to Q */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_rope_unified_pipeline];
			[encoder setBuffer:q->buffer offset:0 atIndex:0];
			[encoder setBuffer:bufTxtCos offset:0 atIndex:1];
			[encoder setBuffer:bufTxtSin offset:0 atIndex:2];
			[encoder setBuffer:bufImgCos offset:0 atIndex:3];
			[encoder setBuffer:bufImgSin offset:0 atIndex:4];
			[encoder setBytes:&seq length:sizeof(int) atIndex:5];
			[encoder setBytes:&img_offset length:sizeof(int) atIndex:6];
			[encoder setBytes:&heads length:sizeof(int) atIndex:7];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
			[encoder setBytes:&axis_dim length:sizeof(int) atIndex:9];

			[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];
			[encoder endEncoding];
		}

		/* Apply unified RoPE to K */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_rope_unified_pipeline];
			[encoder setBuffer:k->buffer offset:0 atIndex:0];
			[encoder setBuffer:bufTxtCos offset:0 atIndex:1];
			[encoder setBuffer:bufTxtSin offset:0 atIndex:2];
			[encoder setBuffer:bufImgCos offset:0 atIndex:3];
			[encoder setBuffer:bufImgSin offset:0 atIndex:4];
			[encoder setBytes:&seq length:sizeof(int) atIndex:5];
			[encoder setBytes:&img_offset length:sizeof(int) atIndex:6];
			[encoder setBytes:&heads length:sizeof(int) atIndex:7];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
			[encoder setBytes:&axis_dim length:sizeof(int) atIndex:9];

			[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];
			[encoder endEncoding];
		}

		q->has_pending_work = 1;
		k->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			q->has_pending_work = 0;
			k->has_pending_work = 0;
		}

		pool_release_buffer(bufTxtCos);
		pool_release_buffer(bufTxtSin);
		pool_release_buffer(bufImgCos);
		pool_release_buffer(bufImgSin);
	}
}

static void encode_rope_single_f32(id<MTLCommandBuffer> cmdBuffer, iris_gpu_tensor_t x, id<MTLBuffer> bufCos, id<MTLBuffer> bufSin, int seq, int heads,
                                   int head_dim) {
	if (!x)
		return;

	int img_offset = 0;
	int axis_dim   = head_dim; /* unused by unified shader but required */

	id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
	[encoder setComputePipelineState:g_rope_unified_pipeline];
	[encoder setBuffer:x->buffer offset:0 atIndex:0];
	[encoder setBuffer:bufCos offset:0 atIndex:1]; /* txt_cos (unused) */
	[encoder setBuffer:bufSin offset:0 atIndex:2]; /* txt_sin (unused) */
	[encoder setBuffer:bufCos offset:0 atIndex:3]; /* img_cos = our table */
	[encoder setBuffer:bufSin offset:0 atIndex:4]; /* img_sin = our table */
	[encoder setBytes:&seq length:sizeof(int) atIndex:5];
	[encoder setBytes:&img_offset length:sizeof(int) atIndex:6];
	[encoder setBytes:&heads length:sizeof(int) atIndex:7];
	[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
	[encoder setBytes:&axis_dim length:sizeof(int) atIndex:9];
	[encoder dispatchThreads:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, MIN(heads, 64), 1)];
	[encoder endEncoding];

	x->has_pending_work = 1;
}

/* Apply single-stream RoPE to one or two f32 tensors using the same frequency table. */
void iris_gpu_rope_single_pair_f32(iris_gpu_tensor_t q, iris_gpu_tensor_t k, const float *cos_freq, const float *sin_freq, int seq, int heads, int head_dim) {
	if (!g_shaders_initialized || !g_rope_unified_pipeline)
		return;
	if (!q && !k)
		return;

	@autoreleasepool {
		size_t        freq_size = (size_t)seq * head_dim * sizeof(float);
		id<MTLBuffer> bufCos    = get_rope_buffer(cos_freq, freq_size);
		id<MTLBuffer> bufSin    = get_rope_buffer(sin_freq, freq_size);
		if (!bufCos || !bufSin)
			return;

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		encode_rope_single_f32(cmdBuffer, q, bufCos, bufSin, seq, heads, head_dim);
		if (k && k != q) {
			encode_rope_single_f32(cmdBuffer, k, bufCos, bufSin, seq, heads, head_dim);
		}

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			if (q)
				q->has_pending_work = 0;
			if (k)
				k->has_pending_work = 0;
		}
	}
}

/* GPU tensor version of single-stream RoPE for f32 tensors.
 * Uses the unified RoPE shader with consecutive-pair rotation.
 * All tokens use the same cos/sin table (no text/image split). */
void iris_gpu_rope_single_f32(iris_gpu_tensor_t x, const float *cos_freq, const float *sin_freq, int seq, int heads, int head_dim) {
	iris_gpu_rope_single_pair_f32(x, NULL, cos_freq, sin_freq, seq, heads, head_dim);
}

/* GPU tensor version of SiLU multiply */
void iris_gpu_silu_mul(iris_gpu_tensor_t gate, iris_gpu_tensor_t up, int n) {
	if (!g_shaders_initialized || !g_silu_mul_pipeline || !gate || !up)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_silu_mul_pipeline];
		[encoder setBuffer:gate->buffer offset:0 atIndex:0];
		[encoder setBuffer:up->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = MIN(256, (NSUInteger)n);
		[encoder dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];

		[encoder endEncoding];

		gate->has_pending_work = 1;
		up->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			gate->has_pending_work = 0;
			up->has_pending_work   = 0;
		}
	}
}

/* GPU tensor version of gated add: out += gate * proj */
void iris_gpu_gated_add(iris_gpu_tensor_t out, const float *gate, iris_gpu_tensor_t proj, int seq, int hidden) {
	if (!g_shaders_initialized || !g_gated_add_pipeline || !out || !proj)
		return;

	@autoreleasepool {
		size_t gate_size = (size_t)hidden * sizeof(float);

		/* Allocate new buffer with data - gate is timestep-dependent
		 * and changes between denoising steps, so it CANNOT use the weight cache.
		 * ARC will release this after the command buffer completes. */
		id<MTLBuffer> bufGate = [g_device newBufferWithBytes:gate length:gate_size options:MTLResourceStorageModeShared];
		if (!bufGate)
			return;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_gated_add_pipeline];
		[encoder setBuffer:out->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufGate offset:0 atIndex:1];
		[encoder setBuffer:proj->buffer offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];

		[encoder dispatchThreads:MTLSizeMake(seq, hidden, 1) threadsPerThreadgroup:MTLSizeMake(MIN(32, seq), MIN(32, hidden), 1)];

		[encoder endEncoding];

		out->has_pending_work  = 1;
		proj->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work  = 0;
			proj->has_pending_work = 0;
		}
		/* ARC will release bufGate after command completes */
	}
}

/* Split fused QKV+MLP output into separate tensors */
void iris_gpu_split_qkv_mlp(iris_gpu_tensor_t fused, iris_gpu_tensor_t q, iris_gpu_tensor_t k, iris_gpu_tensor_t v, iris_gpu_tensor_t gate,
                            iris_gpu_tensor_t up, int seq, int hidden, int mlp_hidden) {
	if (!g_shaders_initialized || !g_split_qkv_mlp_pipeline)
		return;
	if (!fused || !q || !k || !v || !gate || !up)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_split_qkv_mlp_pipeline];
		[encoder setBuffer:fused->buffer offset:0 atIndex:0];
		[encoder setBuffer:q->buffer offset:0 atIndex:1];
		[encoder setBuffer:k->buffer offset:0 atIndex:2];
		[encoder setBuffer:v->buffer offset:0 atIndex:3];
		[encoder setBuffer:gate->buffer offset:0 atIndex:4];
		[encoder setBuffer:up->buffer offset:0 atIndex:5];
		[encoder setBytes:&seq length:sizeof(int) atIndex:6];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:7];
		[encoder setBytes:&mlp_hidden length:sizeof(int) atIndex:8];

		/* Dispatch enough threads to cover max(hidden, mlp_hidden) */
		int max_dim = MAX(hidden, mlp_hidden);
		[encoder dispatchThreads:MTLSizeMake(seq, max_dim, 1) threadsPerThreadgroup:MTLSizeMake(MIN(32, seq), MIN(32, max_dim), 1)];

		[encoder endEncoding];

		fused->has_pending_work = 1;
		q->has_pending_work     = 1;
		k->has_pending_work     = 1;
		v->has_pending_work     = 1;
		gate->has_pending_work  = 1;
		up->has_pending_work    = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			fused->has_pending_work = 0;
			q->has_pending_work     = 0;
			k->has_pending_work     = 0;
			v->has_pending_work     = 0;
			gate->has_pending_work  = 0;
			up->has_pending_work    = 0;
		}
	}
}

/* Concatenate attention and MLP outputs */
void iris_gpu_concat_attn_mlp(iris_gpu_tensor_t attn, iris_gpu_tensor_t mlp, iris_gpu_tensor_t out, int seq, int hidden, int mlp_hidden) {
	if (!g_shaders_initialized || !g_concat_attn_mlp_pipeline)
		return;
	if (!attn || !mlp || !out)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_concat_attn_mlp_pipeline];
		[encoder setBuffer:attn->buffer offset:0 atIndex:0];
		[encoder setBuffer:mlp->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];
		[encoder setBytes:&mlp_hidden length:sizeof(int) atIndex:5];

		/* Dispatch enough threads to cover max(hidden, mlp_hidden) */
		int max_dim = MAX(hidden, mlp_hidden);
		[encoder dispatchThreads:MTLSizeMake(seq, max_dim, 1) threadsPerThreadgroup:MTLSizeMake(MIN(32, seq), MIN(32, max_dim), 1)];

		[encoder endEncoding];

		attn->has_pending_work = 1;
		mlp->has_pending_work  = 1;
		out->has_pending_work  = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			attn->has_pending_work = 0;
			mlp->has_pending_work  = 0;
			out->has_pending_work  = 0;
		}
	}
}

/* GPU tensor version of fused attention (no transpose needed) */
int iris_gpu_attention_fused(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, int seq_q, int seq_k, int num_heads,
                             int head_dim, float scale) {
	if (!g_shaders_initialized || !g_attention_fused_pipeline)
		return 0;
	if (!out || !Q || !K || !V)
		return 0;

	/* Dynamic threadgroup memory: shared_scores[seq_k] + static shared_max/sum.
	 * 32KB threadgroup memory allows up to ~7680 seq_k. */
	NSUInteger scores_size = (NSUInteger)seq_k * sizeof(float);
	NSUInteger static_size = 256 * sizeof(float) * 2; /* shared_max + shared_sum */
	if (scores_size + static_size > 32768)
		return 0; /* Hard limit: 32KB threadgroup */

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_attention_fused_pipeline];
		[encoder setBuffer:Q->buffer offset:0 atIndex:0];
		[encoder setBuffer:K->buffer offset:0 atIndex:1];
		[encoder setBuffer:V->buffer offset:0 atIndex:2];
		[encoder setBuffer:out->buffer offset:0 atIndex:3];
		[encoder setBytes:&seq_q length:sizeof(int) atIndex:4];
		[encoder setBytes:&seq_k length:sizeof(int) atIndex:5];
		[encoder setBytes:&num_heads length:sizeof(int) atIndex:6];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:7];
		[encoder setBytes:&scale length:sizeof(float) atIndex:8];
		[encoder setThreadgroupMemoryLength:scores_size atIndex:0];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
		[encoder dispatchThreadgroups:MTLSizeMake(seq_q, num_heads, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		out->has_pending_work = 1;
		Q->has_pending_work   = 1;
		K->has_pending_work   = 1;
		V->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			Q->has_pending_work   = 0;
			K->has_pending_work   = 0;
			V->has_pending_work   = 0;
		}

		return 1;
	}
}

/* Native BF16 attention using GPU tensor API.
 * All tensors must be bf16 format (is_f16 = 1).
 * Uses the bf16 compute shaders with f32 accumulation.
 * Q, K, V: [heads, seq_q/seq_k, head_dim] in bf16
 * out: [heads, seq_q, head_dim] in bf16
 */
int iris_gpu_attention_bf16_native(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, int seq_q, int seq_k, int num_heads,
                                   int head_dim, float scale) {
	if (!iris_bf16_pipeline_available())
		return 0;
	if (!out || !Q || !K || !V)
		return 0;
	if (!out->is_f16 || !Q->is_f16 || !K->is_f16 || !V->is_f16)
		return 0;

	@autoreleasepool {
		size_t scores_elements = (size_t)num_heads * seq_q * seq_k;
		size_t scores_size     = scores_elements * sizeof(uint16_t);

		id<MTLBuffer> bufScores = pool_get_buffer(scores_size);
		if (!bufScores)
			return 0;

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();

		/* Phase 1: Q @ K^T */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_bmm_bf16_qkt_pipeline];
			[encoder setBuffer:Q->buffer offset:0 atIndex:0];
			[encoder setBuffer:K->buffer offset:0 atIndex:1];
			[encoder setBuffer:bufScores offset:0 atIndex:2];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:3];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:4];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
			[encoder setBytes:&num_heads length:sizeof(int) atIndex:6];
			[encoder setBytes:&scale length:sizeof(float) atIndex:7];

			uint    TILE_SIZE       = 16;
			MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
			MTLSize numGroups       = MTLSizeMake((seq_k + TILE_SIZE - 1) / TILE_SIZE, (seq_q + TILE_SIZE - 1) / TILE_SIZE, num_heads);
			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
			[encoder endEncoding];
		}

		/* Phase 2: Softmax */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_softmax_bf16_pipeline];
			[encoder setBuffer:bufScores offset:0 atIndex:0];
			int total_rows = num_heads * seq_q;
			[encoder setBytes:&total_rows length:sizeof(int) atIndex:1];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:2];

			NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
			[encoder dispatchThreadgroups:MTLSizeMake(total_rows, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
			[encoder endEncoding];
		}

		/* Phase 3: scores @ V */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_bmm_bf16_sv_pipeline];
			[encoder setBuffer:bufScores offset:0 atIndex:0];
			[encoder setBuffer:V->buffer offset:0 atIndex:1];
			[encoder setBuffer:out->buffer offset:0 atIndex:2];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:3];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:4];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
			[encoder setBytes:&num_heads length:sizeof(int) atIndex:6];

			uint    TILE_SIZE       = 16;
			MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
			MTLSize numGroups       = MTLSizeMake((head_dim + TILE_SIZE - 1) / TILE_SIZE, (seq_q + TILE_SIZE - 1) / TILE_SIZE, num_heads);
			[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
			[encoder endEncoding];
		}

		out->has_pending_work = 1;
		Q->has_pending_work   = 1;
		K->has_pending_work   = 1;
		V->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			Q->has_pending_work   = 0;
			K->has_pending_work   = 0;
			V->has_pending_work   = 0;
		}

		pool_release_buffer(bufScores);
		return 1;
	}
}

/* MPSGraph SDPA (bf16) for long sequences.
 * Q/K/V expected in [seq, heads * head_dim] layout (bf16).
 */
static sdpa_graph_cache_t *get_sdpa_graph_cache(int seq_q, int seq_k, int num_heads, int head_dim, float scale) {
	if (!NSClassFromString(@"MPSGraph")) {
		return NULL;
	}

	/* Prefer native SDPA (flash attention) when available for long sequences.
	 * Can be disabled for debugging/regression comparisons by setting:
	 *   IRIS_USE_MANUAL_SDPA=1
	 */
	static int s_force_manual_sdpa = -1;
	if (s_force_manual_sdpa < 0) {
		s_force_manual_sdpa = getenv("IRIS_USE_MANUAL_SDPA") ? 1 : 0;
	}

	pthread_mutex_lock(&g_sdpa_graph_mutex);
	for (int i = 0; i < g_sdpa_graph_count; i++) {
		sdpa_graph_cache_t *entry = &g_sdpa_graph_cache[i];
		if (entry->seq_q == seq_q && entry->seq_k == seq_k && entry->num_heads == num_heads && entry->head_dim == head_dim) {
			pthread_mutex_unlock(&g_sdpa_graph_mutex);
			return entry;
		}
	}

	int slot = 0;
	if (g_sdpa_graph_count < MAX_SDPA_GRAPH_CACHE) {
		slot = g_sdpa_graph_count++;
	}

	sdpa_graph_cache_t *entry = &g_sdpa_graph_cache[slot];
	entry->seq_q              = seq_q;
	entry->seq_k              = seq_k;
	entry->num_heads          = num_heads;
	entry->head_dim           = head_dim;

	@autoreleasepool {
		MPSGraph *graph = [[MPSGraph alloc] init];
		if (!graph) {
			pthread_mutex_unlock(&g_sdpa_graph_mutex);
			return NULL;
		}

		/* Native SDPA expects [batch, heads, seq, head_dim] layout.
		 * Our input is [seq, heads * head_dim], reshape to [1, seq, heads, head_dim]
		 * then transpose to [1, heads, seq, head_dim]. */
		NSArray<NSNumber *> *qShape   = @[ @1, @(seq_q), @(num_heads), @(head_dim) ];
		NSArray<NSNumber *> *kShape   = @[ @1, @(seq_k), @(num_heads), @(head_dim) ];
		NSArray<NSNumber *> *vShape   = @[ @1, @(seq_k), @(num_heads), @(head_dim) ];
		NSArray<NSNumber *> *outShape = @[ @1, @(seq_q), @(num_heads), @(head_dim) ];

		MPSGraphTensor *qIn = [graph placeholderWithShape:qShape dataType:MPSDataTypeBFloat16 name:nil];
		MPSGraphTensor *kIn = [graph placeholderWithShape:kShape dataType:MPSDataTypeBFloat16 name:nil];
		MPSGraphTensor *vIn = [graph placeholderWithShape:vShape dataType:MPSDataTypeBFloat16 name:nil];

		/* Transpose to [1, heads, seq, head_dim] for SDPA */
		MPSGraphTensor *qT = [graph transposeTensor:qIn dimension:1 withDimension:2 name:nil];
		MPSGraphTensor *kT = [graph transposeTensor:kIn dimension:1 withDimension:2 name:nil];
		MPSGraphTensor *vT = [graph transposeTensor:vIn dimension:1 withDimension:2 name:nil];

		MPSGraphTensor *outTensor = nil;

		if (!s_force_manual_sdpa && [graph respondsToSelector:@selector(scaledDotProductAttentionWithQueryTensor:keyTensor:valueTensor:scale:name:)]) {
			/* Native SDPA is significantly more memory-efficient than materializing
			 * the full [heads, seq_q, seq_k] score matrix for long sequences. */
			MPSGraphTensor *out     = [graph scaledDotProductAttentionWithQueryTensor:qT keyTensor:kT valueTensor:vT scale:scale name:nil];
			MPSGraphTensor *outCast = [graph castTensor:out toType:MPSDataTypeBFloat16 name:nil];
			outTensor               = [graph transposeTensor:outCast dimension:1 withDimension:2 name:nil];
		}
		else {
			/* Manual attention: Q @ K^T * scale -> softmax -> @ V
			 * Uses f32 for softmax to maintain precision.
			 * Note: this materializes the score matrix and can be very memory-hungry
			 * for large seq lengths. */
			MPSGraphTensor *kTT = [graph transposeTensor:kT dimension:2 withDimension:3 name:nil];

			MPSGraphTensor *qk          = [graph matrixMultiplicationWithPrimaryTensor:qT secondaryTensor:kTT name:nil];
			MPSGraphTensor *qkF32       = [graph castTensor:qk toType:MPSDataTypeFloat32 name:nil];
			MPSGraphTensor *scaleTensor = [graph constantWithScalar:scale shape:@[ @1 ] dataType:MPSDataTypeFloat32];
			MPSGraphTensor *scaled      = [graph multiplicationWithPrimaryTensor:qkF32 secondaryTensor:scaleTensor name:nil];
			MPSGraphTensor *sm          = [graph softMaxWithTensor:scaled axis:3 name:nil];
			MPSGraphTensor *out         = [graph matrixMultiplicationWithPrimaryTensor:sm secondaryTensor:vT name:nil];
			MPSGraphTensor *outCast     = [graph castTensor:out toType:MPSDataTypeBFloat16 name:nil];
			outTensor                   = [graph transposeTensor:outCast dimension:1 withDimension:2 name:nil];
		}

		entry->graph     = graph;
		entry->qTensor   = qIn;
		entry->kTensor   = kIn;
		entry->vTensor   = vIn;
		entry->outTensor = outTensor;
		entry->qShape    = qShape;
		entry->kShape    = kShape;
		entry->vShape    = vShape;
		entry->outShape  = outShape;
	}

	pthread_mutex_unlock(&g_sdpa_graph_mutex);
	return entry;
}

static linear_graph_cache_t *get_linear_graph_cache(int seq_len, int in_dim, int out_dim) {
	if (!NSClassFromString(@"MPSGraph")) {
		return NULL;
	}

	pthread_mutex_lock(&g_linear_graph_mutex);
	for (int i = 0; i < g_linear_graph_count; i++) {
		linear_graph_cache_t *entry = &g_linear_graph_cache[i];
		if (entry->seq == seq_len && entry->in_dim == in_dim && entry->out_dim == out_dim) {
			pthread_mutex_unlock(&g_linear_graph_mutex);
			return entry;
		}
	}

	int slot = 0;
	if (g_linear_graph_count < MAX_LINEAR_GRAPH_CACHE) {
		slot = g_linear_graph_count++;
	}

	linear_graph_cache_t *entry = &g_linear_graph_cache[slot];
	entry->seq                  = seq_len;
	entry->in_dim               = in_dim;
	entry->out_dim              = out_dim;

	@autoreleasepool {
		MPSGraph *graph = [[MPSGraph alloc] init];
		if (!graph) {
			pthread_mutex_unlock(&g_linear_graph_mutex);
			return NULL;
		}

		NSArray<NSNumber *> *xShape   = @[ @1, @(seq_len), @(in_dim) ];
		NSArray<NSNumber *> *wShape   = @[ @1, @(out_dim), @(in_dim) ];
		NSArray<NSNumber *> *outShape = @[ @1, @(seq_len), @(out_dim) ];

		MPSGraphTensor *xIn     = [graph placeholderWithShape:xShape dataType:MPSDataTypeBFloat16 name:nil];
		MPSGraphTensor *wIn     = [graph placeholderWithShape:wShape dataType:MPSDataTypeBFloat16 name:nil];
		MPSGraphTensor *wT      = [graph transposeTensor:wIn dimension:1 withDimension:2 name:nil];
		MPSGraphTensor *xF32    = [graph castTensor:xIn toType:MPSDataTypeFloat32 name:nil];
		MPSGraphTensor *wTF32   = [graph castTensor:wT toType:MPSDataTypeFloat32 name:nil];
		MPSGraphTensor *out     = [graph matrixMultiplicationWithPrimaryTensor:xF32 secondaryTensor:wTF32 name:nil];
		MPSGraphTensor *outCast = [graph castTensor:out toType:MPSDataTypeBFloat16 name:nil];

		entry->graph     = graph;
		entry->xTensor   = xIn;
		entry->wTensor   = wIn;
		entry->outTensor = outCast;
		entry->xShape    = xShape;
		entry->wShape    = wShape;
		entry->outShape  = outShape;
	}

	pthread_mutex_unlock(&g_linear_graph_mutex);
	return entry;
}

static linear_graph_cache_t *get_linear_graph_cache_bf16(int seq_len, int in_dim, int out_dim) {
	if (!NSClassFromString(@"MPSGraph")) {
		return NULL;
	}

	pthread_mutex_lock(&g_linear_graph_bf16_mutex);
	for (int i = 0; i < g_linear_graph_bf16_count; i++) {
		linear_graph_cache_t *entry = &g_linear_graph_cache_bf16[i];
		if (entry->seq == seq_len && entry->in_dim == in_dim && entry->out_dim == out_dim) {
			pthread_mutex_unlock(&g_linear_graph_bf16_mutex);
			return entry;
		}
	}

	int slot = 0;
	if (g_linear_graph_bf16_count < MAX_LINEAR_GRAPH_CACHE) {
		slot = g_linear_graph_bf16_count++;
	}

	linear_graph_cache_t *entry = &g_linear_graph_cache_bf16[slot];
	entry->seq                  = seq_len;
	entry->in_dim               = in_dim;
	entry->out_dim              = out_dim;

	@autoreleasepool {
		MPSGraph *graph = [[MPSGraph alloc] init];
		if (!graph) {
			pthread_mutex_unlock(&g_linear_graph_bf16_mutex);
			return NULL;
		}

		NSArray<NSNumber *> *xShape   = @[ @1, @(seq_len), @(in_dim) ];
		NSArray<NSNumber *> *wShape   = @[ @1, @(out_dim), @(in_dim) ];
		NSArray<NSNumber *> *outShape = @[ @1, @(seq_len), @(out_dim) ];

		MPSGraphTensor *xIn = [graph placeholderWithShape:xShape dataType:MPSDataTypeBFloat16 name:nil];
		MPSGraphTensor *wIn = [graph placeholderWithShape:wShape dataType:MPSDataTypeBFloat16 name:nil];
		MPSGraphTensor *wT  = [graph transposeTensor:wIn dimension:1 withDimension:2 name:nil];

		/* Try bf16 matmul directly (avoids fp32 casts). If unsupported, encode will
		 * throw and we will permanently disable this fast path at runtime. */
		MPSGraphTensor *out     = [graph matrixMultiplicationWithPrimaryTensor:xIn secondaryTensor:wT name:nil];
		MPSGraphTensor *outCast = [graph castTensor:out toType:MPSDataTypeBFloat16 name:nil];

		entry->graph     = graph;
		entry->xTensor   = xIn;
		entry->wTensor   = wIn;
		entry->outTensor = outCast;
		entry->xShape    = xShape;
		entry->wShape    = wShape;
		entry->outShape  = outShape;
	}

	pthread_mutex_unlock(&g_linear_graph_bf16_mutex);
	return entry;
}

static int iris_gpu_attention_mpsgraph_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, int seq_q, int seq_k,
                                            int num_heads, int head_dim, float scale) {
	if (!g_initialized || !g_device)
		return 0;
	if (!out || !Q || !K || !V)
		return 0;
	if (!out->is_f16 || !Q->is_f16 || !K->is_f16 || !V->is_f16)
		return 0;

	if (bf16_debug_enabled()) {
		fprintf(stderr, "[ATTN] Using MPSGraph attention seq_q=%d seq_k=%d\n", seq_q, seq_k);
	}

	sdpa_graph_cache_t *cache = get_sdpa_graph_cache(seq_q, seq_k, num_heads, head_dim, scale);
	if (!cache || !cache->graph)
		return 0;

	@autoreleasepool {
		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		if (!cmdBuffer)
			return 0;
		MPSCommandBuffer *mpsCmd = nil;
		if (g_tensor_batch_mode) {
			mpsCmd = [MPSCommandBuffer commandBufferWithCommandBuffer:cmdBuffer];
		}
		else {
			mpsCmd = [MPSCommandBuffer commandBufferFromCommandQueue:g_queue];
		}
		if (!mpsCmd)
			return 0;

		MPSGraphTensorData *qData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:Q->buffer shape:cache->qShape dataType:MPSDataTypeBFloat16];
		MPSGraphTensorData *kData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:K->buffer shape:cache->kShape dataType:MPSDataTypeBFloat16];
		MPSGraphTensorData *vData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:V->buffer shape:cache->vShape dataType:MPSDataTypeBFloat16];
		MPSGraphTensorData *outData = [[MPSGraphTensorData alloc] initWithMTLBuffer:out->buffer shape:cache->outShape dataType:MPSDataTypeBFloat16];

		NSDictionary *feeds   = @{cache->qTensor : qData, cache->kTensor : kData, cache->vTensor : vData};
		NSDictionary *results = @{cache->outTensor : outData};

		@try {
			[cache->graph encodeToCommandBuffer:mpsCmd feeds:feeds targetOperations:nil resultsDictionary:results executionDescriptor:nil];
		} @catch (NSException *exception) {
			return 0;
		}

		out->has_pending_work = 1;
		Q->has_pending_work   = 1;
		K->has_pending_work   = 1;
		V->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[mpsCmd commit];
			[mpsCmd waitUntilCompleted];
			out->has_pending_work = 0;
			Q->has_pending_work   = 0;
			K->has_pending_work   = 0;
			V->has_pending_work   = 0;
		}
		else {
			/* MPSGraph may commit-and-continue; update the live buffer. */
			g_tensor_cmd = [mpsCmd rootCommandBuffer];
		}

		return 1;
	}
}

/* Truly fused BF16 attention - no intermediate score storage.
 * Uses the attention_fused_bf16 kernel which keeps all computation in threadgroup memory.
 * Input: Q, K, V in [seq, heads*head_dim] layout (bf16)
 * Output: [seq, heads*head_dim] (bf16)
 * This keeps computation in threadgroup memory without materializing scores.
 */
/* MPSGraph attention is faster than our custom Metal kernel (~20% improvement).
 * Set IRIS_USE_CUSTOM_ATTN=1 to force using the custom kernel for comparison. */
static int prefer_custom_attn(void) {
	static int pref = -1;
	if (pref < 0) {
		pref = getenv("IRIS_USE_CUSTOM_ATTN") ? 1 : 0;
	}
	return pref;
}

int iris_gpu_attention_fused_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, int seq_q, int seq_k, int num_heads,
                                  int head_dim, float scale) {
	if (!g_shaders_initialized)
		return 0;
	if (!out || !Q || !K || !V)
		return 0;
	if (!out->is_f16 || !Q->is_f16 || !K->is_f16 || !V->is_f16)
		return 0;

	/* Try MPSGraph attention first (faster than custom kernel).
	 * Fall back to custom kernel only if MPSGraph fails or is disabled. */
	if (!prefer_custom_attn() && NSClassFromString(@"MPSGraph")) {
		if (iris_gpu_attention_mpsgraph_bf16(out, Q, K, V, seq_q, seq_k, num_heads, head_dim, scale)) {
			return 1;
		}
	}

	/* Dynamic threadgroup memory: shared_scores[seq_k] + static arrays */
	NSUInteger bf16_scores_size   = (NSUInteger)seq_k * sizeof(float);
	NSUInteger bf16_static_size   = (256 + 256 + 128) * sizeof(float); /* max/sum/q */
	int        custom_kernel_fits = (bf16_scores_size + bf16_static_size <= 32768);

	if (custom_kernel_fits && g_attention_fused_bf16_pipeline) {
		@autoreleasepool {
			id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
			id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

			if (bf16_debug_enabled()) {
				fprintf(stderr, "[ATTN] Using custom kernel seq_q=%d seq_k=%d heads=%d head_dim=%d\n", seq_q, seq_k, num_heads, head_dim);
			}

			[encoder setComputePipelineState:g_attention_fused_bf16_pipeline];
			[encoder setBuffer:Q->buffer offset:0 atIndex:0];
			[encoder setBuffer:K->buffer offset:0 atIndex:1];
			[encoder setBuffer:V->buffer offset:0 atIndex:2];
			[encoder setBuffer:out->buffer offset:0 atIndex:3];
			[encoder setBytes:&seq_q length:sizeof(int) atIndex:4];
			[encoder setBytes:&seq_k length:sizeof(int) atIndex:5];
			[encoder setBytes:&num_heads length:sizeof(int) atIndex:6];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:7];
			[encoder setBytes:&scale length:sizeof(float) atIndex:8];
			[encoder setThreadgroupMemoryLength:bf16_scores_size atIndex:0];

			/* Dispatch: one threadgroup per (query_pos, head) pair */
			NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
			[encoder dispatchThreadgroups:MTLSizeMake(seq_q, num_heads, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

			[encoder endEncoding];

			out->has_pending_work = 1;
			Q->has_pending_work   = 1;
			K->has_pending_work   = 1;
			V->has_pending_work   = 1;

			if (!g_tensor_batch_mode) {
				[cmdBuffer commit];
				[cmdBuffer waitUntilCompleted];
				out->has_pending_work = 0;
				Q->has_pending_work   = 0;
				K->has_pending_work   = 0;
				V->has_pending_work   = 0;
			}

			return 1;
		}
	}

	if (NSClassFromString(@"MPSGraph")) {
		if (iris_gpu_attention_mpsgraph_bf16(out, Q, K, V, seq_q, seq_k, num_heads, head_dim, scale)) {
			return 1;
		}
	}

	if (!custom_kernel_fits && bf16_debug_enabled()) {
		fprintf(stderr, "[BF16] attention_fused_bf16 seq_k=%d exceeds threadgroup memory\n", seq_k);
	}
	if (!g_attention_fused_bf16_pipeline && bf16_debug_enabled()) {
		fprintf(stderr, "[BF16] attention_fused_bf16 missing pipeline\n");
	}
	return 0;
}

/* ========================================================================
 * BF16 GPU Tensor Operations
 * These operate on bf16 GPU tensors (is_f16 = 1) with f32 internal computation.
 * ======================================================================== */

/* BF16 AdaLN: out = (1 + scale) * layernorm(x) + shift */
void iris_gpu_adaln_norm_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t x, iris_gpu_tensor_t shift_bf16, iris_gpu_tensor_t scale_bf16, int seq, int hidden,
                              float eps) {
	if (!g_shaders_initialized || !g_adaln_norm_bf16_pipeline)
		return;
	if (!out || !x || !shift_bf16 || !scale_bf16)
		return;
	if (!out->is_f16 || !x->is_f16 || !shift_bf16->is_f16 || !scale_bf16->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_adaln_norm_bf16_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:shift_bf16->buffer offset:0 atIndex:1];
		[encoder setBuffer:scale_bf16->buffer offset:0 atIndex:2];
		[encoder setBuffer:out->buffer offset:0 atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];
		[encoder setBytes:&eps length:sizeof(float) atIndex:5];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
		}
	}
}

/* BF16 QK RMSNorm (in-place on bf16 tensors) */
void iris_gpu_qk_rms_norm_bf16(iris_gpu_tensor_t q, iris_gpu_tensor_t k, iris_gpu_tensor_t q_weight_bf16, iris_gpu_tensor_t k_weight_bf16, int seq, int heads,
                               int head_dim, float eps) {
	if (!g_shaders_initialized || !g_qk_rms_norm_bf16_pipeline)
		return;
	if (!q || !k || !q_weight_bf16 || !k_weight_bf16)
		return;
	if (!q->is_f16 || !k->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_qk_rms_norm_bf16_pipeline];
		[encoder setBuffer:q->buffer offset:0 atIndex:0];
		[encoder setBuffer:k->buffer offset:0 atIndex:1];
		[encoder setBuffer:q_weight_bf16->buffer offset:0 atIndex:2];
		[encoder setBuffer:k_weight_bf16->buffer offset:0 atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&eps length:sizeof(float) atIndex:6];

		[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		q->has_pending_work = 1;
		k->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			q->has_pending_work = 0;
			k->has_pending_work = 0;
		}
	}
}

/* BF16 per-head RMS Norm (single tensor version for GQA support)
 * x: [seq, heads * head_dim] (bf16, modified in-place)
 * weight: [head_dim] (bf16)
 * Returns 1 on success, 0 on failure
 */
int iris_gpu_head_rms_norm_bf16(iris_gpu_tensor_t x, iris_gpu_tensor_t weight_bf16, int seq, int heads, int head_dim, float eps) {
	if (!g_shaders_initialized || !g_head_rms_norm_bf16_pipeline)
		return 0;
	if (!x || !weight_bf16)
		return 0;
	if (!x->is_f16 || !weight_bf16->is_f16)
		return 0;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_head_rms_norm_bf16_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:weight_bf16->buffer offset:0 atIndex:1];
		[encoder setBytes:&heads length:sizeof(int) atIndex:2];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:3];
		[encoder setBytes:&eps length:sizeof(float) atIndex:4];

		[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		x->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			x->has_pending_work = 0;
		}
	}
	return 1;
}

/* BF16 RMS Norm on tensors: out = rms_norm(x) * weight */
void iris_gpu_rms_norm_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t x, iris_gpu_tensor_t weight, int seq, int hidden, float eps) {
	if (!g_shaders_initialized || !g_rms_norm_bf16_pipeline)
		return;
	if (!out || !x || !weight)
		return;
	if (!out->is_f16 || !x->is_f16 || !weight->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rms_norm_bf16_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:weight->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:3];
		[encoder setBytes:&eps length:sizeof(float) atIndex:4];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)hidden);
		[encoder dispatchThreadgroups:MTLSizeMake(seq, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
	}
}

/* BF16 element-wise add: out = a + b (can be in-place: out = a) */
void iris_gpu_add_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t a, iris_gpu_tensor_t b, int n) {
	if (!g_shaders_initialized || !g_add_bf16_pipeline)
		return;
	if (!out || !a || !b)
		return;
	if (!out->is_f16 || !a->is_f16 || !b->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_add_bf16_pipeline];
		[encoder setBuffer:a->buffer offset:0 atIndex:0];
		[encoder setBuffer:b->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&n length:sizeof(int) atIndex:3];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		a->has_pending_work   = 1;
		b->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			a->has_pending_work   = 0;
			b->has_pending_work   = 0;
		}
	}
}

/* BF16 buffer copy using blit encoder (efficient GPU-side memcpy) */
void iris_gpu_copy_bf16(iris_gpu_tensor_t dst, iris_gpu_tensor_t src, size_t n) {
	if (!g_initialized || !dst || !src || n == 0)
		return;
	if (!dst->is_f16 || !src->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>      cmdBuffer = get_tensor_cmd();
		id<MTLBlitCommandEncoder> blit      = [cmdBuffer blitCommandEncoder];
		size_t                    bytes     = n * sizeof(uint16_t);
		[blit copyFromBuffer:src->buffer sourceOffset:0 toBuffer:dst->buffer destinationOffset:0 size:bytes];
		[blit endEncoding];

		dst->has_pending_work = 1;
		src->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			dst->has_pending_work = 0;
			src->has_pending_work = 0;
		}
	}
}

/* BF16 SiLU multiply: gate = silu(gate) * up */
void iris_gpu_silu_mul_bf16(iris_gpu_tensor_t gate, iris_gpu_tensor_t up, int n) {
	if (!g_shaders_initialized || !g_silu_mul_bf16_pipeline)
		return;
	if (!gate || !up || !gate->is_f16 || !up->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_silu_mul_bf16_pipeline];
		[encoder setBuffer:gate->buffer offset:0 atIndex:0];
		[encoder setBuffer:up->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		gate->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			gate->has_pending_work = 0;
		}
	}
}

/* BF16 Gated add: out += gate * proj */
void iris_gpu_gated_add_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t gate_bf16, iris_gpu_tensor_t proj, int seq, int hidden) {
	if (!g_shaders_initialized || !g_gated_add_bf16_pipeline)
		return;
	if (!out || !gate_bf16 || !proj)
		return;
	if (!out->is_f16 || !gate_bf16->is_f16 || !proj->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_gated_add_bf16_pipeline];
		[encoder setBuffer:out->buffer offset:0 atIndex:0];
		[encoder setBuffer:gate_bf16->buffer offset:0 atIndex:1];
		[encoder setBuffer:proj->buffer offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];

		[encoder dispatchThreadgroups:MTLSizeMake(seq, hidden, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
		}
	}
}

/* BF16 RoPE unified (text + image) */
void iris_gpu_rope_unified_bf16(iris_gpu_tensor_t q, iris_gpu_tensor_t k, const float *txt_cos, const float *txt_sin, const float *img_cos,
                                const float *img_sin, int seq, int img_offset, int heads, int head_dim, int axis_dim) {
	if (!g_shaders_initialized || !g_rope_unified_bf16_pipeline)
		return;
	if (!q || !k || !q->is_f16 || !k->is_f16)
		return;

	@autoreleasepool {
		/* Get cached frequency buffers (f32 - frequencies don't need to be bf16) */
		int    txt_len  = img_offset;
		int    img_len  = seq - img_offset;
		size_t txt_size = (size_t)txt_len * head_dim * sizeof(float);
		size_t img_size = (size_t)img_len * head_dim * sizeof(float);

		id<MTLBuffer> bufTxtCos = get_rope_buffer(txt_cos, txt_size);
		id<MTLBuffer> bufTxtSin = get_rope_buffer(txt_sin, txt_size);
		id<MTLBuffer> bufImgCos = get_rope_buffer(img_cos, img_size);
		id<MTLBuffer> bufImgSin = get_rope_buffer(img_sin, img_size);

		if (!bufTxtCos || !bufTxtSin || !bufImgCos || !bufImgSin)
			return;

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();

		/* Apply RoPE to Q */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_rope_unified_bf16_pipeline];
			[encoder setBuffer:q->buffer offset:0 atIndex:0];
			[encoder setBuffer:bufTxtCos offset:0 atIndex:1];
			[encoder setBuffer:bufTxtSin offset:0 atIndex:2];
			[encoder setBuffer:bufImgCos offset:0 atIndex:3];
			[encoder setBuffer:bufImgSin offset:0 atIndex:4];
			[encoder setBytes:&seq length:sizeof(int) atIndex:5];
			[encoder setBytes:&img_offset length:sizeof(int) atIndex:6];
			[encoder setBytes:&heads length:sizeof(int) atIndex:7];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
			[encoder setBytes:&axis_dim length:sizeof(int) atIndex:9];
			[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
			[encoder endEncoding];
		}

		/* Apply RoPE to K */
		{
			id<MTLComputeCommandEncoder> encoder = [cmdBuffer computeCommandEncoder];
			[encoder setComputePipelineState:g_rope_unified_bf16_pipeline];
			[encoder setBuffer:k->buffer offset:0 atIndex:0];
			[encoder setBuffer:bufTxtCos offset:0 atIndex:1];
			[encoder setBuffer:bufTxtSin offset:0 atIndex:2];
			[encoder setBuffer:bufImgCos offset:0 atIndex:3];
			[encoder setBuffer:bufImgSin offset:0 atIndex:4];
			[encoder setBytes:&seq length:sizeof(int) atIndex:5];
			[encoder setBytes:&img_offset length:sizeof(int) atIndex:6];
			[encoder setBytes:&heads length:sizeof(int) atIndex:7];
			[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
			[encoder setBytes:&axis_dim length:sizeof(int) atIndex:9];
			[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
			[encoder endEncoding];
		}

		q->has_pending_work = 1;
		k->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			q->has_pending_work = 0;
			k->has_pending_work = 0;
		}
	}
}

/* BF16 RoPE 2D (single stream) */
void iris_gpu_rope_2d_bf16(iris_gpu_tensor_t x, const float *cos_freq, const float *sin_freq, int seq, int heads, int head_dim, int axis_dim) {
	if (!g_shaders_initialized || !g_rope_2d_bf16_pipeline)
		return;
	if (!x || !x->is_f16)
		return;

	@autoreleasepool {
		size_t        freq_size = (size_t)seq * head_dim * sizeof(float);
		id<MTLBuffer> bufCos    = get_rope_buffer(cos_freq, freq_size);
		id<MTLBuffer> bufSin    = get_rope_buffer(sin_freq, freq_size);
		if (!bufCos || !bufSin)
			return;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rope_2d_bf16_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufCos offset:0 atIndex:1];
		[encoder setBuffer:bufSin offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&heads length:sizeof(int) atIndex:4];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:5];
		[encoder setBytes:&axis_dim length:sizeof(int) atIndex:6];
		[encoder dispatchThreadgroups:MTLSizeMake(seq, heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		x->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			x->has_pending_work = 0;
		}
	}
}

/* BF16 Causal Attention with GQA support (for text encoder) */
int iris_gpu_causal_attention_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t Q, iris_gpu_tensor_t K, iris_gpu_tensor_t V, const int *attention_mask, int seq,
                                   int num_q_heads, int num_kv_heads, int head_dim, float scale) {
	if (!g_shaders_initialized || !g_causal_attention_bf16_pipeline) {
		return 0;
	}
	if (!out || !Q || !K || !V)
		return 0;
	if (!out->is_f16 || !Q->is_f16 || !K->is_f16 || !V->is_f16)
		return 0;

	/* Limit seq length to what the shader can handle (512 for shared memory) */
	if (seq > 512) {
		return 0;
	}

	@autoreleasepool {
		/* Create mask buffer if needed */
		id<MTLBuffer> bufMask = nil;
		if (attention_mask) {
			size_t mask_size = (size_t)seq * sizeof(int);
			bufMask          = pool_get_buffer(mask_size);
			if (bufMask) {
				memcpy([bufMask contents], attention_mask, mask_size);
			}
		}

		int use_mask = (attention_mask != NULL) ? 1 : 0;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_causal_attention_bf16_pipeline];
		[encoder setBuffer:Q->buffer offset:0 atIndex:0];
		[encoder setBuffer:K->buffer offset:0 atIndex:1];
		[encoder setBuffer:V->buffer offset:0 atIndex:2];
		[encoder setBuffer:out->buffer offset:0 atIndex:3];
		if (bufMask) {
			[encoder setBuffer:bufMask offset:0 atIndex:4];
		}
		else {
			[encoder setBuffer:Q->buffer offset:0 atIndex:4]; /* Dummy for null mask */
		}
		[encoder setBytes:&seq length:sizeof(int) atIndex:5];
		[encoder setBytes:&num_q_heads length:sizeof(int) atIndex:6];
		[encoder setBytes:&num_kv_heads length:sizeof(int) atIndex:7];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
		[encoder setBytes:&scale length:sizeof(float) atIndex:9];
		[encoder setBytes:&use_mask length:sizeof(int) atIndex:10];

		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq);
		[encoder dispatchThreadgroups:MTLSizeMake(seq, num_q_heads, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		Q->has_pending_work   = 1;
		K->has_pending_work   = 1;
		V->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			Q->has_pending_work   = 0;
			K->has_pending_work   = 0;
			V->has_pending_work   = 0;
		}

		if (bufMask) {
			pool_release_buffer(bufMask);
		}
	}

	return 1;
}

/* BF16 RoPE for text encoder (Qwen3 style) */
void iris_gpu_rope_text_bf16(iris_gpu_tensor_t q, iris_gpu_tensor_t k, const float *cos_cache, const float *sin_cache, int seq, int num_q_heads,
                             int num_kv_heads, int head_dim) {
	if (!g_shaders_initialized || !g_rope_bf16_pipeline)
		return;
	if (!q || !k || !q->is_f16 || !k->is_f16)
		return;

	@autoreleasepool {
		int    half_dim  = head_dim / 2;
		size_t freq_size = (size_t)seq * half_dim * sizeof(float);

		id<MTLBuffer> bufCos = get_rope_buffer(cos_cache, freq_size);
		id<MTLBuffer> bufSin = get_rope_buffer(sin_cache, freq_size);
		if (!bufCos || !bufSin)
			return;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_rope_bf16_pipeline];
		[encoder setBuffer:q->buffer offset:0 atIndex:0];
		[encoder setBuffer:k->buffer offset:0 atIndex:1];
		[encoder setBuffer:bufCos offset:0 atIndex:2];
		[encoder setBuffer:bufSin offset:0 atIndex:3];
		[encoder setBytes:&seq length:sizeof(int) atIndex:4];
		[encoder setBytes:&num_q_heads length:sizeof(int) atIndex:5];
		[encoder setBytes:&num_kv_heads length:sizeof(int) atIndex:6];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:7];

		/* Dispatch: one thread per (pos, head) - max of q_heads and kv_heads */
		int max_heads = num_q_heads > num_kv_heads ? num_q_heads : num_kv_heads;
		[encoder dispatchThreadgroups:MTLSizeMake(seq, max_heads, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		q->has_pending_work = 1;
		k->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			q->has_pending_work = 0;
			k->has_pending_work = 0;
		}
	}
}

/* Concatenate two bf16 sequences along seq dimension */
void iris_gpu_concat_seq_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t a, iris_gpu_tensor_t b, int seq_a, int seq_b, int hidden) {
	if (!g_shaders_initialized || !g_concat_seq_bf16_pipeline)
		return;
	if (!out || !a || !b)
		return;
	if (!out->is_f16 || !a->is_f16 || !b->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_concat_seq_bf16_pipeline];
		[encoder setBuffer:a->buffer offset:0 atIndex:0];
		[encoder setBuffer:b->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&seq_a length:sizeof(int) atIndex:3];
		[encoder setBytes:&seq_b length:sizeof(int) atIndex:4];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:5];

		int total_seq = seq_a + seq_b;
		[encoder dispatchThreadgroups:MTLSizeMake(total_seq, hidden, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		a->has_pending_work   = 1;
		b->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			a->has_pending_work   = 0;
			b->has_pending_work   = 0;
		}
	}
}

/* Slice a bf16 sequence along seq dimension */
void iris_gpu_slice_seq_bf16(iris_gpu_tensor_t out, iris_gpu_tensor_t in, int seq_out, int hidden, int start) {
	if (!g_shaders_initialized || !g_slice_seq_bf16_pipeline)
		return;
	if (!out || !in)
		return;
	if (!out->is_f16 || !in->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_slice_seq_bf16_pipeline];
		[encoder setBuffer:in->buffer offset:0 atIndex:0];
		[encoder setBuffer:out->buffer offset:0 atIndex:1];
		[encoder setBytes:&seq_out length:sizeof(int) atIndex:2];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:3];
		[encoder setBytes:&start length:sizeof(int) atIndex:4];

		[encoder dispatchThreadgroups:MTLSizeMake(seq_out, hidden, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		in->has_pending_work  = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			in->has_pending_work  = 0;
		}
	}
}

/* Convert f32 GPU tensor to bf16 GPU tensor */
iris_gpu_tensor_t iris_gpu_tensor_f32_to_bf16(iris_gpu_tensor_t f32_tensor) {
	if (!g_shaders_initialized || !g_f32_to_bf16_pipeline)
		return NULL;
	if (!f32_tensor || f32_tensor->is_f16)
		return NULL;

	iris_gpu_tensor_t bf16_tensor = iris_gpu_tensor_alloc_f16(f32_tensor->num_elements);
	if (!bf16_tensor)
		return NULL;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		int n = (int)f32_tensor->num_elements;
		[encoder setComputePipelineState:g_f32_to_bf16_pipeline];
		[encoder setBuffer:f32_tensor->buffer offset:0 atIndex:0];
		[encoder setBuffer:bf16_tensor->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		bf16_tensor->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			bf16_tensor->has_pending_work = 0;
		}
	}

	return bf16_tensor;
}

/* Convert bf16 GPU tensor to f32 GPU tensor */
iris_gpu_tensor_t iris_gpu_tensor_bf16_to_f32(iris_gpu_tensor_t bf16_tensor) {
	if (!g_shaders_initialized || !g_bf16_to_f32_pipeline)
		return NULL;
	if (!bf16_tensor || !bf16_tensor->is_f16)
		return NULL;

	iris_gpu_tensor_t f32_tensor = iris_gpu_tensor_alloc(bf16_tensor->num_elements);
	if (!f32_tensor)
		return NULL;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		int n = (int)bf16_tensor->num_elements;
		[encoder setComputePipelineState:g_bf16_to_f32_pipeline];
		[encoder setBuffer:bf16_tensor->buffer offset:0 atIndex:0];
		[encoder setBuffer:f32_tensor->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		f32_tensor->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			f32_tensor->has_pending_work = 0;
		}
	}

	return f32_tensor;
}

/* Convert f32 GPU tensor to bf16, writing into pre-allocated bf16 tensor.
 * No allocation, batch-mode safe. */
int iris_gpu_convert_f32_to_bf16_into(iris_gpu_tensor_t bf16_out, iris_gpu_tensor_t f32_in) {
	if (!g_shaders_initialized || !g_f32_to_bf16_pipeline)
		return 0;
	if (!bf16_out || !f32_in || !bf16_out->is_f16 || f32_in->is_f16)
		return 0;

	@autoreleasepool {
		int                          n         = (int)f32_in->num_elements;
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_f32_to_bf16_pipeline];
		[encoder setBuffer:f32_in->buffer offset:0 atIndex:0];
		[encoder setBuffer:bf16_out->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		bf16_out->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			bf16_out->has_pending_work = 0;
		}
	}
	return 1;
}

/* Convert bf16 GPU tensor to f32, writing into pre-allocated f32 tensor.
 * No allocation, batch-mode safe. */
int iris_gpu_convert_bf16_to_f32_into(iris_gpu_tensor_t f32_out, iris_gpu_tensor_t bf16_in) {
	if (!g_shaders_initialized || !g_bf16_to_f32_pipeline)
		return 0;
	if (!f32_out || !bf16_in || f32_out->is_f16 || !bf16_in->is_f16)
		return 0;

	@autoreleasepool {
		int                          n         = (int)bf16_in->num_elements;
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_bf16_to_f32_pipeline];
		[encoder setBuffer:bf16_in->buffer offset:0 atIndex:0];
		[encoder setBuffer:f32_out->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = (n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		f32_out->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			f32_out->has_pending_work = 0;
		}
	}
	return 1;
}

iris_gpu_tensor_t iris_gpu_linear_bf16_native(iris_gpu_tensor_t x, const uint16_t *W_bf16, int seq_len, int in_dim, int out_dim) {
	if (!g_shaders_initialized || !x || !x->is_f16 || !W_bf16)
		return NULL;

	iris_gpu_tensor_t out = iris_gpu_tensor_alloc_f16((size_t)seq_len * out_dim);
	if (!out)
		return NULL;

	if (!iris_gpu_linear_bf16_native_into(out, x, W_bf16, seq_len, in_dim, out_dim)) {
		iris_gpu_tensor_free(out);
		return NULL;
	}

	return out;
}

/* MPSGraph bf16 linear writing into a pre-allocated bf16 output tensor. */
static int iris_gpu_linear_bf16_mpsgraph_into(iris_gpu_tensor_t out, iris_gpu_tensor_t x, const uint16_t *W_bf16, int seq_len, int in_dim, int out_dim) {
	if (!g_initialized || !out || !x || !W_bf16)
		return 0;
	if (!out->is_f16 || !x->is_f16)
		return 0;
	if (out->num_elements != (size_t)seq_len * (size_t)out_dim)
		return 0;

	size_t        numW = (size_t)out_dim * (size_t)in_dim;
	id<MTLBuffer> bufW = get_cached_bf16_buffer(W_bf16, numW);
	if (!bufW)
		return 0;

	/* Fast path: try bf16 matmul directly (no fp32 casts). If unsupported, MPSGraph
	 * will throw; disable this path for the remainder of the run and fall back to
	 * the fp32-cast graph (still MPSGraph, much faster than our custom kernel). */
	/* Heuristic: bf16 matmul helps a lot for very wide outputs (single-block fused
	 * projections, MLP up/gate). For narrower linears (hidden->hidden), it tends
	 * to help only at large seq_len (bigger images). */
	if (!g_linear_graph_bf16_disabled && (out_dim >= 8192 || seq_len >= 8192)) {
		linear_graph_cache_t *cache_bf16 = get_linear_graph_cache_bf16(seq_len, in_dim, out_dim);
		if (cache_bf16 && cache_bf16->graph) {
			@autoreleasepool {
				id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
				if (!cmdBuffer)
					return 0;

				MPSCommandBuffer *mpsCmd = nil;
				if (g_tensor_batch_mode) {
					mpsCmd = [MPSCommandBuffer commandBufferWithCommandBuffer:cmdBuffer];
				}
				else {
					mpsCmd = [MPSCommandBuffer commandBufferFromCommandQueue:g_queue];
				}
				if (!mpsCmd)
					return 0;

				MPSGraphTensorData *xData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:x->buffer shape:cache_bf16->xShape dataType:MPSDataTypeBFloat16];
				MPSGraphTensorData *wData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:bufW shape:cache_bf16->wShape dataType:MPSDataTypeBFloat16];
				MPSGraphTensorData *outData = [[MPSGraphTensorData alloc] initWithMTLBuffer:out->buffer
				                                                                      shape:cache_bf16->outShape
				                                                                   dataType:MPSDataTypeBFloat16];

				NSDictionary *feeds   = @{cache_bf16->xTensor : xData, cache_bf16->wTensor : wData};
				NSDictionary *results = @{cache_bf16->outTensor : outData};

				int bf16_ok = 0;
				@try {
					[cache_bf16->graph encodeToCommandBuffer:mpsCmd feeds:feeds targetOperations:nil resultsDictionary:results executionDescriptor:nil];
					bf16_ok = 1;
				} @catch (NSException *exception) {
					/* If bf16 matmul isn't supported at all, the first attempt will
					 * throw consistently; disable the fast path to avoid repeated
					 * exceptions. If we've already seen successes, treat failures
					 * as shape-specific and just fall back for this call. */
					if (!g_linear_graph_bf16_seen_success) {
						g_linear_graph_bf16_disabled = 1;
					}
				}

				if (bf16_ok) {
					g_linear_graph_bf16_seen_success = 1;
					out->has_pending_work            = 1;
					x->has_pending_work              = 1;

					if (!g_tensor_batch_mode) {
						[mpsCmd commit];
						[mpsCmd waitUntilCompleted];
						out->has_pending_work = 0;
						x->has_pending_work   = 0;
					}
					else {
						/* MPSGraph may commit-and-continue; update the live buffer. */
						g_tensor_cmd = [mpsCmd rootCommandBuffer];
					}

					return 1;
				}
			}
		}
	}

	linear_graph_cache_t *cache = get_linear_graph_cache(seq_len, in_dim, out_dim);
	if (!cache || !cache->graph)
		return 0;

	@autoreleasepool {
		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		if (!cmdBuffer)
			return 0;

		MPSCommandBuffer *mpsCmd = nil;
		if (g_tensor_batch_mode) {
			mpsCmd = [MPSCommandBuffer commandBufferWithCommandBuffer:cmdBuffer];
		}
		else {
			mpsCmd = [MPSCommandBuffer commandBufferFromCommandQueue:g_queue];
		}
		if (!mpsCmd)
			return 0;

		MPSGraphTensorData *xData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:x->buffer shape:cache->xShape dataType:MPSDataTypeBFloat16];
		MPSGraphTensorData *wData   = [[MPSGraphTensorData alloc] initWithMTLBuffer:bufW shape:cache->wShape dataType:MPSDataTypeBFloat16];
		MPSGraphTensorData *outData = [[MPSGraphTensorData alloc] initWithMTLBuffer:out->buffer shape:cache->outShape dataType:MPSDataTypeBFloat16];

		NSDictionary *feeds   = @{cache->xTensor : xData, cache->wTensor : wData};
		NSDictionary *results = @{cache->outTensor : outData};

		@try {
			[cache->graph encodeToCommandBuffer:mpsCmd feeds:feeds targetOperations:nil resultsDictionary:results executionDescriptor:nil];
		} @catch (NSException *exception) {
			return 0;
		}

		out->has_pending_work = 1;
		x->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[mpsCmd commit];
			[mpsCmd waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
		else {
			/* MPSGraph may commit-and-continue; update the live buffer. */
			g_tensor_cmd = [mpsCmd rootCommandBuffer];
		}
	}

	return 1;
}

int iris_gpu_linear_bf16_native_into(iris_gpu_tensor_t out, iris_gpu_tensor_t x, const uint16_t *W_bf16, int seq_len, int in_dim, int out_dim) {
	if (!g_shaders_initialized || !out || !x || !W_bf16)
		return 0;
	if (!out->is_f16 || !x->is_f16)
		return 0;
	if (out->num_elements != (size_t)seq_len * (size_t)out_dim)
		return 0;

	/* MPSGraph linear is faster than our custom kernel. */
	if (bf16_linear_use_graph(seq_len, in_dim, out_dim)) {
		if (iris_gpu_linear_bf16_mpsgraph_into(out, x, W_bf16, seq_len, in_dim, out_dim)) {
			return 1;
		}
	}

	if (!g_linear_bf16_pipeline)
		return 0;

	@autoreleasepool {
		size_t        numW = (size_t)out_dim * (size_t)in_dim;
		id<MTLBuffer> bufW = get_cached_bf16_buffer(W_bf16, numW);
		if (!bufW)
			return 0;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_linear_bf16_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufW offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&seq_len length:sizeof(int) atIndex:3];
		[encoder setBytes:&in_dim length:sizeof(int) atIndex:4];
		[encoder setBytes:&out_dim length:sizeof(int) atIndex:5];

		uint    TILE_SIZE       = 16;
		MTLSize threadsPerGroup = MTLSizeMake(TILE_SIZE, TILE_SIZE, 1);
		MTLSize numGroups       = MTLSizeMake((out_dim + TILE_SIZE - 1) / TILE_SIZE, (seq_len + TILE_SIZE - 1) / TILE_SIZE, 1);
		[encoder dispatchThreadgroups:numGroups threadsPerThreadgroup:threadsPerGroup];
		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
	}

	return 1;
}

/* BF16 Split QKV+MLP: split fused output into separate tensors */
void iris_gpu_split_qkv_mlp_bf16(iris_gpu_tensor_t fused, iris_gpu_tensor_t q, iris_gpu_tensor_t k, iris_gpu_tensor_t v, iris_gpu_tensor_t gate,
                                 iris_gpu_tensor_t up, int seq, int hidden, int mlp_hidden) {
	if (!g_shaders_initialized || !g_split_qkv_mlp_bf16_pipeline)
		return;
	if (!fused || !q || !k || !v || !gate || !up)
		return;
	if (!fused->is_f16 || !q->is_f16 || !k->is_f16 || !v->is_f16 || !gate->is_f16 || !up->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_split_qkv_mlp_bf16_pipeline];
		[encoder setBuffer:fused->buffer offset:0 atIndex:0];
		[encoder setBuffer:q->buffer offset:0 atIndex:1];
		[encoder setBuffer:k->buffer offset:0 atIndex:2];
		[encoder setBuffer:v->buffer offset:0 atIndex:3];
		[encoder setBuffer:gate->buffer offset:0 atIndex:4];
		[encoder setBuffer:up->buffer offset:0 atIndex:5];
		[encoder setBytes:&seq length:sizeof(int) atIndex:6];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:7];
		[encoder setBytes:&mlp_hidden length:sizeof(int) atIndex:8];

		int max_dim = hidden > mlp_hidden ? hidden : mlp_hidden;
		[encoder dispatchThreads:MTLSizeMake(seq, max_dim, 1) threadsPerThreadgroup:MTLSizeMake(MIN(32, seq), MIN(32, max_dim), 1)];
		[encoder endEncoding];

		q->has_pending_work    = 1;
		k->has_pending_work    = 1;
		v->has_pending_work    = 1;
		gate->has_pending_work = 1;
		up->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			q->has_pending_work    = 0;
			k->has_pending_work    = 0;
			v->has_pending_work    = 0;
			gate->has_pending_work = 0;
			up->has_pending_work   = 0;
		}
	}
}

/* BF16 Concat attention + MLP outputs */
void iris_gpu_concat_attn_mlp_bf16(iris_gpu_tensor_t attn, iris_gpu_tensor_t mlp, iris_gpu_tensor_t out, int seq, int hidden, int mlp_hidden) {
	if (!g_shaders_initialized || !g_concat_attn_mlp_bf16_pipeline)
		return;
	if (!attn || !mlp || !out)
		return;
	if (!attn->is_f16 || !mlp->is_f16 || !out->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_concat_attn_mlp_bf16_pipeline];
		[encoder setBuffer:attn->buffer offset:0 atIndex:0];
		[encoder setBuffer:mlp->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&seq length:sizeof(int) atIndex:3];
		[encoder setBytes:&hidden length:sizeof(int) atIndex:4];
		[encoder setBytes:&mlp_hidden length:sizeof(int) atIndex:5];

		int max_dim = hidden > mlp_hidden ? hidden : mlp_hidden;
		[encoder dispatchThreads:MTLSizeMake(seq, max_dim, 1) threadsPerThreadgroup:MTLSizeMake(MIN(32, seq), MIN(32, max_dim), 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
		}
	}
}

/* BF16 Transpose for attention: [seq, heads*head_dim] -> [heads, seq, head_dim] */
void iris_gpu_transpose_to_heads_bf16(iris_gpu_tensor_t in, iris_gpu_tensor_t out, int seq, int heads, int head_dim) {
	if (!g_shaders_initialized || !g_transpose_to_heads_bf16_pipeline)
		return;
	if (!in || !out || !in->is_f16 || !out->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_transpose_to_heads_bf16_pipeline];
		[encoder setBuffer:in->buffer offset:0 atIndex:0];
		[encoder setBuffer:out->buffer offset:0 atIndex:1];
		[encoder setBytes:&seq length:sizeof(int) atIndex:2];
		[encoder setBytes:&heads length:sizeof(int) atIndex:3];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:4];

		/* Dispatch: one thread per element */
		[encoder dispatchThreadgroups:MTLSizeMake((head_dim + 7) / 8, (seq + 7) / 8, heads) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		in->has_pending_work  = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			in->has_pending_work  = 0;
		}
	}
}

/* BF16 Transpose for attention output: [heads, seq, head_dim] -> [seq, heads*head_dim] */
void iris_gpu_transpose_from_heads_bf16(iris_gpu_tensor_t in, iris_gpu_tensor_t out, int seq, int heads, int head_dim) {
	if (!g_shaders_initialized || !g_transpose_from_heads_bf16_pipeline)
		return;
	if (!in || !out || !in->is_f16 || !out->is_f16)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_transpose_from_heads_bf16_pipeline];
		[encoder setBuffer:in->buffer offset:0 atIndex:0];
		[encoder setBuffer:out->buffer offset:0 atIndex:1];
		[encoder setBytes:&seq length:sizeof(int) atIndex:2];
		[encoder setBytes:&heads length:sizeof(int) atIndex:3];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:4];

		/* Dispatch: one thread per element */
		[encoder dispatchThreadgroups:MTLSizeMake((head_dim + 7) / 8, (seq + 7) / 8, heads) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		in->has_pending_work  = 1;

		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			in->has_pending_work  = 0;
		}
	}
}

/* ========================================================================
 * Causal Attention for Text Encoder (Qwen3)
 * Fused GPU kernel that processes all heads in parallel with causal masking.
 * ======================================================================== */

int iris_metal_causal_attention(float *out, const float *Q, const float *K, const float *V, const int *attention_mask, int seq, int num_q_heads,
                                int num_kv_heads, int head_dim, float scale) {
	if (!g_shaders_initialized || !g_causal_attention_pipeline) {
		return 0; /* Shader not available, fall back to CPU */
	}

	/* Limit seq length to what the shader can handle (512 for shared memory) */
	if (seq > 512) {
		return 0; /* Fall back to CPU for long sequences */
	}

	@autoreleasepool {
		size_t q_size    = (size_t)seq * num_q_heads * head_dim * sizeof(float);
		size_t kv_size   = (size_t)seq * num_kv_heads * head_dim * sizeof(float);
		size_t out_size  = q_size;
		size_t mask_size = (size_t)seq * sizeof(int);

		/* Create GPU buffers */
		id<MTLBuffer> bufQ    = pool_get_buffer(q_size);
		id<MTLBuffer> bufK    = pool_get_buffer(kv_size);
		id<MTLBuffer> bufV    = pool_get_buffer(kv_size);
		id<MTLBuffer> bufOut  = pool_get_buffer(out_size);
		id<MTLBuffer> bufMask = attention_mask ? pool_get_buffer(mask_size) : nil;

		if (!bufQ || !bufK || !bufV || !bufOut) {
			if (bufQ)
				pool_release_buffer(bufQ);
			if (bufK)
				pool_release_buffer(bufK);
			if (bufV)
				pool_release_buffer(bufV);
			if (bufOut)
				pool_release_buffer(bufOut);
			if (bufMask)
				pool_release_buffer(bufMask);
			return 0;
		}

		/* Copy input data */
		memcpy([bufQ contents], Q, q_size);
		memcpy([bufK contents], K, kv_size);
		memcpy([bufV contents], V, kv_size);
		if (attention_mask && bufMask) {
			memcpy([bufMask contents], attention_mask, mask_size);
		}

		int use_mask = (attention_mask != NULL) ? 1 : 0;

		/* Create command buffer and encode kernel */
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_causal_attention_pipeline];
		[encoder setBuffer:bufQ offset:0 atIndex:0];
		[encoder setBuffer:bufK offset:0 atIndex:1];
		[encoder setBuffer:bufV offset:0 atIndex:2];
		[encoder setBuffer:bufOut offset:0 atIndex:3];
		if (bufMask) {
			[encoder setBuffer:bufMask offset:0 atIndex:4];
		}
		else {
			/* Set a dummy buffer for null mask - kernel will use use_mask flag */
			[encoder setBuffer:bufQ offset:0 atIndex:4];
		}
		[encoder setBytes:&seq length:sizeof(int) atIndex:5];
		[encoder setBytes:&num_q_heads length:sizeof(int) atIndex:6];
		[encoder setBytes:&num_kv_heads length:sizeof(int) atIndex:7];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:8];
		[encoder setBytes:&scale length:sizeof(float) atIndex:9];
		[encoder setBytes:&use_mask length:sizeof(int) atIndex:10];

		/* Dispatch: one threadgroup per (query_pos, head) pair
		 * Each threadgroup has threads for parallel reduction (softmax) */
		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq);
		[encoder dispatchThreadgroups:MTLSizeMake(seq, num_q_heads, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		/* Execute and wait */
		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		/* Copy output back to CPU */
		memcpy(out, [bufOut contents], out_size);

		/* Release buffers */
		pool_release_buffer(bufQ);
		pool_release_buffer(bufK);
		pool_release_buffer(bufV);
		pool_release_buffer(bufOut);
		if (bufMask)
			pool_release_buffer(bufMask);

		return 1; /* Success */
	}
}

/* Fused non-causal attention for transformer
 * Works directly on [seq, hidden] layout without transpose.
 * Returns 1 on success, 0 to fall back to CPU.
 */
int iris_metal_attention_fused(float *out, const float *Q, const float *K, const float *V, int seq_q, int seq_k, int num_heads, int head_dim, float scale) {
	if (!g_shaders_initialized || !g_attention_fused_pipeline) {
		return 0; /* Shader not available, fall back to CPU */
	}

	/* Dynamic threadgroup memory: shared_scores[seq_k] + static shared_max/sum */
	NSUInteger raw_scores_size = (NSUInteger)seq_k * sizeof(float);
	NSUInteger raw_static_size = 256 * sizeof(float) * 2; /* shared_max + shared_sum */
	if (raw_scores_size + raw_static_size > 32768) {
		return 0; /* Exceeds 32KB threadgroup memory limit */
	}

	@autoreleasepool {
		int    hidden   = num_heads * head_dim;
		size_t q_size   = (size_t)seq_q * hidden * sizeof(float);
		size_t kv_size  = (size_t)seq_k * hidden * sizeof(float);
		size_t out_size = q_size;

		/* Create GPU buffers */
		id<MTLBuffer> bufQ   = pool_get_buffer(q_size);
		id<MTLBuffer> bufK   = pool_get_buffer(kv_size);
		id<MTLBuffer> bufV   = pool_get_buffer(kv_size);
		id<MTLBuffer> bufOut = pool_get_buffer(out_size);

		if (!bufQ || !bufK || !bufV || !bufOut) {
			if (bufQ)
				pool_release_buffer(bufQ);
			if (bufK)
				pool_release_buffer(bufK);
			if (bufV)
				pool_release_buffer(bufV);
			if (bufOut)
				pool_release_buffer(bufOut);
			return 0;
		}

		/* Copy input data */
		memcpy([bufQ contents], Q, q_size);
		memcpy([bufK contents], K, kv_size);
		memcpy([bufV contents], V, kv_size);

		/* Create command buffer and encode kernel */
		id<MTLCommandBuffer>         cmdBuffer = [g_queue commandBuffer];
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_attention_fused_pipeline];
		[encoder setBuffer:bufQ offset:0 atIndex:0];
		[encoder setBuffer:bufK offset:0 atIndex:1];
		[encoder setBuffer:bufV offset:0 atIndex:2];
		[encoder setBuffer:bufOut offset:0 atIndex:3];
		[encoder setBytes:&seq_q length:sizeof(int) atIndex:4];
		[encoder setBytes:&seq_k length:sizeof(int) atIndex:5];
		[encoder setBytes:&num_heads length:sizeof(int) atIndex:6];
		[encoder setBytes:&head_dim length:sizeof(int) atIndex:7];
		[encoder setBytes:&scale length:sizeof(float) atIndex:8];
		[encoder setThreadgroupMemoryLength:raw_scores_size atIndex:0];

		/* Dispatch: one threadgroup per (query_pos, head) pair
		 * Each threadgroup has threads for parallel reduction (softmax) */
		NSUInteger threadsPerGroup = MIN(256, (NSUInteger)seq_k);
		[encoder dispatchThreadgroups:MTLSizeMake(seq_q, num_heads, 1) threadsPerThreadgroup:MTLSizeMake(threadsPerGroup, 1, 1)];

		[encoder endEncoding];

		/* Execute and wait */
		[cmdBuffer commit];
		[cmdBuffer waitUntilCompleted];

		/* Copy output back to CPU */
		memcpy(out, [bufOut contents], out_size);

		/* Release buffers */
		pool_release_buffer(bufQ);
		pool_release_buffer(bufK);
		pool_release_buffer(bufV);
		pool_release_buffer(bufOut);

		return 1; /* Success */
	}
}

/* ========================================================================
 * F32 VAE GPU Operations
 * ======================================================================== */

/* GroupNorm on f32 GPU tensors */
void iris_gpu_group_norm_f32(iris_gpu_tensor_t out, iris_gpu_tensor_t x, const float *gamma, const float *beta, int batch, int channels, int spatial,
                             int num_groups, float eps) {
	if (!g_shaders_initialized || !g_group_norm_f32_pipeline)
		return;
	if (!out || !x || !gamma || !beta)
		return;

	@autoreleasepool {
		size_t        gamma_size = (size_t)channels * sizeof(float);
		id<MTLBuffer> bufGamma   = get_cached_weight_buffer(gamma, gamma_size);
		id<MTLBuffer> bufBeta    = get_cached_weight_buffer(beta, gamma_size);
		if (!bufGamma || !bufBeta)
			return;

		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		int channels_per_group = channels / num_groups;
		int total_groups       = batch * num_groups;

		[encoder setComputePipelineState:g_group_norm_f32_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:bufGamma offset:0 atIndex:1];
		[encoder setBuffer:bufBeta offset:0 atIndex:2];
		[encoder setBuffer:out->buffer offset:0 atIndex:3];
		[encoder setBytes:&channels length:sizeof(int) atIndex:4];
		[encoder setBytes:&spatial length:sizeof(int) atIndex:5];
		[encoder setBytes:&channels_per_group length:sizeof(int) atIndex:6];
		[encoder setBytes:&eps length:sizeof(float) atIndex:7];

		/* One threadgroup per (batch, group) pair */
		NSUInteger threads_per_group = 256;
		[encoder dispatchThreadgroups:MTLSizeMake(total_groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads_per_group, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
	}
}

/* Swish/SiLU on f32 GPU tensor */
void iris_gpu_swish_f32(iris_gpu_tensor_t out, iris_gpu_tensor_t x, int n) {
	if (!g_shaders_initialized || !g_swish_f32_pipeline)
		return;
	if (!out || !x || n <= 0)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_swish_f32_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:out->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];

		NSUInteger threads = 256;
		NSUInteger groups  = ((NSUInteger)n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
	}
}

/* Element-wise add on f32 GPU tensors */
void iris_gpu_add_f32(iris_gpu_tensor_t out, iris_gpu_tensor_t a, iris_gpu_tensor_t b, int n) {
	if (!g_shaders_initialized || !g_add_f32_pipeline)
		return;
	if (!out || !a || !b || n <= 0)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_add_f32_pipeline];
		[encoder setBuffer:a->buffer offset:0 atIndex:0];
		[encoder setBuffer:b->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&n length:sizeof(int) atIndex:3];

		NSUInteger threads = 256;
		NSUInteger groups  = ((NSUInteger)n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		a->has_pending_work   = 1;
		b->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			a->has_pending_work   = 0;
			b->has_pending_work   = 0;
		}
	}
}

/* In-place-safe LeakyReLU on f32 GPU tensor: out = x >= 0 ? x : slope*x */
void iris_gpu_leaky_relu_f32(iris_gpu_tensor_t out, iris_gpu_tensor_t x, int n, float slope) {
	if (!g_shaders_initialized || !g_leaky_relu_f32_pipeline)
		return;
	if (!out || !x || n <= 0)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_leaky_relu_f32_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:out->buffer offset:0 atIndex:1];
		[encoder setBytes:&n length:sizeof(int) atIndex:2];
		[encoder setBytes:&slope length:sizeof(float) atIndex:3];

		NSUInteger threads = 256;
		NSUInteger groups  = ((NSUInteger)n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
	}
}

/* Scaled residual add on f32 GPU tensors: out = scale*a + b */
void iris_gpu_scale_add_f32(iris_gpu_tensor_t out, iris_gpu_tensor_t a, iris_gpu_tensor_t b, float scale, int n) {
	if (!g_shaders_initialized || !g_scale_add_f32_pipeline)
		return;
	if (!out || !a || !b || n <= 0)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_scale_add_f32_pipeline];
		[encoder setBuffer:a->buffer offset:0 atIndex:0];
		[encoder setBuffer:b->buffer offset:0 atIndex:1];
		[encoder setBuffer:out->buffer offset:0 atIndex:2];
		[encoder setBytes:&n length:sizeof(int) atIndex:3];
		[encoder setBytes:&scale length:sizeof(float) atIndex:4];

		NSUInteger threads = 256;
		NSUInteger groups  = ((NSUInteger)n + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		a->has_pending_work   = 1;
		b->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			a->has_pending_work   = 0;
			b->has_pending_work   = 0;
		}
	}
}

/* Nearest neighbor 2x upsample on f32 GPU tensor */
iris_gpu_tensor_t iris_gpu_upsample_nearest_2x_f32(iris_gpu_tensor_t x, int channels, int H, int W) {
	if (!g_shaders_initialized || !g_upsample_nearest_2x_f32_pipeline)
		return NULL;
	if (!x || channels <= 0 || H <= 0 || W <= 0)
		return NULL;

	int               out_h     = H * 2;
	int               out_w     = W * 2;
	size_t            out_elems = (size_t)channels * out_h * out_w;
	iris_gpu_tensor_t out       = iris_gpu_tensor_alloc(out_elems);
	if (!out)
		return NULL;

	@autoreleasepool {
		id<MTLCommandBuffer>         cmdBuffer = get_tensor_cmd();
		id<MTLComputeCommandEncoder> encoder   = [cmdBuffer computeCommandEncoder];

		[encoder setComputePipelineState:g_upsample_nearest_2x_f32_pipeline];
		[encoder setBuffer:x->buffer offset:0 atIndex:0];
		[encoder setBuffer:out->buffer offset:0 atIndex:1];
		[encoder setBytes:&channels length:sizeof(int) atIndex:2];
		[encoder setBytes:&H length:sizeof(int) atIndex:3];
		[encoder setBytes:&W length:sizeof(int) atIndex:4];

		NSUInteger threads = 256;
		NSUInteger groups  = (out_elems + threads - 1) / threads;
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
		[encoder endEncoding];

		out->has_pending_work = 1;
		x->has_pending_work   = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
	}

	return out;
}

/* GPU blit copy for f32 tensors */
void iris_gpu_copy_f32(iris_gpu_tensor_t dst, iris_gpu_tensor_t src, size_t n) {
	if (!g_initialized || !dst || !src || n == 0)
		return;
	@autoreleasepool {
		id<MTLCommandBuffer>      cmdBuffer = get_tensor_cmd();
		id<MTLBlitCommandEncoder> blit      = [cmdBuffer blitCommandEncoder];
		size_t                    bytes     = n * sizeof(float);
		[blit copyFromBuffer:src->buffer sourceOffset:0 toBuffer:dst->buffer destinationOffset:0 size:bytes];
		[blit endEncoding];
		dst->has_pending_work = 1;
		src->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			dst->has_pending_work = 0;
			src->has_pending_work = 0;
		}
	}
}

/* GPU blit copy for f32 tensors with element offsets */
void iris_gpu_copy_region_f32(iris_gpu_tensor_t dst, size_t dst_offset, iris_gpu_tensor_t src, size_t src_offset, size_t n) {
	if (!g_initialized || !dst || !src || n == 0)
		return;
	if (dst_offset + n > dst->num_elements)
		return;
	if (src_offset + n > src->num_elements)
		return;

	@autoreleasepool {
		id<MTLCommandBuffer>      cmdBuffer = get_tensor_cmd();
		id<MTLBlitCommandEncoder> blit      = [cmdBuffer blitCommandEncoder];

		[blit copyFromBuffer:src->buffer
		         sourceOffset:src_offset * sizeof(float)
		             toBuffer:dst->buffer
		    destinationOffset:dst_offset * sizeof(float)
		                 size:n * sizeof(float)];
		[blit endEncoding];

		dst->has_pending_work = 1;
		src->has_pending_work = 1;
		if (!g_tensor_batch_mode) {
			[cmdBuffer commit];
			[cmdBuffer waitUntilCompleted];
			dst->has_pending_work = 0;
			src->has_pending_work = 0;
		}
	}
}

/* Conv2d on f32 GPU tensors using MPSGraph within the batch system */
iris_gpu_tensor_t iris_gpu_conv2d_f32(iris_gpu_tensor_t x, const float *weight, const float *bias, int batch, int in_ch, int out_ch, int H, int W, int kH,
                                      int kW, int stride, int padding) {
	if (!g_initialized || !x || !weight || !bias)
		return NULL;
	if (batch <= 0 || in_ch <= 0 || out_ch <= 0 || H <= 0 || W <= 0 || kH <= 0 || kW <= 0 || stride <= 0)
		return NULL;

	conv2d_graph_cache_t *cache = get_conv2d_graph_cache(batch, in_ch, out_ch, H, W, kH, kW, stride, padding, iris_circular);
	if (!cache || !cache->graph)
		return NULL;

	int outH = (H + 2 * padding - kH) / stride + 1;
	int outW = (W + 2 * padding - kW) / stride + 1;
	if (outH <= 0 || outW <= 0)
		return NULL;

	size_t out_elems = (size_t)batch * out_ch * outH * outW;
	size_t w_bytes   = (size_t)out_ch * in_ch * kH * kW * sizeof(float);
	size_t b_bytes   = (size_t)out_ch * sizeof(float);

	iris_gpu_tensor_t out = iris_gpu_tensor_alloc(out_elems);
	if (!out)
		return NULL;

	@autoreleasepool {
		id<MTLBuffer> w_buf = get_cached_weight_buffer(weight, w_bytes);
		id<MTLBuffer> b_buf = get_cached_weight_buffer(bias, b_bytes);
		if (!w_buf || !b_buf) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		id<MTLCommandBuffer> cmdBuffer = get_tensor_cmd();
		if (!cmdBuffer) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		MPSCommandBuffer *mpsCmd = nil;
		if (g_tensor_batch_mode) {
			mpsCmd = [MPSCommandBuffer commandBufferWithCommandBuffer:cmdBuffer];
		}
		else {
			mpsCmd = [MPSCommandBuffer commandBufferFromCommandQueue:g_queue];
		}
		if (!mpsCmd) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		MPSGraphTensorData *in_data  = [[MPSGraphTensorData alloc] initWithMTLBuffer:x->buffer shape:cache->inputShape dataType:MPSDataTypeFloat32];
		MPSGraphTensorData *w_data   = [[MPSGraphTensorData alloc] initWithMTLBuffer:w_buf shape:cache->weightShape dataType:MPSDataTypeFloat32];
		MPSGraphTensorData *b_data   = [[MPSGraphTensorData alloc] initWithMTLBuffer:b_buf shape:cache->biasShape dataType:MPSDataTypeFloat32];
		MPSGraphTensorData *out_data = [[MPSGraphTensorData alloc] initWithMTLBuffer:out->buffer shape:cache->outShape dataType:MPSDataTypeFloat32];
		if (!in_data || !w_data || !b_data || !out_data) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		NSDictionary *feeds   = @{cache->inputTensor : in_data, cache->weightTensor : w_data, cache->biasTensor : b_data};
		NSDictionary *results = @{cache->outTensor : out_data};

		@try {
			[cache->graph encodeToCommandBuffer:mpsCmd feeds:feeds targetOperations:nil resultsDictionary:results executionDescriptor:nil];
		} @catch (NSException *exception) {
			iris_gpu_tensor_free(out);
			return NULL;
		}

		out->has_pending_work = 1;
		x->has_pending_work   = 1;

		if (!g_tensor_batch_mode) {
			[mpsCmd commit];
			[mpsCmd waitUntilCompleted];
			out->has_pending_work = 0;
			x->has_pending_work   = 0;
		}
		else {
			/* MPSGraph may commit-and-continue; update the live buffer. */
			g_tensor_cmd = [mpsCmd rootCommandBuffer];
		}
	}

	return out;
}
