/*
 * iris_shaders.metal - Metal compute shaders for Iris inference
 *
 * These kernels accelerate operations that run on CPU otherwise:
 * - RMSNorm (used in QK normalization)
 * - LayerNorm + AdaLN modulation
 * - SiLU activation
 * - Softmax (row-wise)
 */

#include <metal_stdlib>
#include <metal_simdgroup>
using namespace metal;

inline float bf16_to_f32(ushort bf16);
inline ushort f32_to_bf16(float f32);

/* ========================================================================
 * RMSNorm: out[i] = x[i] * rsqrt(mean(x^2) + eps) * weight[i]
 * ======================================================================== */

/* RMSNorm kernel - processes one row per threadgroup
 * x: [seq, hidden], weight: [hidden], out: [seq, hidden]
 */
kernel void rms_norm(
    device const float *x [[buffer(0)]],
    device const float *weight [[buffer(1)]],
    device float *out [[buffer(2)]],
    constant int &hidden [[buffer(3)]],
    constant float &eps [[buffer(4)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    threadgroup float shared_sum[256];  // For reduction

    device const float *x_row = x + row * hidden;
    device float *out_row = out + row * hidden;

    // Compute partial sum of squares
    float local_sum = 0.0f;
    for (int i = tid; i < hidden; i += threads) {
        float val = x_row[i];
        local_sum += val * val;
    }
    shared_sum[tid] = local_sum;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Parallel reduction for sum
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // Compute RMS inverse
    float rms_inv = rsqrt(shared_sum[0] / float(hidden) + eps);

    // Apply normalization with weight
    for (int i = tid; i < hidden; i += threads) {
        out_row[i] = x_row[i] * rms_inv * weight[i];
    }
}

/* QK RMSNorm - processes Q and K for all heads in a sequence position
 * q: [seq, heads*head_dim], k: [seq, heads*head_dim]
 * q_weight, k_weight: [head_dim]
 */
kernel void qk_rms_norm(
    device float *q [[buffer(0)]],
    device float *k [[buffer(1)]],
    device const float *q_weight [[buffer(2)]],
    device const float *k_weight [[buffer(3)]],
    constant int &heads [[buffer(4)]],
    constant int &head_dim [[buffer(5)]],
    constant float &eps [[buffer(6)]],
    uint2 pos [[thread_position_in_grid]]  // (seq_idx, head_idx)
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    uint hidden = heads * head_dim;
    uint offset = seq_idx * hidden + head_idx * head_dim;

    // RMSNorm for Q
    float sum_sq = 0.0f;
    for (int d = 0; d < head_dim; d++) {
        float val = q[offset + d];
        sum_sq += val * val;
    }
    float rms_inv = rsqrt(sum_sq / float(head_dim) + eps);
    for (int d = 0; d < head_dim; d++) {
        q[offset + d] = q[offset + d] * rms_inv * q_weight[d];
    }

    // RMSNorm for K
    sum_sq = 0.0f;
    for (int d = 0; d < head_dim; d++) {
        float val = k[offset + d];
        sum_sq += val * val;
    }
    rms_inv = rsqrt(sum_sq / float(head_dim) + eps);
    for (int d = 0; d < head_dim; d++) {
        k[offset + d] = k[offset + d] * rms_inv * k_weight[d];
    }
}

/* ========================================================================
 * LayerNorm + AdaLN modulation
 * out = (1 + scale) * norm(x) + shift
 * where norm(x) = (x - mean) / sqrt(var + eps)
 * ======================================================================== */

kernel void adaln_norm(
    device const float *x [[buffer(0)]],
    device const float *shift [[buffer(1)]],
    device const float *scale [[buffer(2)]],
    device float *out [[buffer(3)]],
    constant int &hidden [[buffer(4)]],
    constant float &eps [[buffer(5)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    threadgroup float shared_sum[256];
    threadgroup float shared_sum_sq[256];

    device const float *x_row = x + row * hidden;
    device float *out_row = out + row * hidden;

    // Compute partial sums for mean and variance
    float local_sum = 0.0f;
    float local_sum_sq = 0.0f;
    for (int i = tid; i < hidden; i += threads) {
        float val = x_row[i];
        local_sum += val;
        local_sum_sq += val * val;
    }
    shared_sum[tid] = local_sum;
    shared_sum_sq[tid] = local_sum_sq;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Parallel reduction
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
            shared_sum_sq[tid] += shared_sum_sq[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // Compute mean and std_inv
    float mean = shared_sum[0] / float(hidden);
    float var = shared_sum_sq[0] / float(hidden) - mean * mean;
    float std_inv = rsqrt(var + eps);

    // Apply LayerNorm + AdaLN modulation
    for (int i = tid; i < hidden; i += threads) {
        float norm = (x_row[i] - mean) * std_inv;
        out_row[i] = (1.0f + scale[i]) * norm + shift[i];
    }
}

/* ========================================================================
 * SiLU activation: x * sigmoid(x) = x / (1 + exp(-x))
 * ======================================================================== */

kernel void silu(
    device float *x [[buffer(0)]],
    constant int &n [[buffer(1)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float val = x[gid];
        x[gid] = val / (1.0f + exp(-val));
    }
}

/* SiLU with multiply: gate = silu(gate) * up (SwiGLU style) */
kernel void silu_mul(
    device float *gate [[buffer(0)]],
    device const float *up [[buffer(1)]],
    constant int &n [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float g = gate[gid];
        float silu_g = g / (1.0f + exp(-g));
        gate[gid] = silu_g * up[gid];
    }
}

/* ========================================================================
 * Gated Add: out += gate * proj
 * gate: [hidden], proj: [seq, hidden], out: [seq, hidden]
 * ======================================================================== */

kernel void gated_add(
    device float *out [[buffer(0)]],
    device const float *gate [[buffer(1)]],
    device const float *proj [[buffer(2)]],
    constant int &seq [[buffer(3)]],
    constant int &hidden [[buffer(4)]],
    uint2 pos [[thread_position_in_grid]]  // (seq_idx, hidden_idx)
) {
    uint s = pos.x;
    uint h = pos.y;
    if (s < uint(seq) && h < uint(hidden)) {
        uint idx = s * hidden + h;
        out[idx] += gate[h] * proj[idx];
    }
}

/* ========================================================================
 * Split Fused QKV+MLP Output
 * fused: [seq, fused_dim] where fused_dim = hidden*3 + mlp_hidden*2
 * Splits into: q, k, v [seq, hidden], gate, up [seq, mlp_hidden]
 * ======================================================================== */

kernel void split_qkv_mlp(
    device const float *fused [[buffer(0)]],  // [seq, fused_dim]
    device float *q [[buffer(1)]],            // [seq, hidden]
    device float *k [[buffer(2)]],            // [seq, hidden]
    device float *v [[buffer(3)]],            // [seq, hidden]
    device float *gate [[buffer(4)]],         // [seq, mlp_hidden]
    device float *up [[buffer(5)]],           // [seq, mlp_hidden]
    constant int &seq [[buffer(6)]],
    constant int &hidden [[buffer(7)]],
    constant int &mlp_hidden [[buffer(8)]],
    uint2 pos [[thread_position_in_grid]]  // (seq_idx, element_idx within largest output)
) {
    uint s = pos.x;
    uint e = pos.y;

    if (s >= uint(seq)) return;

    int fused_dim = hidden * 3 + mlp_hidden * 2;
    device const float *row = fused + s * fused_dim;

    // Copy Q (dims 0 to hidden-1)
    if (e < uint(hidden)) {
        q[s * hidden + e] = row[e];
        k[s * hidden + e] = row[hidden + e];
        v[s * hidden + e] = row[hidden * 2 + e];
    }

    // Copy gate and up (dims 0 to mlp_hidden-1)
    if (e < uint(mlp_hidden)) {
        gate[s * mlp_hidden + e] = row[hidden * 3 + e];
        up[s * mlp_hidden + e] = row[hidden * 3 + mlp_hidden + e];
    }
}

/* ========================================================================
 * Concat Attention + MLP outputs for fused projection
 * attn: [seq, hidden], mlp: [seq, mlp_hidden]
 * out: [seq, hidden + mlp_hidden]
 * ======================================================================== */

kernel void concat_attn_mlp(
    device const float *attn [[buffer(0)]],   // [seq, hidden]
    device const float *mlp [[buffer(1)]],    // [seq, mlp_hidden]
    device float *out [[buffer(2)]],          // [seq, hidden + mlp_hidden]
    constant int &seq [[buffer(3)]],
    constant int &hidden [[buffer(4)]],
    constant int &mlp_hidden [[buffer(5)]],
    uint2 pos [[thread_position_in_grid]]  // (seq_idx, element_idx)
) {
    uint s = pos.x;
    uint e = pos.y;

    if (s >= uint(seq)) return;

    int out_dim = hidden + mlp_hidden;
    device float *out_row = out + s * out_dim;

    // Copy attention output
    if (e < uint(hidden)) {
        out_row[e] = attn[s * hidden + e];
    }

    // Copy MLP output
    if (e < uint(mlp_hidden)) {
        out_row[hidden + e] = mlp[s * mlp_hidden + e];
    }
}

/* ========================================================================
 * Softmax (row-wise): out[i] = exp(x[i] - max) / sum(exp(x - max))
 * ======================================================================== */

kernel void softmax(
    device float *x [[buffer(0)]],
    constant int &rows [[buffer(1)]],
    constant int &cols [[buffer(2)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];

    device float *row_ptr = x + row * cols;

    // Find max (for numerical stability)
    float local_max = -INFINITY;
    for (int i = tid; i < cols; i += threads) {
        local_max = max(local_max, row_ptr[i]);
    }
    shared_max[tid] = local_max;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Reduce to find global max
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // Compute exp and sum
    float local_sum = 0.0f;
    for (int i = tid; i < cols; i += threads) {
        float e = exp(row_ptr[i] - max_val);
        row_ptr[i] = e;  // Store exp temporarily
        local_sum += e;
    }
    shared_sum[tid] = local_sum;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Reduce to find total sum
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float sum = shared_sum[0];

    // Normalize
    float inv_sum = 1.0f / sum;
    for (int i = tid; i < cols; i += threads) {
        row_ptr[i] *= inv_sum;
    }
}

/* ========================================================================
 * RoPE (Rotary Position Embeddings)
 * ======================================================================== */

/* Apply 2D RoPE to Q or K tensor
 * x: [seq, heads*head_dim]
 * cos, sin: [seq, head_dim]  (precomputed frequencies)
 */
kernel void apply_rope_2d(
    device float *x [[buffer(0)]],
    device const float *cos_freq [[buffer(1)]],
    device const float *sin_freq [[buffer(2)]],
    constant int &seq [[buffer(3)]],
    constant int &heads [[buffer(4)]],
    constant int &head_dim [[buffer(5)]],
    constant int &axis_dim [[buffer(6)]],  // 32 for FLUX
    uint2 pos [[thread_position_in_grid]]  // (seq_idx, head_idx)
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    if (seq_idx >= uint(seq) || head_idx >= uint(heads)) return;

    uint hidden = heads * head_dim;
    device float *vec = x + seq_idx * hidden + head_idx * head_dim;
    device const float *cos_row = cos_freq + seq_idx * head_dim;
    device const float *sin_row = sin_freq + seq_idx * head_dim;

    // RoPE rotation for each axis (4 axes of 32 dims each = 128)
    int half_axis = axis_dim / 2;  // 16

    for (int axis = 0; axis < 4; axis++) {
        int axis_offset = axis * axis_dim;
        for (int d = 0; d < half_axis; d++) {
            int i0 = axis_offset + d;
            int i1 = axis_offset + half_axis + d;

            float c = cos_row[i0];
            float s = sin_row[i0];

            float x0 = vec[i0];
            float x1 = vec[i1];

            vec[i0] = x0 * c - x1 * s;
            vec[i1] = x0 * s + x1 * c;
        }
    }
}

/* Apply 2D RoPE to bf16 Q or K tensor
 * x: [seq, heads*head_dim] (bf16)
 * cos, sin: [seq, head_dim] (f32)
 */
kernel void apply_rope_2d_bf16(
    device ushort *x [[buffer(0)]],
    device const float *cos_freq [[buffer(1)]],
    device const float *sin_freq [[buffer(2)]],
    constant int &seq [[buffer(3)]],
    constant int &heads [[buffer(4)]],
    constant int &head_dim [[buffer(5)]],
    constant int &axis_dim [[buffer(6)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    if (seq_idx >= uint(seq) || head_idx >= uint(heads)) return;

    uint hidden = heads * head_dim;
    device ushort *vec = x + seq_idx * hidden + head_idx * head_dim;
    device const float *cos_row = cos_freq + seq_idx * head_dim;
    device const float *sin_row = sin_freq + seq_idx * head_dim;

    (void)axis_dim;
    for (int d = 0; d < head_dim; d += 2) {
        float c = cos_row[d];
        float s = sin_row[d];

        float x0 = bf16_to_f32(vec[d]);
        float x1 = bf16_to_f32(vec[d + 1]);

        vec[d] = f32_to_bf16(x0 * c - x1 * s);
        vec[d + 1] = f32_to_bf16(x1 * c + x0 * s);
    }
}

/* ========================================================================
 * Unified RoPE for Text+Image (Single Block Forward)
 * Applies different frequency tables to text and image portions in one pass.
 * Text portion: positions [0, img_offset)
 * Image portion: positions [img_offset, seq)
 * ======================================================================== */
kernel void apply_rope_unified(
    device float *x [[buffer(0)]],
    device const float *txt_cos [[buffer(1)]],
    device const float *txt_sin [[buffer(2)]],
    device const float *img_cos [[buffer(3)]],
    device const float *img_sin [[buffer(4)]],
    constant int &seq [[buffer(5)]],
    constant int &img_offset [[buffer(6)]],
    constant int &heads [[buffer(7)]],
    constant int &head_dim [[buffer(8)]],
    constant int &axis_dim [[buffer(9)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    if (seq_idx >= uint(seq) || head_idx >= uint(heads)) return;

    uint hidden = heads * head_dim;
    device float *vec = x + seq_idx * hidden + head_idx * head_dim;

    // Select appropriate frequency table based on position
    device const float *cos_row;
    device const float *sin_row;

    if (seq_idx < uint(img_offset)) {
        // Text portion: use text frequencies indexed by seq_idx
        cos_row = txt_cos + seq_idx * head_dim;
        sin_row = txt_sin + seq_idx * head_dim;
    } else {
        // Image portion: use image frequencies indexed by (seq_idx - img_offset)
        uint img_idx = seq_idx - uint(img_offset);
        cos_row = img_cos + img_idx * head_dim;
        sin_row = img_sin + img_idx * head_dim;
    }

    // RoPE rotation: apply to consecutive pairs (d, d+1) matching CPU implementation
    // cos[d] == cos[d+1] due to repeat_interleave in frequency generation
    for (int d = 0; d < head_dim; d += 2) {
        float c = cos_row[d];
        float s = sin_row[d];

        float x0 = vec[d];
        float x1 = vec[d + 1];

        // Complex rotation: (x0 + i*x1) * (cos + i*sin)
        vec[d] = x0 * c - x1 * s;
        vec[d + 1] = x1 * c + x0 * s;
    }
}

/* ========================================================================
 * Fused Non-Causal Attention for Transformer (FLUX)
 * Processes all heads in parallel without causal masking.
 *
 * This kernel computes one output row per threadgroup:
 * out[query_idx, head] = softmax(Q @ K^T * scale) @ V
 *
 * Works directly on [seq, heads*head_dim] layout without transpose.
 * Supports different Q and K/V sequence lengths (for joint attention).
 * ======================================================================== */

kernel void attention_fused(
    device const float *Q [[buffer(0)]],      // [seq_q, heads * head_dim]
    device const float *K [[buffer(1)]],      // [seq_k, heads * head_dim]
    device const float *V [[buffer(2)]],      // [seq_k, heads * head_dim]
    device float *out [[buffer(3)]],          // [seq_q, heads * head_dim]
    constant int &seq_q [[buffer(4)]],        // Query sequence length
    constant int &seq_k [[buffer(5)]],        // Key/Value sequence length
    constant int &num_heads [[buffer(6)]],
    constant int &head_dim [[buffer(7)]],
    constant float &scale [[buffer(8)]],
    threadgroup float *shared_scores [[threadgroup(0)]],  // [seq_k] dynamic
    uint3 tg_pos [[threadgroup_position_in_grid]],   // (query_idx, head_idx, 0)
    uint3 tid_pos [[thread_position_in_threadgroup]],
    uint3 tg_size [[threads_per_threadgroup]]
) {
    // Shared memory for reductions (shared_scores is dynamic via threadgroup(0))
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];

    int query_idx = tg_pos.x;
    int head_idx = tg_pos.y;
    uint tid = tid_pos.x;
    uint threads = tg_size.x;

    if (query_idx >= seq_q || head_idx >= num_heads) return;

    int hidden = num_heads * head_dim;

    // Pointers to this position's Q and output (layout: [seq, heads*head_dim])
    device const float *q_row = Q + query_idx * hidden + head_idx * head_dim;
    device float *out_row = out + query_idx * hidden + head_idx * head_dim;

    // K and V have same layout, head offset is same
    device const float *K_head = K + head_idx * head_dim;
    device const float *V_head = V + head_idx * head_dim;

    // ========== Phase 1: Compute Q @ K^T ==========
    float local_max = -INFINITY;

    for (int key_idx = tid; key_idx < seq_k; key_idx += threads) {
        // Dot product: Q[query_idx, head] · K[key_idx, head]
        float dot = 0.0f;
        device const float *k_row = K_head + key_idx * hidden;
        for (int d = 0; d < head_dim; d++) {
            dot += q_row[d] * k_row[d];
        }
        float score = dot * scale;
        shared_scores[key_idx] = score;
        local_max = max(local_max, score);
    }

    shared_max[tid] = local_max;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 2: Find global max ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // ========== Phase 3: Compute exp(score - max) and sum ==========
    float local_sum = 0.0f;
    for (int key_idx = tid; key_idx < seq_k; key_idx += threads) {
        float e = exp(shared_scores[key_idx] - max_val);
        shared_scores[key_idx] = e;
        local_sum += e;
    }

    shared_sum[tid] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 4: Find total sum ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float inv_sum = 1.0f / shared_sum[0];

    // ========== Phase 5: Normalize scores ==========
    for (int key_idx = tid; key_idx < seq_k; key_idx += threads) {
        shared_scores[key_idx] *= inv_sum;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 6: Compute output = scores @ V ==========
    for (int d = tid; d < head_dim; d += threads) {
        float acc = 0.0f;
        for (int key_idx = 0; key_idx < seq_k; key_idx++) {
            float v_val = V_head[key_idx * hidden + d];
            acc += shared_scores[key_idx] * v_val;
        }
        out_row[d] = acc;
    }
}

/* ========================================================================
 * Fused Causal Attention for Text Encoder (Qwen3)
 * Processes all heads in parallel with causal masking and GQA support.
 *
 * This kernel computes one output row per threadgroup:
 * out[query_idx, head] = softmax(Q @ K^T * scale + causal_mask) @ V
 *
 * GQA: Multiple Q heads share the same K/V heads
 * (e.g., 32 Q heads / 8 KV heads = 4 Q heads per KV)
 * ======================================================================== */

/* Fused causal attention - one threadgroup per (query_pos, head) pair
 * Q: [seq, num_q_heads * head_dim]
 * K: [seq, num_kv_heads * head_dim]
 * V: [seq, num_kv_heads * head_dim]
 * out: [seq, num_q_heads * head_dim]
 * attn_mask: [seq] - 1 for valid tokens, 0 for padding (optional, can be null)
 */
kernel void causal_attention_fused(
    device const float *Q [[buffer(0)]],
    device const float *K [[buffer(1)]],
    device const float *V [[buffer(2)]],
    device float *out [[buffer(3)]],
    device const int *attn_mask [[buffer(4)]],  // Attention mask (1=valid, 0=padding)
    constant int &seq [[buffer(5)]],
    constant int &num_q_heads [[buffer(6)]],
    constant int &num_kv_heads [[buffer(7)]],
    constant int &head_dim [[buffer(8)]],
    constant float &scale [[buffer(9)]],
    constant int &use_mask [[buffer(10)]],  // Whether to apply attn_mask
    uint3 tg_pos [[threadgroup_position_in_grid]],   // (query_idx, head_idx, 0)
    uint3 tid_pos [[thread_position_in_threadgroup]],
    uint3 tg_size [[threads_per_threadgroup]]
) {
    // Shared memory for scores and reductions
    threadgroup float shared_scores[512];  // For attention scores (up to 512 seq len)
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];

    int query_idx = tg_pos.x;
    int head_idx = tg_pos.y;
    uint tid = tid_pos.x;
    uint threads = tg_size.x;

    if (query_idx >= seq || head_idx >= num_q_heads) return;

    // GQA: map Q head to KV head
    int heads_per_kv = num_q_heads / num_kv_heads;
    int kv_head_idx = head_idx / heads_per_kv;

    int q_dim = num_q_heads * head_dim;
    int kv_dim = num_kv_heads * head_dim;

    // Pointers to this head's Q, K, V
    device const float *q_row = Q + query_idx * q_dim + head_idx * head_dim;
    device const float *K_head = K + kv_head_idx * head_dim;
    device const float *V_head = V + kv_head_idx * head_dim;
    device float *out_row = out + query_idx * q_dim + head_idx * head_dim;

    // ========== Phase 1: Compute Q @ K^T with causal mask ==========
    // Each thread computes scores for a subset of key positions
    float local_max = -INFINITY;

    for (int key_idx = tid; key_idx < seq; key_idx += threads) {
        // Causal mask: only attend to positions <= query_idx
        // Attention mask: only attend to valid tokens (mask[key_idx] != 0)
        bool masked = (key_idx > query_idx);
        if (use_mask && attn_mask[key_idx] == 0) {
            masked = true;
        }

        if (masked) {
            shared_scores[key_idx] = -INFINITY;
        } else {
            // Dot product: Q[query_idx, head] · K[key_idx, kv_head]
            float dot = 0.0f;
            device const float *k_row = K_head + key_idx * kv_dim;
            for (int d = 0; d < head_dim; d++) {
                dot += q_row[d] * k_row[d];
            }
            float score = dot * scale;
            shared_scores[key_idx] = score;
            local_max = max(local_max, score);
        }
    }

    // Store local max for reduction
    shared_max[tid] = local_max;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 2: Find global max (parallel reduction) ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // ========== Phase 3: Compute exp(score - max) and sum ==========
    float local_sum = 0.0f;
    for (int key_idx = tid; key_idx < seq; key_idx += threads) {
        float score = shared_scores[key_idx];
        if (score > -1e30f) {  // Not masked
            float e = exp(score - max_val);
            shared_scores[key_idx] = e;
            local_sum += e;
        } else {
            shared_scores[key_idx] = 0.0f;
        }
    }

    shared_sum[tid] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 4: Find total sum (parallel reduction) ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float sum = shared_sum[0];
    float inv_sum = 1.0f / sum;

    // ========== Phase 5: Normalize scores ==========
    for (int key_idx = tid; key_idx < seq; key_idx += threads) {
        shared_scores[key_idx] *= inv_sum;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 6: Compute output = scores @ V ==========
    // Each thread computes a subset of output dimensions
    for (int d = tid; d < head_dim; d += threads) {
        float acc = 0.0f;
        for (int key_idx = 0; key_idx < seq; key_idx++) {
            float score = shared_scores[key_idx];
            if (score > 0.0f) {  // Skip zeros (masked positions)
                float v_val = V_head[key_idx * kv_dim + d];
                acc += score * v_val;
            }
        }
        out_row[d] = acc;
    }
}

/* ========================================================================
 * Half-Precision Batched Matrix Multiply for Attention
 *
 * Tiled implementation with f32 accumulation for numerical stability.
 * Works directly with half-precision data.
 * ======================================================================== */

constant uint TILE_SIZE = 16;

/* Batched matmul for Q @ K^T (transposes K)
 * Q: [batch, M, K] (half)
 * K: [batch, N, K] (half) - note: N is seq_k, accessed transposed
 * out: [batch, M, N] (half)
 * For attention: M=seq_q, N=seq_k, K=head_dim
 */
kernel void batched_matmul_half_qkt(
    device const half *Q [[buffer(0)]],
    device const half *K [[buffer(1)]],
    device half *out [[buffer(2)]],
    constant int &M [[buffer(3)]],      // seq_q
    constant int &N [[buffer(4)]],      // seq_k
    constant int &K_dim [[buffer(5)]],  // head_dim
    constant int &batch [[buffer(6)]],  // num_heads
    constant float &scale [[buffer(7)]],
    uint3 tid [[thread_position_in_threadgroup]],
    uint3 group_id [[threadgroup_position_in_grid]]
) {
    threadgroup half A_tile[TILE_SIZE][TILE_SIZE];
    threadgroup half B_tile[TILE_SIZE][TILE_SIZE];

    uint b = group_id.z;
    uint col = group_id.x * TILE_SIZE + tid.x;
    uint row = group_id.y * TILE_SIZE + tid.y;

    // Batch offsets
    uint q_batch_offset = b * M * K_dim;
    uint k_batch_offset = b * N * K_dim;
    uint out_batch_offset = b * M * N;

    float sum = 0.0f;
    uint numTiles = (K_dim + TILE_SIZE - 1) / TILE_SIZE;

    for (uint t = 0; t < numTiles; t++) {
        uint tiledK = t * TILE_SIZE + tid.x;
        // Load Q tile: Q[b, row, tiledK]
        if (row < (uint)M && tiledK < (uint)K_dim) {
            A_tile[tid.y][tid.x] = Q[q_batch_offset + row * K_dim + tiledK];
        } else {
            A_tile[tid.y][tid.x] = 0.0h;
        }

        uint tiledK_row = t * TILE_SIZE + tid.y;
        // Load K tile transposed: K[b, col, tiledK_row] -> access K^T
        if (col < (uint)N && tiledK_row < (uint)K_dim) {
            B_tile[tid.y][tid.x] = K[k_batch_offset + col * K_dim + tiledK_row];
        } else {
            B_tile[tid.y][tid.x] = 0.0h;
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        // Compute partial dot product with f32 accumulation
        for (uint k = 0; k < TILE_SIZE; k++) {
            sum += float(A_tile[tid.y][k]) * float(B_tile[k][tid.x]);
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // Write result with scale
    if (row < (uint)M && col < (uint)N) {
        out[out_batch_offset + row * N + col] = half(sum * scale);
    }
}

/* Batched matmul for scores @ V (no transpose)
 * scores: [batch, M, K] (half) - K is seq_k
 * V: [batch, K, N] (half)
 * out: [batch, M, N] (half)
 * For attention: M=seq_q, K=seq_k, N=head_dim
 */
kernel void batched_matmul_half_sv(
    device const half *scores [[buffer(0)]],
    device const half *V [[buffer(1)]],
    device half *out [[buffer(2)]],
    constant int &M [[buffer(3)]],      // seq_q
    constant int &K_dim [[buffer(4)]],  // seq_k
    constant int &N [[buffer(5)]],      // head_dim
    constant int &batch [[buffer(6)]],  // num_heads
    uint3 tid [[thread_position_in_threadgroup]],
    uint3 group_id [[threadgroup_position_in_grid]]
) {
    threadgroup half A_tile[TILE_SIZE][TILE_SIZE];
    threadgroup half B_tile[TILE_SIZE][TILE_SIZE];

    uint b = group_id.z;
    uint col = group_id.x * TILE_SIZE + tid.x;
    uint row = group_id.y * TILE_SIZE + tid.y;

    // Batch offsets
    uint scores_batch_offset = b * M * K_dim;
    uint v_batch_offset = b * K_dim * N;
    uint out_batch_offset = b * M * N;

    float sum = 0.0f;
    uint numTiles = (K_dim + TILE_SIZE - 1) / TILE_SIZE;

    for (uint t = 0; t < numTiles; t++) {
        uint tiledK = t * TILE_SIZE + tid.x;
        // Load scores tile: scores[b, row, tiledK]
        if (row < (uint)M && tiledK < (uint)K_dim) {
            A_tile[tid.y][tid.x] = scores[scores_batch_offset + row * K_dim + tiledK];
        } else {
            A_tile[tid.y][tid.x] = 0.0h;
        }

        uint tiledK_row = t * TILE_SIZE + tid.y;
        // Load V tile: V[b, tiledK_row, col]
        if (tiledK_row < (uint)K_dim && col < (uint)N) {
            B_tile[tid.y][tid.x] = V[v_batch_offset + tiledK_row * N + col];
        } else {
            B_tile[tid.y][tid.x] = 0.0h;
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        // Compute partial dot product with f32 accumulation
        for (uint k = 0; k < TILE_SIZE; k++) {
            sum += float(A_tile[tid.y][k]) * float(B_tile[k][tid.x]);
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // Write result
    if (row < (uint)M && col < (uint)N) {
        out[out_batch_offset + row * N + col] = half(sum);
    }
}

/* Softmax for half-precision attention scores
 * scores: [batch, M, N] (half)
 * Applies row-wise softmax in-place
 */
kernel void softmax_half(
    device half *scores [[buffer(0)]],
    constant int &total_rows [[buffer(1)]],
    constant int &N [[buffer(2)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    if (row >= (uint)total_rows) return;

    device half *row_data = scores + row * N;
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];

    // Find max (for numerical stability)
    float local_max = -INFINITY;
    for (int i = tid; i < N; i += threads) {
        local_max = max(local_max, float(row_data[i]));
    }
    shared_max[tid] = local_max;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Reduce to find global max
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // Compute exp(x - max) and sum
    float local_sum = 0.0f;
    for (int i = tid; i < N; i += threads) {
        float exp_val = exp(float(row_data[i]) - max_val);
        row_data[i] = half(exp_val);  // Store temporarily
        local_sum += exp_val;
    }
    shared_sum[tid] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Reduce sum
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float sum = shared_sum[0];
    float inv_sum = 1.0f / sum;

    // Normalize
    for (int i = tid; i < N; i += threads) {
        row_data[i] = half(float(row_data[i]) * inv_sum);
    }
}

/* ========================================================================
 * BFloat16 Native Kernels
 * These kernels accept bf16 inputs and produce bf16 outputs with f32
 * internal computation for numerical stability.
 * ======================================================================== */

/* Helper: bf16 <-> f32 conversion functions */
inline float bf16_to_f32(ushort bf16) {
    uint bits = uint(bf16) << 16;
    return as_type<float>(bits);
}

inline ushort f32_to_bf16(float f32) {
    uint bits = as_type<uint>(f32);
    // Round to nearest even
    uint lsb = (bits >> 16) & 1;
    uint rounding = 0x7FFF + lsb;
    bits += rounding;
    return ushort(bits >> 16);
}

/* RMSNorm for bf16: out = x * rsqrt(mean(x^2) + eps) * weight
 * x: [seq, hidden] (bf16), weight: [hidden] (bf16), out: [seq, hidden] (bf16)
 */
kernel void rms_norm_bf16(
    device const ushort *x [[buffer(0)]],
    device const ushort *weight [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &hidden [[buffer(3)]],
    constant float &eps [[buffer(4)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    threadgroup float shared_sum[256];

    device const ushort *x_row = x + row * hidden;
    device ushort *out_row = out + row * hidden;

    // Compute partial sum of squares in f32
    float local_sum = 0.0f;
    for (int i = tid; i < hidden; i += threads) {
        float val = bf16_to_f32(x_row[i]);
        local_sum += val * val;
    }
    shared_sum[tid] = local_sum;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Parallel reduction
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    float rms_inv = rsqrt(shared_sum[0] / float(hidden) + eps);

    // Apply normalization with weight, output bf16
    for (int i = tid; i < hidden; i += threads) {
        float val = bf16_to_f32(x_row[i]);
        float w = bf16_to_f32(weight[i]);
        out_row[i] = f32_to_bf16(val * rms_inv * w);
    }
}

/* QK RMSNorm for bf16 - processes Q and K heads in bf16 */
kernel void qk_rms_norm_bf16(
    device ushort *q [[buffer(0)]],
    device ushort *k [[buffer(1)]],
    device const ushort *q_weight [[buffer(2)]],
    device const ushort *k_weight [[buffer(3)]],
    constant int &heads [[buffer(4)]],
    constant int &head_dim [[buffer(5)]],
    constant float &eps [[buffer(6)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    uint hidden = heads * head_dim;
    uint offset = seq_idx * hidden + head_idx * head_dim;

    // RMSNorm for Q
    float sum_sq = 0.0f;
    for (int d = 0; d < head_dim; d++) {
        float val = bf16_to_f32(q[offset + d]);
        sum_sq += val * val;
    }
    float rms_inv = rsqrt(sum_sq / float(head_dim) + eps);
    for (int d = 0; d < head_dim; d++) {
        float val = bf16_to_f32(q[offset + d]);
        float w = bf16_to_f32(q_weight[d]);
        q[offset + d] = f32_to_bf16(val * rms_inv * w);
    }

    // RMSNorm for K
    sum_sq = 0.0f;
    for (int d = 0; d < head_dim; d++) {
        float val = bf16_to_f32(k[offset + d]);
        sum_sq += val * val;
    }
    rms_inv = rsqrt(sum_sq / float(head_dim) + eps);
    for (int d = 0; d < head_dim; d++) {
        float val = bf16_to_f32(k[offset + d]);
        float w = bf16_to_f32(k_weight[d]);
        k[offset + d] = f32_to_bf16(val * rms_inv * w);
    }
}

/* Per-head RMSNorm for bf16 - single tensor version for GQA support
 * x: [seq, heads * head_dim] (bf16, modified in-place)
 * weight: [head_dim] (bf16)
 * Dispatched with (seq, heads) threadgroups
 */
kernel void head_rms_norm_bf16(
    device ushort *x [[buffer(0)]],
    device const ushort *weight [[buffer(1)]],
    constant int &heads [[buffer(2)]],
    constant int &head_dim [[buffer(3)]],
    constant float &eps [[buffer(4)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    if (head_idx >= uint(heads)) return;

    uint hidden = heads * head_dim;
    uint offset = seq_idx * hidden + head_idx * head_dim;

    // Compute RMS
    float sum_sq = 0.0f;
    for (int d = 0; d < head_dim; d++) {
        float val = bf16_to_f32(x[offset + d]);
        sum_sq += val * val;
    }
    float rms_inv = rsqrt(sum_sq / float(head_dim) + eps);

    // Normalize and scale
    for (int d = 0; d < head_dim; d++) {
        float val = bf16_to_f32(x[offset + d]);
        float w = bf16_to_f32(weight[d]);
        x[offset + d] = f32_to_bf16(val * rms_inv * w);
    }
}

/* LayerNorm + AdaLN for bf16
 * out = (1 + scale) * norm(x) + shift
 */
kernel void adaln_norm_bf16(
    device const ushort *x [[buffer(0)]],
    device const ushort *shift [[buffer(1)]],
    device const ushort *scale [[buffer(2)]],
    device ushort *out [[buffer(3)]],
    constant int &hidden [[buffer(4)]],
    constant float &eps [[buffer(5)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    threadgroup float shared_sum[256];
    threadgroup float shared_sum_sq[256];

    device const ushort *x_row = x + row * hidden;
    device ushort *out_row = out + row * hidden;

    float local_sum = 0.0f;
    float local_sum_sq = 0.0f;
    for (int i = tid; i < hidden; i += threads) {
        float val = bf16_to_f32(x_row[i]);
        local_sum += val;
        local_sum_sq += val * val;
    }
    shared_sum[tid] = local_sum;
    shared_sum_sq[tid] = local_sum_sq;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
            shared_sum_sq[tid] += shared_sum_sq[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    float mean = shared_sum[0] / float(hidden);
    float var = shared_sum_sq[0] / float(hidden) - mean * mean;
    float std_inv = rsqrt(var + eps);

    for (int i = tid; i < hidden; i += threads) {
        float val = bf16_to_f32(x_row[i]);
        float s = bf16_to_f32(scale[i]);
        float sh = bf16_to_f32(shift[i]);
        float norm = (val - mean) * std_inv;
        out_row[i] = f32_to_bf16((1.0f + s) * norm + sh);
    }
}

/* SiLU for bf16: x * sigmoid(x) */
kernel void silu_bf16(
    device ushort *x [[buffer(0)]],
    constant int &n [[buffer(1)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float val = bf16_to_f32(x[gid]);
        x[gid] = f32_to_bf16(val / (1.0f + exp(-val)));
    }
}

/* SiLU with multiply for bf16: gate = silu(gate) * up */
kernel void silu_mul_bf16(
    device ushort *gate [[buffer(0)]],
    device const ushort *up [[buffer(1)]],
    constant int &n [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float g = bf16_to_f32(gate[gid]);
        float silu_g = g / (1.0f + exp(-g));
        float u = bf16_to_f32(up[gid]);
        gate[gid] = f32_to_bf16(silu_g * u);
    }
}

/* Gated add for bf16: out += gate * proj */
kernel void gated_add_bf16(
    device ushort *out [[buffer(0)]],
    device const ushort *gate [[buffer(1)]],
    device const ushort *proj [[buffer(2)]],
    constant int &seq [[buffer(3)]],
    constant int &hidden [[buffer(4)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint s = pos.x;
    uint h = pos.y;
    if (s < uint(seq) && h < uint(hidden)) {
        uint idx = s * hidden + h;
        float o = bf16_to_f32(out[idx]);
        float g = bf16_to_f32(gate[h]);
        float p = bf16_to_f32(proj[idx]);
        out[idx] = f32_to_bf16(o + g * p);
    }
}

/* Simple element-wise add for bf16: out = a + b */
kernel void add_bf16(
    device const ushort *a [[buffer(0)]],
    device const ushort *b [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &n [[buffer(3)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float va = bf16_to_f32(a[gid]);
        float vb = bf16_to_f32(b[gid]);
        out[gid] = f32_to_bf16(va + vb);
    }
}

/* RoPE for bf16: applies rotary position embeddings */
kernel void apply_rope_unified_bf16(
    device ushort *x [[buffer(0)]],
    device const float *txt_cos [[buffer(1)]],
    device const float *txt_sin [[buffer(2)]],
    device const float *img_cos [[buffer(3)]],
    device const float *img_sin [[buffer(4)]],
    constant int &seq [[buffer(5)]],
    constant int &img_offset [[buffer(6)]],
    constant int &heads [[buffer(7)]],
    constant int &head_dim [[buffer(8)]],
    constant int &axis_dim [[buffer(9)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint seq_idx = pos.x;
    uint head_idx = pos.y;

    if (seq_idx >= uint(seq) || head_idx >= uint(heads)) return;

    uint hidden = heads * head_dim;
    device ushort *vec = x + seq_idx * hidden + head_idx * head_dim;

    device const float *cos_row;
    device const float *sin_row;

    if (seq_idx < uint(img_offset)) {
        cos_row = txt_cos + seq_idx * head_dim;
        sin_row = txt_sin + seq_idx * head_dim;
    } else {
        uint img_idx = seq_idx - uint(img_offset);
        cos_row = img_cos + img_idx * head_dim;
        sin_row = img_sin + img_idx * head_dim;
    }

    for (int d = 0; d < head_dim; d += 2) {
        float c = cos_row[d];
        float s = sin_row[d];

        float x0 = bf16_to_f32(vec[d]);
        float x1 = bf16_to_f32(vec[d + 1]);

        vec[d] = f32_to_bf16(x0 * c - x1 * s);
        vec[d + 1] = f32_to_bf16(x1 * c + x0 * s);
    }
}

/* Batched matmul Q @ K^T for bf16 with f32 accumulation */
kernel void batched_matmul_bf16_qkt(
    device const ushort *Q [[buffer(0)]],
    device const ushort *K [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &M [[buffer(3)]],      // seq_q
    constant int &N [[buffer(4)]],      // seq_k
    constant int &K_dim [[buffer(5)]],  // head_dim
    constant int &batch [[buffer(6)]],  // num_heads
    constant float &scale [[buffer(7)]],
    uint3 tid [[thread_position_in_threadgroup]],
    uint3 group_id [[threadgroup_position_in_grid]]
) {
    threadgroup float A_tile[TILE_SIZE][TILE_SIZE];
    threadgroup float B_tile[TILE_SIZE][TILE_SIZE];

    uint b = group_id.z;
    uint col = group_id.x * TILE_SIZE + tid.x;
    uint row = group_id.y * TILE_SIZE + tid.y;

    uint q_batch_offset = b * M * K_dim;
    uint k_batch_offset = b * N * K_dim;
    uint out_batch_offset = b * M * N;

    float sum = 0.0f;
    uint numTiles = (K_dim + TILE_SIZE - 1) / TILE_SIZE;

    for (uint t = 0; t < numTiles; t++) {
        uint tiledK = t * TILE_SIZE + tid.x;
        if (row < (uint)M && tiledK < (uint)K_dim) {
            A_tile[tid.y][tid.x] = bf16_to_f32(Q[q_batch_offset + row * K_dim + tiledK]);
        } else {
            A_tile[tid.y][tid.x] = 0.0f;
        }

        uint tiledK_row = t * TILE_SIZE + tid.y;
        if (col < (uint)N && tiledK_row < (uint)K_dim) {
            B_tile[tid.y][tid.x] = bf16_to_f32(K[k_batch_offset + col * K_dim + tiledK_row]);
        } else {
            B_tile[tid.y][tid.x] = 0.0f;
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        for (uint k = 0; k < TILE_SIZE; k++) {
            sum += A_tile[tid.y][k] * B_tile[k][tid.x];
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (row < (uint)M && col < (uint)N) {
        out[out_batch_offset + row * N + col] = f32_to_bf16(sum * scale);
    }
}

/* Batched matmul scores @ V for bf16 with f32 accumulation */
kernel void batched_matmul_bf16_sv(
    device const ushort *scores [[buffer(0)]],
    device const ushort *V [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &M [[buffer(3)]],      // seq_q
    constant int &K_dim [[buffer(4)]],  // seq_k
    constant int &N [[buffer(5)]],      // head_dim
    constant int &batch [[buffer(6)]],  // num_heads
    uint3 tid [[thread_position_in_threadgroup]],
    uint3 group_id [[threadgroup_position_in_grid]]
) {
    threadgroup float A_tile[TILE_SIZE][TILE_SIZE];
    threadgroup float B_tile[TILE_SIZE][TILE_SIZE];

    uint b = group_id.z;
    uint col = group_id.x * TILE_SIZE + tid.x;
    uint row = group_id.y * TILE_SIZE + tid.y;

    uint scores_batch_offset = b * M * K_dim;
    uint v_batch_offset = b * K_dim * N;
    uint out_batch_offset = b * M * N;

    float sum = 0.0f;
    uint numTiles = (K_dim + TILE_SIZE - 1) / TILE_SIZE;

    for (uint t = 0; t < numTiles; t++) {
        uint tiledK = t * TILE_SIZE + tid.x;
        if (row < (uint)M && tiledK < (uint)K_dim) {
            A_tile[tid.y][tid.x] = bf16_to_f32(scores[scores_batch_offset + row * K_dim + tiledK]);
        } else {
            A_tile[tid.y][tid.x] = 0.0f;
        }

        uint tiledK_row = t * TILE_SIZE + tid.y;
        if (tiledK_row < (uint)K_dim && col < (uint)N) {
            B_tile[tid.y][tid.x] = bf16_to_f32(V[v_batch_offset + tiledK_row * N + col]);
        } else {
            B_tile[tid.y][tid.x] = 0.0f;
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        for (uint k = 0; k < TILE_SIZE; k++) {
            sum += A_tile[tid.y][k] * B_tile[k][tid.x];
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (row < (uint)M && col < (uint)N) {
        out[out_batch_offset + row * N + col] = f32_to_bf16(sum);
    }
}

/* Softmax for bf16 attention scores (f32 internal computation) */
kernel void softmax_bf16(
    device ushort *scores [[buffer(0)]],
    constant int &total_rows [[buffer(1)]],
    constant int &N [[buffer(2)]],
    uint row [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    if (row >= (uint)total_rows) return;

    device ushort *row_data = scores + row * N;
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];

    // Find max in f32
    float local_max = -INFINITY;
    for (int i = tid; i < N; i += threads) {
        local_max = max(local_max, bf16_to_f32(row_data[i]));
    }
    shared_max[tid] = local_max;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // Compute exp and sum in f32
    float local_sum = 0.0f;
    for (int i = tid; i < N; i += threads) {
        float exp_val = exp(bf16_to_f32(row_data[i]) - max_val);
        row_data[i] = f32_to_bf16(exp_val);
        local_sum += exp_val;
    }
    shared_sum[tid] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float inv_sum = 1.0f / shared_sum[0];

    // Normalize and store as bf16
    for (int i = tid; i < N; i += threads) {
        row_data[i] = f32_to_bf16(bf16_to_f32(row_data[i]) * inv_sum);
    }
}

/* Convert f32 tensor to bf16 */
kernel void f32_to_bf16_convert(
    device const float *input [[buffer(0)]],
    device ushort *output [[buffer(1)]],
    constant int &n [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        output[gid] = f32_to_bf16(input[gid]);
    }
}

/* Convert bf16 tensor to f32 */
kernel void bf16_to_f32_convert(
    device const ushort *input [[buffer(0)]],
    device float *output [[buffer(1)]],
    constant int &n [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        output[gid] = bf16_to_f32(input[gid]);
    }
}

/* Batched bf16 linear: out = x @ W^T where W is bf16 weights
 * x: [batch, in_features] (bf16)
 * W: [out_features, in_features] (bf16)
 * out: [batch, out_features] (bf16)
 * Uses f32 accumulation for numerical stability
 */
kernel void linear_bf16(
    device const ushort *x [[buffer(0)]],
    device const ushort *W [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &batch [[buffer(3)]],
    constant int &in_features [[buffer(4)]],
    constant int &out_features [[buffer(5)]],
    uint3 tid [[thread_position_in_threadgroup]],
    uint3 group_id [[threadgroup_position_in_grid]]
) {
    threadgroup float A_tile[TILE_SIZE][TILE_SIZE];
    threadgroup float B_tile[TILE_SIZE][TILE_SIZE];

    uint row = group_id.y * TILE_SIZE + tid.y;  // batch index
    uint col = group_id.x * TILE_SIZE + tid.x;  // output feature

    float sum = 0.0f;
    uint numTiles = (in_features + TILE_SIZE - 1) / TILE_SIZE;

    for (uint t = 0; t < numTiles; t++) {
        uint tiledK = t * TILE_SIZE + tid.x;
        // Load x[row, tiledK]
        if (row < (uint)batch && tiledK < (uint)in_features) {
            A_tile[tid.y][tid.x] = bf16_to_f32(x[row * in_features + tiledK]);
        } else {
            A_tile[tid.y][tid.x] = 0.0f;
        }

        uint tiledK_row = t * TILE_SIZE + tid.y;
        // Load W[col, tiledK_row] (transposed access)
        if (col < (uint)out_features && tiledK_row < (uint)in_features) {
            B_tile[tid.y][tid.x] = bf16_to_f32(W[col * in_features + tiledK_row]);
        } else {
            B_tile[tid.y][tid.x] = 0.0f;
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        for (uint k = 0; k < TILE_SIZE; k++) {
            sum += A_tile[tid.y][k] * B_tile[k][tid.x];
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (row < (uint)batch && col < (uint)out_features) {
        out[row * out_features + col] = f32_to_bf16(sum);
    }
}

/* Split fused QKV+MLP output into separate tensors (bf16 version)
 * fused: [seq, hidden*3 + mlp_hidden*2] (bf16)
 * q, k, v: [seq, hidden] (bf16)
 * gate, up: [seq, mlp_hidden] (bf16)
 */
kernel void split_qkv_mlp_bf16(
    device const ushort *fused [[buffer(0)]],
    device ushort *q [[buffer(1)]],
    device ushort *k [[buffer(2)]],
    device ushort *v [[buffer(3)]],
    device ushort *gate [[buffer(4)]],
    device ushort *up [[buffer(5)]],
    constant int &seq [[buffer(6)]],
    constant int &hidden [[buffer(7)]],
    constant int &mlp_hidden [[buffer(8)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint s = pos.x;
    uint e = pos.y;

    if (s >= uint(seq)) return;

    int fused_dim = hidden * 3 + mlp_hidden * 2;
    device const ushort *row = fused + s * fused_dim;

    if (e < uint(hidden)) {
        q[s * hidden + e] = row[e];
        k[s * hidden + e] = row[hidden + e];
        v[s * hidden + e] = row[hidden * 2 + e];
    }

    if (e < uint(mlp_hidden)) {
        gate[s * mlp_hidden + e] = row[hidden * 3 + e];
        up[s * mlp_hidden + e] = row[hidden * 3 + mlp_hidden + e];
    }
}

/* Concat attention + MLP outputs (bf16 version)
 * attn: [seq, hidden] (bf16)
 * mlp: [seq, mlp_hidden] (bf16)
 * out: [seq, hidden + mlp_hidden] (bf16)
 */
kernel void concat_attn_mlp_bf16(
    device const ushort *attn [[buffer(0)]],
    device const ushort *mlp [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &seq [[buffer(3)]],
    constant int &hidden [[buffer(4)]],
    constant int &mlp_hidden [[buffer(5)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint s = pos.x;
    uint e = pos.y;

    if (s >= uint(seq)) return;

    int out_dim = hidden + mlp_hidden;
    device ushort *out_row = out + s * out_dim;

    if (e < uint(hidden)) {
        out_row[e] = attn[s * hidden + e];
    }

    if (e < uint(mlp_hidden)) {
        out_row[hidden + e] = mlp[s * mlp_hidden + e];
    }
}

/* Concatenate two bf16 sequences along seq dimension:
 * out = [a; b], where a: [seq_a, hidden], b: [seq_b, hidden]
 */
kernel void concat_seq_bf16(
    device const ushort *a [[buffer(0)]],
    device const ushort *b [[buffer(1)]],
    device ushort *out [[buffer(2)]],
    constant int &seq_a [[buffer(3)]],
    constant int &seq_b [[buffer(4)]],
    constant int &hidden [[buffer(5)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint s = pos.x;
    uint h = pos.y;
    uint total_seq = uint(seq_a + seq_b);

    if (s >= total_seq || h >= uint(hidden)) return;

    if (s < uint(seq_a)) {
        out[s * hidden + h] = a[s * hidden + h];
    } else {
        uint b_idx = s - uint(seq_a);
        out[s * hidden + h] = b[b_idx * hidden + h];
    }
}

/* Slice a bf16 sequence along seq dimension:
 * out[s, h] = in[(s + start), h], out: [seq_out, hidden]
 */
kernel void slice_seq_bf16(
    device const ushort *in [[buffer(0)]],
    device ushort *out [[buffer(1)]],
    constant int &seq_out [[buffer(2)]],
    constant int &hidden [[buffer(3)]],
    constant int &start [[buffer(4)]],
    uint2 pos [[thread_position_in_grid]]
) {
    uint s = pos.x;
    uint h = pos.y;

    if (s >= uint(seq_out) || h >= uint(hidden)) return;

    out[s * hidden + h] = in[(s + uint(start)) * hidden + h];
}

/* ========================================================================
 * BF16 Transpose kernels for attention
 * ======================================================================== */

/* Transpose for attention input: [seq, heads*head_dim] -> [heads, seq, head_dim]
 * in:  [seq, heads * head_dim] (bf16)
 * out: [heads, seq, head_dim] (bf16)
 */
kernel void transpose_to_heads_bf16(
    device const ushort *in [[buffer(0)]],
    device ushort *out [[buffer(1)]],
    constant int &seq [[buffer(2)]],
    constant int &heads [[buffer(3)]],
    constant int &head_dim [[buffer(4)]],
    uint3 pos [[thread_position_in_grid]]
) {
    uint h = pos.z;      // head index
    uint s = pos.y;      // sequence position
    uint d = pos.x;      // head_dim position

    if (h >= uint(heads) || s >= uint(seq) || d >= uint(head_dim)) return;

    // Input layout: [seq, heads * head_dim] - row s, column h*head_dim + d
    uint in_idx = s * (heads * head_dim) + h * head_dim + d;

    // Output layout: [heads, seq, head_dim] - head h, row s, column d
    uint out_idx = h * (seq * head_dim) + s * head_dim + d;

    out[out_idx] = in[in_idx];
}

/* Transpose for attention output: [heads, seq, head_dim] -> [seq, heads*head_dim]
 * in:  [heads, seq, head_dim] (bf16)
 * out: [seq, heads * head_dim] (bf16)
 */
kernel void transpose_from_heads_bf16(
    device const ushort *in [[buffer(0)]],
    device ushort *out [[buffer(1)]],
    constant int &seq [[buffer(2)]],
    constant int &heads [[buffer(3)]],
    constant int &head_dim [[buffer(4)]],
    uint3 pos [[thread_position_in_grid]]
) {
    uint h = pos.z;      // head index
    uint s = pos.y;      // sequence position
    uint d = pos.x;      // head_dim position

    if (h >= uint(heads) || s >= uint(seq) || d >= uint(head_dim)) return;

    // Input layout: [heads, seq, head_dim] - head h, row s, column d
    uint in_idx = h * (seq * head_dim) + s * head_dim + d;

    // Output layout: [seq, heads * head_dim] - row s, column h*head_dim + d
    uint out_idx = s * (heads * head_dim) + h * head_dim + d;

    out[out_idx] = in[in_idx];
}

/* ========================================================================
 * Fused Non-Causal Attention for BF16 Pipeline
 * Same algorithm as attention_fused but with bf16 I/O and f32 computation.
 *
 * This kernel computes one output row per threadgroup:
 * out[query_idx, head] = softmax(Q @ K^T * scale) @ V
 *
 * Works directly on [seq, heads*head_dim] layout without transpose.
 * bf16 input/output, f32 accumulation for numerical stability.
 * ======================================================================== */

kernel void attention_fused_bf16(
    device const ushort *Q [[buffer(0)]],      // [seq_q, heads * head_dim] bf16
    device const ushort *K [[buffer(1)]],      // [seq_k, heads * head_dim] bf16
    device const ushort *V [[buffer(2)]],      // [seq_k, heads * head_dim] bf16
    device ushort *out [[buffer(3)]],          // [seq_q, heads * head_dim] bf16
    constant int &seq_q [[buffer(4)]],         // Query sequence length
    constant int &seq_k [[buffer(5)]],         // Key/Value sequence length
    constant int &num_heads [[buffer(6)]],
    constant int &head_dim [[buffer(7)]],
    constant float &scale [[buffer(8)]],
    threadgroup float *shared_scores [[threadgroup(0)]],  // [seq_k] dynamic
    uint3 tg_pos [[threadgroup_position_in_grid]],   // (query_idx, head_idx, 0)
    uint3 tid_pos [[thread_position_in_threadgroup]],
    uint3 tg_size [[threads_per_threadgroup]]
) {
    // Shared memory for reductions (shared_scores is dynamic via threadgroup(0))
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];

    int query_idx = tg_pos.x;
    int head_idx = tg_pos.y;
    uint tid = tid_pos.x;
    uint threads = tg_size.x;

    if (query_idx >= seq_q || head_idx >= num_heads) return;

    int hidden = num_heads * head_dim;

    // Pointers to this position's Q and output (layout: [seq, heads*head_dim])
    device const ushort *q_row = Q + query_idx * hidden + head_idx * head_dim;
    device ushort *out_row = out + query_idx * hidden + head_idx * head_dim;

    // K and V have same layout, head offset is same
    device const ushort *K_head = K + head_idx * head_dim;
    device const ushort *V_head = V + head_idx * head_dim;

    // Cache Q in threadgroup memory (converted to f32)
    threadgroup float shared_q[128];  // Max head_dim = 128
    for (int d = tid; d < head_dim; d += threads) {
        shared_q[d] = bf16_to_f32(q_row[d]);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 1: Compute Q @ K^T ==========
    float local_max = -INFINITY;

    for (int key_idx = tid; key_idx < seq_k; key_idx += threads) {
        // Dot product: Q[query_idx, head] · K[key_idx, head]
        float dot = 0.0f;
        device const ushort *k_row = K_head + key_idx * hidden;
        for (int d = 0; d < head_dim; d++) {
            dot += shared_q[d] * bf16_to_f32(k_row[d]);
        }
        float score = dot * scale;
        shared_scores[key_idx] = score;
        local_max = max(local_max, score);
    }

    shared_max[tid] = local_max;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 2: Find global max ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // ========== Phase 3: Compute exp(score - max) and sum ==========
    float local_sum = 0.0f;
    for (int key_idx = tid; key_idx < seq_k; key_idx += threads) {
        float e = exp(shared_scores[key_idx] - max_val);
        shared_scores[key_idx] = e;
        local_sum += e;
    }

    shared_sum[tid] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 4: Find total sum ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float inv_sum = 1.0f / shared_sum[0];

    // ========== Phase 5: Normalize scores ==========
    for (int key_idx = tid; key_idx < seq_k; key_idx += threads) {
        shared_scores[key_idx] *= inv_sum;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 6: Compute output = scores @ V ==========
    for (int d = tid; d < head_dim; d += threads) {
        float acc = 0.0f;
        for (int key_idx = 0; key_idx < seq_k; key_idx++) {
            float v_val = bf16_to_f32(V_head[key_idx * hidden + d]);
            acc += shared_scores[key_idx] * v_val;
        }
        out_row[d] = f32_to_bf16(acc);
    }
}

/* ========================================================================
 * BF16 Causal Attention with GQA Support
 *
 * Fused kernel for causal (decoder) attention with bf16 I/O and f32 compute.
 * Supports Grouped Query Attention where num_q_heads > num_kv_heads.
 *
 * Q: [seq, num_q_heads * head_dim] (bf16)
 * K: [seq, num_kv_heads * head_dim] (bf16)
 * V: [seq, num_kv_heads * head_dim] (bf16)
 * out: [seq, num_q_heads * head_dim] (bf16)
 * attn_mask: [seq] - 1 for valid tokens, 0 for padding (optional)
 *
 * Each threadgroup processes one (query_pos, head) pair.
 * ======================================================================== */

kernel void causal_attention_fused_bf16(
    device const ushort *Q [[buffer(0)]],
    device const ushort *K [[buffer(1)]],
    device const ushort *V [[buffer(2)]],
    device ushort *out [[buffer(3)]],
    device const int *attn_mask [[buffer(4)]],
    constant int &seq [[buffer(5)]],
    constant int &num_q_heads [[buffer(6)]],
    constant int &num_kv_heads [[buffer(7)]],
    constant int &head_dim [[buffer(8)]],
    constant float &scale [[buffer(9)]],
    constant int &use_mask [[buffer(10)]],
    uint3 tg_pos [[threadgroup_position_in_grid]],
    uint3 tid_pos [[thread_position_in_threadgroup]],
    uint3 tg_size [[threads_per_threadgroup]]
) {
    // Shared memory for scores and reductions
    threadgroup float shared_scores[512];
    threadgroup float shared_max[256];
    threadgroup float shared_sum[256];
    threadgroup float shared_q[128];  // Cache Q for this query position

    int query_idx = tg_pos.x;
    int head_idx = tg_pos.y;
    uint tid = tid_pos.x;
    uint threads = tg_size.x;

    if (query_idx >= seq || head_idx >= num_q_heads) return;

    // GQA: map Q head to KV head
    int heads_per_kv = num_q_heads / num_kv_heads;
    int kv_head_idx = head_idx / heads_per_kv;

    int q_dim = num_q_heads * head_dim;
    int kv_dim = num_kv_heads * head_dim;

    // Pointers to this head's Q, K, V (bf16)
    device const ushort *q_row = Q + query_idx * q_dim + head_idx * head_dim;
    device const ushort *K_head = K + kv_head_idx * head_dim;
    device const ushort *V_head = V + kv_head_idx * head_dim;
    device ushort *out_row = out + query_idx * q_dim + head_idx * head_dim;

    // Load Q into shared memory (convert bf16 -> f32)
    for (int d = tid; d < head_dim; d += threads) {
        shared_q[d] = bf16_to_f32(q_row[d]);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 1: Compute Q @ K^T with causal mask ==========
    float local_max = -INFINITY;

    for (int key_idx = tid; key_idx < seq; key_idx += threads) {
        // Causal mask: only attend to positions <= query_idx
        // Attention mask: only attend to valid tokens
        bool masked = (key_idx > query_idx);
        if (use_mask && attn_mask[key_idx] == 0) {
            masked = true;
        }

        if (masked) {
            shared_scores[key_idx] = -INFINITY;
        } else {
            // Dot product: Q[query_idx, head] · K[key_idx, kv_head]
            float dot = 0.0f;
            device const ushort *k_row = K_head + key_idx * kv_dim;
            for (int d = 0; d < head_dim; d++) {
                dot += shared_q[d] * bf16_to_f32(k_row[d]);
            }
            float score = dot * scale;
            shared_scores[key_idx] = score;
            local_max = max(local_max, score);
        }
    }

    shared_max[tid] = local_max;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 2: Find global max (parallel reduction) ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_max[tid] = max(shared_max[tid], shared_max[tid + stride]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float max_val = shared_max[0];

    // ========== Phase 3: Compute exp(score - max) and sum ==========
    float local_sum = 0.0f;
    for (int key_idx = tid; key_idx < seq; key_idx += threads) {
        float score = shared_scores[key_idx];
        if (score > -1e30f) {
            float e = exp(score - max_val);
            shared_scores[key_idx] = e;
            local_sum += e;
        } else {
            shared_scores[key_idx] = 0.0f;
        }
    }

    shared_sum[tid] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 4: Find total sum (parallel reduction) ==========
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride && tid + stride < threads) {
            shared_sum[tid] += shared_sum[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float inv_sum = 1.0f / shared_sum[0];

    // ========== Phase 5: Normalize scores ==========
    for (int key_idx = tid; key_idx < seq; key_idx += threads) {
        shared_scores[key_idx] *= inv_sum;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // ========== Phase 6: Compute output = scores @ V ==========
    for (int d = tid; d < head_dim; d += threads) {
        float acc = 0.0f;
        for (int key_idx = 0; key_idx < seq; key_idx++) {
            float score = shared_scores[key_idx];
            if (score > 0.0f) {
                float v_val = bf16_to_f32(V_head[key_idx * kv_dim + d]);
                acc += score * v_val;
            }
        }
        out_row[d] = f32_to_bf16(acc);
    }
}

/* ========================================================================
 * BF16 RoPE for Text-Only (Qwen3 style)
 *
 * Applies rotary position embeddings to Q and K tensors.
 * Unlike the unified RoPE, this is for decoder-only text encoding.
 *
 * Q: [seq, num_q_heads * head_dim] (bf16) - modified in-place
 * K: [seq, num_kv_heads * head_dim] (bf16) - modified in-place
 * cos: [seq, head_dim/2] (f32) - precomputed cos(pos * freq)
 * sin: [seq, head_dim/2] (f32) - precomputed sin(pos * freq)
 *
 * RoPE formula: for each pair (x0, x1):
 *   x0' = x0 * cos - x1 * sin
 *   x1' = x0 * sin + x1 * cos
 * ======================================================================== */

kernel void rope_bf16(
    device ushort *Q [[buffer(0)]],
    device ushort *K [[buffer(1)]],
    device const float *cos_cache [[buffer(2)]],
    device const float *sin_cache [[buffer(3)]],
    constant int &seq [[buffer(4)]],
    constant int &num_q_heads [[buffer(5)]],
    constant int &num_kv_heads [[buffer(6)]],
    constant int &head_dim [[buffer(7)]],
    uint2 gid [[thread_position_in_grid]]  // (pos, head)
) {
    int pos = gid.x;
    int head = gid.y;

    if (pos >= seq) return;

    int half_dim = head_dim / 2;

    // Apply RoPE to Q heads
    if (head < num_q_heads) {
        int q_dim = num_q_heads * head_dim;
        device ushort *q_head = Q + pos * q_dim + head * head_dim;

        for (int i = 0; i < half_dim; i++) {
            float x0 = bf16_to_f32(q_head[i]);
            float x1 = bf16_to_f32(q_head[i + half_dim]);
            float c = cos_cache[pos * half_dim + i];
            float s = sin_cache[pos * half_dim + i];

            q_head[i] = f32_to_bf16(x0 * c - x1 * s);
            q_head[i + half_dim] = f32_to_bf16(x0 * s + x1 * c);
        }
    }

    // Apply RoPE to K heads (fewer heads due to GQA)
    if (head < num_kv_heads) {
        int kv_dim = num_kv_heads * head_dim;
        device ushort *k_head = K + pos * kv_dim + head * head_dim;

        for (int i = 0; i < half_dim; i++) {
            float x0 = bf16_to_f32(k_head[i]);
            float x1 = bf16_to_f32(k_head[i + half_dim]);
            float c = cos_cache[pos * half_dim + i];
            float s = sin_cache[pos * half_dim + i];

            k_head[i] = f32_to_bf16(x0 * c - x1 * s);
            k_head[i + half_dim] = f32_to_bf16(x0 * s + x1 * c);
        }
    }
}

/* ========================================================================
 * F32 VAE Shaders
 * ======================================================================== */

/* GroupNorm f32: one threadgroup per (batch, group) pair.
 * Layout: [batch, channels, H, W] (NCHW)
 * Each group has channels_per_group consecutive channels.
 * Computes mean and variance over all spatial positions × channels in the group. */
kernel void group_norm_f32(
    device const float *x [[buffer(0)]],
    device const float *gamma [[buffer(1)]],
    device const float *beta [[buffer(2)]],
    device float *out [[buffer(3)]],
    constant int &channels [[buffer(4)]],
    constant int &spatial [[buffer(5)]],       /* H * W */
    constant int &channels_per_group [[buffer(6)]],
    constant float &eps [[buffer(7)]],
    uint group_id [[threadgroup_position_in_grid]],
    uint tid [[thread_position_in_threadgroup]],
    uint threads [[threads_per_threadgroup]]
) {
    threadgroup float shared_sum[256];
    threadgroup float shared_sum2[256];

    /* group_id encodes (batch, group) */
    int num_groups = channels / channels_per_group;
    int batch_idx = group_id / num_groups;
    int group_idx = group_id % num_groups;

    int c_start = group_idx * channels_per_group;
    int group_size = channels_per_group * spatial;

    /* Base offset for this batch element */
    device const float *x_batch = x + batch_idx * channels * spatial;
    device float *out_batch = out + batch_idx * channels * spatial;

    /* Compute partial sums for mean and variance */
    float local_sum = 0.0f;
    float local_sum2 = 0.0f;
    for (int i = tid; i < group_size; i += threads) {
        int c = c_start + i / spatial;
        int s = i % spatial;
        float val = x_batch[c * spatial + s];
        local_sum += val;
        local_sum2 += val * val;
    }
    shared_sum[tid] = local_sum;
    shared_sum2[tid] = local_sum2;

    threadgroup_barrier(mem_flags::mem_threadgroup);

    /* Parallel reduction */
    for (uint stride = threads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            shared_sum[tid] += shared_sum[tid + stride];
            shared_sum2[tid] += shared_sum2[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    float mean = shared_sum[0] / float(group_size);
    float var = shared_sum2[0] / float(group_size) - mean * mean;
    float inv_std = rsqrt(var + eps);

    /* Apply normalization with gamma/beta per channel */
    for (int i = tid; i < group_size; i += threads) {
        int c = c_start + i / spatial;
        int s = i % spatial;
        int idx = c * spatial + s;
        float val = (x_batch[idx] - mean) * inv_std;
        out_batch[idx] = gamma[c] * val + beta[c];
    }
}

/* Swish f32: out = x * sigmoid(x), in-place safe (out can alias x) */
kernel void swish_f32(
    device const float *x [[buffer(0)]],
    device float *out [[buffer(1)]],
    constant int &n [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float v = x[gid];
        out[gid] = v / (1.0f + exp(-v));
    }
}

/* Add f32: out = a + b, in-place safe (out can alias a or b) */
kernel void add_f32(
    device const float *a [[buffer(0)]],
    device const float *b [[buffer(1)]],
    device float *out [[buffer(2)]],
    constant int &n [[buffer(3)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        out[gid] = a[gid] + b[gid];
    }
}

/* Upsample nearest 2x f32: [B, C, H, W] -> [B, C, 2H, 2W]
 * Each thread writes one output element. */
kernel void upsample_nearest_2x_f32(
    device const float *x [[buffer(0)]],
    device float *out [[buffer(1)]],
    constant int &channels [[buffer(2)]],
    constant int &in_h [[buffer(3)]],
    constant int &in_w [[buffer(4)]],
    uint gid [[thread_position_in_grid]]
) {
    int out_h = in_h * 2;
    int out_w = in_w * 2;
    int out_spatial = out_h * out_w;
    int total = channels * out_spatial;  /* batch=1 assumed */
    if (gid >= uint(total)) return;

    int c = gid / out_spatial;
    int rem = gid % out_spatial;
    int oy = rem / out_w;
    int ox = rem % out_w;

    int iy = oy / 2;
    int ix = ox / 2;

    out[c * out_spatial + oy * out_w + ox] = x[c * in_h * in_w + iy * in_w + ix];
}

/* LeakyReLU f32: out = x >= 0 ? x : slope*x, in-place safe (out can alias x) */
kernel void leaky_relu_f32(
    device const float *x [[buffer(0)]],
    device float *out [[buffer(1)]],
    constant int &n [[buffer(2)]],
    constant float &slope [[buffer(3)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        float v = x[gid];
        out[gid] = v >= 0.0f ? v : slope * v;
    }
}

/* Scaled residual add f32: out = scale*a + b, in-place safe (out can alias a or b) */
kernel void scale_add_f32(
    device const float *a [[buffer(0)]],
    device const float *b [[buffer(1)]],
    device float *out [[buffer(2)]],
    constant int &n [[buffer(3)]],
    constant float &scale [[buffer(4)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < uint(n)) {
        out[gid] = scale * a[gid] + b[gid];
    }
}
