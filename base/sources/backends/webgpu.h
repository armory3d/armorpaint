
// Based on
// https://github.com/webgpu-native/webgpu-headers/blob/main/webgpu.h
// Copyright 2019-2023 WebGPU-Native developers
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#define WGPU_NULLABLE

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WGPUStringView {
	WGPU_NULLABLE char const *data;
	size_t                    length;
} WGPUStringView;

typedef uint64_t WGPUFlags;
typedef uint32_t WGPUBool;

typedef struct WGPUAdapterImpl           *WGPUAdapter;
typedef struct WGPUBindGroupImpl         *WGPUBindGroup;
typedef struct WGPUBindGroupLayoutImpl   *WGPUBindGroupLayout;
typedef struct WGPUBufferImpl            *WGPUBuffer;
typedef struct WGPUCommandBufferImpl     *WGPUCommandBuffer;
typedef struct WGPUCommandEncoderImpl    *WGPUCommandEncoder;
typedef struct WGPUDeviceImpl            *WGPUDevice;
typedef struct WGPUInstanceImpl          *WGPUInstance;
typedef struct WGPUPipelineLayoutImpl    *WGPUPipelineLayout;
typedef struct WGPUQuerySetImpl          *WGPUQuerySet;
typedef struct WGPUQueueImpl             *WGPUQueue;
typedef struct WGPURenderPassEncoderImpl *WGPURenderPassEncoder;
typedef struct WGPURenderPipelineImpl    *WGPURenderPipeline;
typedef struct WGPUSamplerImpl           *WGPUSampler;
typedef struct WGPUShaderModuleImpl      *WGPUShaderModule;
typedef struct WGPUSurfaceImpl           *WGPUSurface;
typedef struct WGPUTextureImpl           *WGPUTexture;
typedef struct WGPUTextureViewImpl       *WGPUTextureView;

struct WGPUAdapterInfo;
struct WGPUBindGroupEntry;
struct WGPUBlendComponent;
struct WGPUBufferBindingLayout;
struct WGPUBufferDescriptor;
struct WGPUColor;
struct WGPUCommandBufferDescriptor;
struct WGPUCommandEncoderDescriptor;
struct WGPUConstantEntry;
struct WGPUExtent3D;
struct WGPUFuture;
struct WGPUInstanceLimits;
struct WGPULimits;
struct WGPUMultisampleState;
struct WGPUOrigin3D;
struct WGPUPassTimestampWrites;
struct WGPUPipelineLayoutDescriptor;
struct WGPUPrimitiveState;
struct WGPUQueueDescriptor;
struct WGPURenderPassDepthStencilAttachment;
struct WGPUSamplerBindingLayout;
struct WGPUSamplerDescriptor;
struct WGPUShaderSourceWGSL;
struct WGPUStencilFaceState;
struct WGPUStorageTextureBindingLayout;
struct WGPUSurfaceCapabilities;
struct WGPUSurfaceTexture;
struct WGPUTexelCopyBufferLayout;
struct WGPUTextureBindingLayout;
struct WGPUTextureViewDescriptor;
struct WGPUVertexAttribute;
struct WGPUBindGroupDescriptor;
struct WGPUBindGroupLayoutEntry;
struct WGPUBlendState;
struct WGPUDepthStencilState;
struct WGPUDeviceDescriptor;
struct WGPUInstanceDescriptor;
struct WGPURenderPassColorAttachment;
struct WGPURequestAdapterOptions;
struct WGPUShaderModuleDescriptor;
struct WGPUTexelCopyBufferInfo;
struct WGPUTexelCopyTextureInfo;
struct WGPUTextureDescriptor;
struct WGPUVertexBufferLayout;
struct WGPUBindGroupLayoutDescriptor;
struct WGPUColorTargetState;
struct WGPURenderPassDescriptor;
struct WGPUVertexState;
struct WGPUFragmentState;
struct WGPURenderPipelineDescriptor;

struct WGPUDeviceLostCallbackInfo;
struct WGPURequestAdapterCallbackInfo;
struct WGPURequestDeviceCallbackInfo;
struct WGPUUncapturedErrorCallbackInfo;

typedef enum WGPUAdapterType {
	WGPUAdapterType_DiscreteGPU   = 0x00000001,
	WGPUAdapterType_IntegratedGPU = 0x00000002,
	WGPUAdapterType_CPU           = 0x00000003,
	WGPUAdapterType_Unknown       = 0x00000004,
	WGPUAdapterType_Force32       = 0x7FFFFFFF
} WGPUAdapterType;

typedef enum WGPUAddressMode {
	WGPUAddressMode_Undefined    = 0x00000000,
	WGPUAddressMode_ClampToEdge  = 0x00000001,
	WGPUAddressMode_Repeat       = 0x00000002,
	WGPUAddressMode_MirrorRepeat = 0x00000003,
	WGPUAddressMode_Force32      = 0x7FFFFFFF
} WGPUAddressMode;

typedef enum WGPUBackendType {
	WGPUBackendType_Undefined = 0x00000000,
	WGPUBackendType_Null      = 0x00000001,
	WGPUBackendType_WebGPU    = 0x00000002,
	WGPUBackendType_D3D11     = 0x00000003,
	WGPUBackendType_D3D12     = 0x00000004,
	WGPUBackendType_Metal     = 0x00000005,
	WGPUBackendType_Vulkan    = 0x00000006,
	WGPUBackendType_OpenGL    = 0x00000007,
	WGPUBackendType_OpenGLES  = 0x00000008,
	WGPUBackendType_Force32   = 0x7FFFFFFF
} WGPUBackendType;

typedef enum WGPUBlendFactor {
	WGPUBlendFactor_Undefined         = 0x00000000,
	WGPUBlendFactor_Zero              = 0x00000001,
	WGPUBlendFactor_One               = 0x00000002,
	WGPUBlendFactor_Src               = 0x00000003,
	WGPUBlendFactor_OneMinusSrc       = 0x00000004,
	WGPUBlendFactor_SrcAlpha          = 0x00000005,
	WGPUBlendFactor_OneMinusSrcAlpha  = 0x00000006,
	WGPUBlendFactor_Dst               = 0x00000007,
	WGPUBlendFactor_OneMinusDst       = 0x00000008,
	WGPUBlendFactor_DstAlpha          = 0x00000009,
	WGPUBlendFactor_OneMinusDstAlpha  = 0x0000000A,
	WGPUBlendFactor_SrcAlphaSaturated = 0x0000000B,
	WGPUBlendFactor_Constant          = 0x0000000C,
	WGPUBlendFactor_OneMinusConstant  = 0x0000000D,
	WGPUBlendFactor_Src1              = 0x0000000E,
	WGPUBlendFactor_OneMinusSrc1      = 0x0000000F,
	WGPUBlendFactor_Src1Alpha         = 0x00000010,
	WGPUBlendFactor_OneMinusSrc1Alpha = 0x00000011,
	WGPUBlendFactor_Force32           = 0x7FFFFFFF
} WGPUBlendFactor;

typedef enum WGPUBlendOperation {
	WGPUBlendOperation_Undefined       = 0x00000000,
	WGPUBlendOperation_Add             = 0x00000001,
	WGPUBlendOperation_Subtract        = 0x00000002,
	WGPUBlendOperation_ReverseSubtract = 0x00000003,
	WGPUBlendOperation_Min             = 0x00000004,
	WGPUBlendOperation_Max             = 0x00000005,
	WGPUBlendOperation_Force32         = 0x7FFFFFFF
} WGPUBlendOperation;

typedef enum WGPUBufferBindingType {
	WGPUBufferBindingType_BindingNotUsed  = 0x00000000,
	WGPUBufferBindingType_Undefined       = 0x00000001,
	WGPUBufferBindingType_Uniform         = 0x00000002,
	WGPUBufferBindingType_Storage         = 0x00000003,
	WGPUBufferBindingType_ReadOnlyStorage = 0x00000004,
	WGPUBufferBindingType_Force32         = 0x7FFFFFFF
} WGPUBufferBindingType;

typedef enum WGPUCallbackMode {
	WGPUCallbackMode_WaitAnyOnly        = 0x00000001,
	WGPUCallbackMode_AllowProcessEvents = 0x00000002,
	WGPUCallbackMode_AllowSpontaneous   = 0x00000003,
	WGPUCallbackMode_Force32            = 0x7FFFFFFF
} WGPUCallbackMode;

typedef enum WGPUCompareFunction {
	WGPUCompareFunction_Undefined    = 0x00000000,
	WGPUCompareFunction_Never        = 0x00000001,
	WGPUCompareFunction_Less         = 0x00000002,
	WGPUCompareFunction_Equal        = 0x00000003,
	WGPUCompareFunction_LessEqual    = 0x00000004,
	WGPUCompareFunction_Greater      = 0x00000005,
	WGPUCompareFunction_NotEqual     = 0x00000006,
	WGPUCompareFunction_GreaterEqual = 0x00000007,
	WGPUCompareFunction_Always       = 0x00000008,
	WGPUCompareFunction_Force32      = 0x7FFFFFFF
} WGPUCompareFunction;

typedef enum WGPUCompositeAlphaMode {
	WGPUCompositeAlphaMode_Auto            = 0x00000000,
	WGPUCompositeAlphaMode_Opaque          = 0x00000001,
	WGPUCompositeAlphaMode_Premultiplied   = 0x00000002,
	WGPUCompositeAlphaMode_Unpremultiplied = 0x00000003,
	WGPUCompositeAlphaMode_Inherit         = 0x00000004,
	WGPUCompositeAlphaMode_Force32         = 0x7FFFFFFF
} WGPUCompositeAlphaMode;

typedef enum WGPUCullMode {
	WGPUCullMode_Undefined = 0x00000000,
	WGPUCullMode_None      = 0x00000001,
	WGPUCullMode_Front     = 0x00000002,
	WGPUCullMode_Back      = 0x00000003,
	WGPUCullMode_Force32   = 0x7FFFFFFF
} WGPUCullMode;

typedef enum WGPUDeviceLostReason {
	WGPUDeviceLostReason_Unknown           = 0x00000001,
	WGPUDeviceLostReason_Destroyed         = 0x00000002,
	WGPUDeviceLostReason_CallbackCancelled = 0x00000003,
	WGPUDeviceLostReason_FailedCreation    = 0x00000004,
	WGPUDeviceLostReason_Force32           = 0x7FFFFFFF
} WGPUDeviceLostReason;

typedef enum WGPUErrorType {
	WGPUErrorType_NoError     = 0x00000001,
	WGPUErrorType_Validation  = 0x00000002,
	WGPUErrorType_OutOfMemory = 0x00000003,
	WGPUErrorType_Internal    = 0x00000004,
	WGPUErrorType_Unknown     = 0x00000005,
	WGPUErrorType_Force32     = 0x7FFFFFFF
} WGPUErrorType;

typedef enum WGPUFeatureLevel {
	WGPUFeatureLevel_Undefined     = 0x00000000,
	WGPUFeatureLevel_Compatibility = 0x00000001,
	WGPUFeatureLevel_Core          = 0x00000002,
	WGPUFeatureLevel_Force32       = 0x7FFFFFFF
} WGPUFeatureLevel;

typedef enum WGPUFeatureName {
	WGPUFeatureName_CoreFeaturesAndLimits          = 0x00000001,
	WGPUFeatureName_DepthClipControl               = 0x00000002,
	WGPUFeatureName_Depth32FloatStencil8           = 0x00000003,
	WGPUFeatureName_TextureCompressionBC           = 0x00000004,
	WGPUFeatureName_TextureCompressionBCSliced3D   = 0x00000005,
	WGPUFeatureName_TextureCompressionETC2         = 0x00000006,
	WGPUFeatureName_TextureCompressionASTC         = 0x00000007,
	WGPUFeatureName_TextureCompressionASTCSliced3D = 0x00000008,
	WGPUFeatureName_TimestampQuery                 = 0x00000009,
	WGPUFeatureName_IndirectFirstInstance          = 0x0000000A,
	WGPUFeatureName_ShaderF16                      = 0x0000000B,
	WGPUFeatureName_RG11B10UfloatRenderable        = 0x0000000C,
	WGPUFeatureName_BGRA8UnormStorage              = 0x0000000D,
	WGPUFeatureName_Float32Filterable              = 0x0000000E,
	WGPUFeatureName_Float32Blendable               = 0x0000000F,
	WGPUFeatureName_ClipDistances                  = 0x00000010,
	WGPUFeatureName_DualSourceBlending             = 0x00000011,
	WGPUFeatureName_Subgroups                      = 0x00000012,
	WGPUFeatureName_TextureFormatsTier1            = 0x00000013,
	WGPUFeatureName_TextureFormatsTier2            = 0x00000014,
	WGPUFeatureName_PrimitiveIndex                 = 0x00000015,
	WGPUFeatureName_TextureComponentSwizzle        = 0x00000016,
	WGPUFeatureName_Force32                        = 0x7FFFFFFF
} WGPUFeatureName;

typedef enum WGPUFilterMode {
	WGPUFilterMode_Undefined = 0x00000000,
	WGPUFilterMode_Nearest   = 0x00000001,
	WGPUFilterMode_Linear    = 0x00000002,
	WGPUFilterMode_Force32   = 0x7FFFFFFF
} WGPUFilterMode;

typedef enum WGPUFrontFace {
	WGPUFrontFace_Undefined = 0x00000000,
	WGPUFrontFace_CCW       = 0x00000001,
	WGPUFrontFace_CW        = 0x00000002,
	WGPUFrontFace_Force32   = 0x7FFFFFFF
} WGPUFrontFace;

typedef enum WGPUIndexFormat {
	WGPUIndexFormat_Undefined = 0x00000000,
	WGPUIndexFormat_Uint16    = 0x00000001,
	WGPUIndexFormat_Uint32    = 0x00000002,
	WGPUIndexFormat_Force32   = 0x7FFFFFFF
} WGPUIndexFormat;

typedef enum WGPUInstanceFeatureName {
	WGPUInstanceFeatureName_TimedWaitAny              = 0x00000001,
	WGPUInstanceFeatureName_ShaderSourceSPIRV         = 0x00000002,
	WGPUInstanceFeatureName_MultipleDevicesPerAdapter = 0x00000003,
	WGPUInstanceFeatureName_Force32                   = 0x7FFFFFFF
} WGPUInstanceFeatureName;

typedef enum WGPULoadOp {
	WGPULoadOp_Undefined = 0x00000000,
	WGPULoadOp_Load      = 0x00000001,
	WGPULoadOp_Clear     = 0x00000002,
	WGPULoadOp_Force32   = 0x7FFFFFFF
} WGPULoadOp;

typedef enum WGPUMipmapFilterMode {
	WGPUMipmapFilterMode_Undefined = 0x00000000,
	WGPUMipmapFilterMode_Nearest   = 0x00000001,
	WGPUMipmapFilterMode_Linear    = 0x00000002,
	WGPUMipmapFilterMode_Force32   = 0x7FFFFFFF
} WGPUMipmapFilterMode;

typedef enum WGPUOptionalBool {
	WGPUOptionalBool_False     = 0x00000000,
	WGPUOptionalBool_True      = 0x00000001,
	WGPUOptionalBool_Undefined = 0x00000002,
	WGPUOptionalBool_Force32   = 0x7FFFFFFF
} WGPUOptionalBool;

typedef enum WGPUPowerPreference {
	WGPUPowerPreference_Undefined       = 0x00000000,
	WGPUPowerPreference_LowPower        = 0x00000001,
	WGPUPowerPreference_HighPerformance = 0x00000002,
	WGPUPowerPreference_Force32         = 0x7FFFFFFF
} WGPUPowerPreference;

typedef enum WGPUPresentMode {
	WGPUPresentMode_Undefined   = 0x00000000,
	WGPUPresentMode_Fifo        = 0x00000001,
	WGPUPresentMode_FifoRelaxed = 0x00000002,
	WGPUPresentMode_Immediate   = 0x00000003,
	WGPUPresentMode_Mailbox     = 0x00000004,
	WGPUPresentMode_Force32     = 0x7FFFFFFF
} WGPUPresentMode;

typedef enum WGPUPrimitiveTopology {
	WGPUPrimitiveTopology_Undefined     = 0x00000000,
	WGPUPrimitiveTopology_PointList     = 0x00000001,
	WGPUPrimitiveTopology_LineList      = 0x00000002,
	WGPUPrimitiveTopology_LineStrip     = 0x00000003,
	WGPUPrimitiveTopology_TriangleList  = 0x00000004,
	WGPUPrimitiveTopology_TriangleStrip = 0x00000005,
	WGPUPrimitiveTopology_Force32       = 0x7FFFFFFF
} WGPUPrimitiveTopology;

typedef enum WGPUQueueWorkDoneStatus {
	WGPUQueueWorkDoneStatus_Success           = 0x00000001,
	WGPUQueueWorkDoneStatus_CallbackCancelled = 0x00000002,
	WGPUQueueWorkDoneStatus_Error             = 0x00000003,
	WGPUQueueWorkDoneStatus_Force32           = 0x7FFFFFFF
} WGPUQueueWorkDoneStatus;

typedef enum WGPURequestAdapterStatus {
	WGPURequestAdapterStatus_Success           = 0x00000001,
	WGPURequestAdapterStatus_CallbackCancelled = 0x00000002,
	WGPURequestAdapterStatus_Unavailable       = 0x00000003,
	WGPURequestAdapterStatus_Error             = 0x00000004,
	WGPURequestAdapterStatus_Force32           = 0x7FFFFFFF
} WGPURequestAdapterStatus;

typedef enum WGPURequestDeviceStatus {
	WGPURequestDeviceStatus_Success           = 0x00000001,
	WGPURequestDeviceStatus_CallbackCancelled = 0x00000002,
	WGPURequestDeviceStatus_Error             = 0x00000003,
	WGPURequestDeviceStatus_Force32           = 0x7FFFFFFF
} WGPURequestDeviceStatus;

typedef enum WGPUSamplerBindingType {
	WGPUSamplerBindingType_BindingNotUsed = 0x00000000,
	WGPUSamplerBindingType_Undefined      = 0x00000001,
	WGPUSamplerBindingType_Filtering      = 0x00000002,
	WGPUSamplerBindingType_NonFiltering   = 0x00000003,
	WGPUSamplerBindingType_Comparison     = 0x00000004,
	WGPUSamplerBindingType_Force32        = 0x7FFFFFFF
} WGPUSamplerBindingType;

typedef enum WGPUStatus {
	WGPUStatus_Success = 0x00000001,
	WGPUStatus_Error   = 0x00000002,
	WGPUStatus_Force32 = 0x7FFFFFFF
} WGPUStatus;

typedef enum WGPUStencilOperation {
	WGPUStencilOperation_Undefined      = 0x00000000,
	WGPUStencilOperation_Keep           = 0x00000001,
	WGPUStencilOperation_Zero           = 0x00000002,
	WGPUStencilOperation_Replace        = 0x00000003,
	WGPUStencilOperation_Invert         = 0x00000004,
	WGPUStencilOperation_IncrementClamp = 0x00000005,
	WGPUStencilOperation_DecrementClamp = 0x00000006,
	WGPUStencilOperation_IncrementWrap  = 0x00000007,
	WGPUStencilOperation_DecrementWrap  = 0x00000008,
	WGPUStencilOperation_Force32        = 0x7FFFFFFF
} WGPUStencilOperation;

typedef enum WGPUStorageTextureAccess {
	WGPUStorageTextureAccess_BindingNotUsed = 0x00000000,
	WGPUStorageTextureAccess_Undefined      = 0x00000001,
	WGPUStorageTextureAccess_WriteOnly      = 0x00000002,
	WGPUStorageTextureAccess_ReadOnly       = 0x00000003,
	WGPUStorageTextureAccess_ReadWrite      = 0x00000004,
	WGPUStorageTextureAccess_Force32        = 0x7FFFFFFF
} WGPUStorageTextureAccess;

typedef enum WGPUStoreOp {
	WGPUStoreOp_Undefined = 0x00000000,
	WGPUStoreOp_Store     = 0x00000001,
	WGPUStoreOp_Discard   = 0x00000002,
	WGPUStoreOp_Force32   = 0x7FFFFFFF
} WGPUStoreOp;

typedef enum WGPUSType {
	WGPUSType_ShaderSourceSPIRV                = 0x00000001,
	WGPUSType_ShaderSourceWGSL                 = 0x00000002,
	WGPUSType_RenderPassMaxDrawCount           = 0x00000003,
	WGPUSType_SurfaceSourceMetalLayer          = 0x00000004,
	WGPUSType_SurfaceSourceWindowsHWND         = 0x00000005,
	WGPUSType_SurfaceSourceXlibWindow          = 0x00000006,
	WGPUSType_SurfaceSourceWaylandSurface      = 0x00000007,
	WGPUSType_SurfaceSourceAndroidNativeWindow = 0x00000008,
	WGPUSType_SurfaceSourceXCBWindow           = 0x00000009,
	WGPUSType_SurfaceColorManagement           = 0x0000000A,
	WGPUSType_RequestAdapterWebXROptions       = 0x0000000B,
	WGPUSType_Force32                          = 0x7FFFFFFF
} WGPUSType;

typedef enum WGPUSurfaceGetCurrentTextureStatus {
	WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal    = 0x00000001,
	WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal = 0x00000002,
	WGPUSurfaceGetCurrentTextureStatus_Timeout           = 0x00000003,
	WGPUSurfaceGetCurrentTextureStatus_Outdated          = 0x00000004,
	WGPUSurfaceGetCurrentTextureStatus_Lost              = 0x00000005,
	WGPUSurfaceGetCurrentTextureStatus_Error             = 0x00000006,
	WGPUSurfaceGetCurrentTextureStatus_Force32           = 0x7FFFFFFF
} WGPUSurfaceGetCurrentTextureStatus;

typedef enum WGPUTextureAspect {
	WGPUTextureAspect_Undefined   = 0x00000000,
	WGPUTextureAspect_All         = 0x00000001,
	WGPUTextureAspect_StencilOnly = 0x00000002,
	WGPUTextureAspect_DepthOnly   = 0x00000003,
	WGPUTextureAspect_Force32     = 0x7FFFFFFF
} WGPUTextureAspect;

typedef enum WGPUTextureDimension {
	WGPUTextureDimension_Undefined = 0x00000000,
	WGPUTextureDimension_1D        = 0x00000001,
	WGPUTextureDimension_2D        = 0x00000002,
	WGPUTextureDimension_3D        = 0x00000003,
	WGPUTextureDimension_Force32   = 0x7FFFFFFF
} WGPUTextureDimension;

typedef enum WGPUTextureFormat {
	WGPUTextureFormat_Undefined            = 0x00000000,
	WGPUTextureFormat_R8Unorm              = 0x00000001,
	WGPUTextureFormat_R8Snorm              = 0x00000002,
	WGPUTextureFormat_R8Uint               = 0x00000003,
	WGPUTextureFormat_R8Sint               = 0x00000004,
	WGPUTextureFormat_R16Unorm             = 0x00000005,
	WGPUTextureFormat_R16Snorm             = 0x00000006,
	WGPUTextureFormat_R16Uint              = 0x00000007,
	WGPUTextureFormat_R16Sint              = 0x00000008,
	WGPUTextureFormat_R16Float             = 0x00000009,
	WGPUTextureFormat_RG8Unorm             = 0x0000000A,
	WGPUTextureFormat_RG8Snorm             = 0x0000000B,
	WGPUTextureFormat_RG8Uint              = 0x0000000C,
	WGPUTextureFormat_RG8Sint              = 0x0000000D,
	WGPUTextureFormat_R32Float             = 0x0000000E,
	WGPUTextureFormat_R32Uint              = 0x0000000F,
	WGPUTextureFormat_R32Sint              = 0x00000010,
	WGPUTextureFormat_RG16Unorm            = 0x00000011,
	WGPUTextureFormat_RG16Snorm            = 0x00000012,
	WGPUTextureFormat_RG16Uint             = 0x00000013,
	WGPUTextureFormat_RG16Sint             = 0x00000014,
	WGPUTextureFormat_RG16Float            = 0x00000015,
	WGPUTextureFormat_RGBA8Unorm           = 0x00000016,
	WGPUTextureFormat_RGBA8UnormSrgb       = 0x00000017,
	WGPUTextureFormat_RGBA8Snorm           = 0x00000018,
	WGPUTextureFormat_RGBA8Uint            = 0x00000019,
	WGPUTextureFormat_RGBA8Sint            = 0x0000001A,
	WGPUTextureFormat_BGRA8Unorm           = 0x0000001B,
	WGPUTextureFormat_BGRA8UnormSrgb       = 0x0000001C,
	WGPUTextureFormat_RGB10A2Uint          = 0x0000001D,
	WGPUTextureFormat_RGB10A2Unorm         = 0x0000001E,
	WGPUTextureFormat_RG11B10Ufloat        = 0x0000001F,
	WGPUTextureFormat_RGB9E5Ufloat         = 0x00000020,
	WGPUTextureFormat_RG32Float            = 0x00000021,
	WGPUTextureFormat_RG32Uint             = 0x00000022,
	WGPUTextureFormat_RG32Sint             = 0x00000023,
	WGPUTextureFormat_RGBA16Unorm          = 0x00000024,
	WGPUTextureFormat_RGBA16Snorm          = 0x00000025,
	WGPUTextureFormat_RGBA16Uint           = 0x00000026,
	WGPUTextureFormat_RGBA16Sint           = 0x00000027,
	WGPUTextureFormat_RGBA16Float          = 0x00000028,
	WGPUTextureFormat_RGBA32Float          = 0x00000029,
	WGPUTextureFormat_RGBA32Uint           = 0x0000002A,
	WGPUTextureFormat_RGBA32Sint           = 0x0000002B,
	WGPUTextureFormat_Stencil8             = 0x0000002C,
	WGPUTextureFormat_Depth16Unorm         = 0x0000002D,
	WGPUTextureFormat_Depth24Plus          = 0x0000002E,
	WGPUTextureFormat_Depth24PlusStencil8  = 0x0000002F,
	WGPUTextureFormat_Depth32Float         = 0x00000030,
	WGPUTextureFormat_Depth32FloatStencil8 = 0x00000031,
	WGPUTextureFormat_BC1RGBAUnorm         = 0x00000032,
	WGPUTextureFormat_BC1RGBAUnormSrgb     = 0x00000033,
	WGPUTextureFormat_BC2RGBAUnorm         = 0x00000034,
	WGPUTextureFormat_BC2RGBAUnormSrgb     = 0x00000035,
	WGPUTextureFormat_BC3RGBAUnorm         = 0x00000036,
	WGPUTextureFormat_BC3RGBAUnormSrgb     = 0x00000037,
	WGPUTextureFormat_BC4RUnorm            = 0x00000038,
	WGPUTextureFormat_BC4RSnorm            = 0x00000039,
	WGPUTextureFormat_BC5RGUnorm           = 0x0000003A,
	WGPUTextureFormat_BC5RGSnorm           = 0x0000003B,
	WGPUTextureFormat_BC6HRGBUfloat        = 0x0000003C,
	WGPUTextureFormat_BC6HRGBFloat         = 0x0000003D,
	WGPUTextureFormat_BC7RGBAUnorm         = 0x0000003E,
	WGPUTextureFormat_BC7RGBAUnormSrgb     = 0x0000003F,
	WGPUTextureFormat_ETC2RGB8Unorm        = 0x00000040,
	WGPUTextureFormat_ETC2RGB8UnormSrgb    = 0x00000041,
	WGPUTextureFormat_ETC2RGB8A1Unorm      = 0x00000042,
	WGPUTextureFormat_ETC2RGB8A1UnormSrgb  = 0x00000043,
	WGPUTextureFormat_ETC2RGBA8Unorm       = 0x00000044,
	WGPUTextureFormat_ETC2RGBA8UnormSrgb   = 0x00000045,
	WGPUTextureFormat_EACR11Unorm          = 0x00000046,
	WGPUTextureFormat_EACR11Snorm          = 0x00000047,
	WGPUTextureFormat_EACRG11Unorm         = 0x00000048,
	WGPUTextureFormat_EACRG11Snorm         = 0x00000049,
	WGPUTextureFormat_ASTC4x4Unorm         = 0x0000004A,
	WGPUTextureFormat_ASTC4x4UnormSrgb     = 0x0000004B,
	WGPUTextureFormat_ASTC5x4Unorm         = 0x0000004C,
	WGPUTextureFormat_ASTC5x4UnormSrgb     = 0x0000004D,
	WGPUTextureFormat_ASTC5x5Unorm         = 0x0000004E,
	WGPUTextureFormat_ASTC5x5UnormSrgb     = 0x0000004F,
	WGPUTextureFormat_ASTC6x5Unorm         = 0x00000050,
	WGPUTextureFormat_ASTC6x5UnormSrgb     = 0x00000051,
	WGPUTextureFormat_ASTC6x6Unorm         = 0x00000052,
	WGPUTextureFormat_ASTC6x6UnormSrgb     = 0x00000053,
	WGPUTextureFormat_ASTC8x5Unorm         = 0x00000054,
	WGPUTextureFormat_ASTC8x5UnormSrgb     = 0x00000055,
	WGPUTextureFormat_ASTC8x6Unorm         = 0x00000056,
	WGPUTextureFormat_ASTC8x6UnormSrgb     = 0x00000057,
	WGPUTextureFormat_ASTC8x8Unorm         = 0x00000058,
	WGPUTextureFormat_ASTC8x8UnormSrgb     = 0x00000059,
	WGPUTextureFormat_ASTC10x5Unorm        = 0x0000005A,
	WGPUTextureFormat_ASTC10x5UnormSrgb    = 0x0000005B,
	WGPUTextureFormat_ASTC10x6Unorm        = 0x0000005C,
	WGPUTextureFormat_ASTC10x6UnormSrgb    = 0x0000005D,
	WGPUTextureFormat_ASTC10x8Unorm        = 0x0000005E,
	WGPUTextureFormat_ASTC10x8UnormSrgb    = 0x0000005F,
	WGPUTextureFormat_ASTC10x10Unorm       = 0x00000060,
	WGPUTextureFormat_ASTC10x10UnormSrgb   = 0x00000061,
	WGPUTextureFormat_ASTC12x10Unorm       = 0x00000062,
	WGPUTextureFormat_ASTC12x10UnormSrgb   = 0x00000063,
	WGPUTextureFormat_ASTC12x12Unorm       = 0x00000064,
	WGPUTextureFormat_ASTC12x12UnormSrgb   = 0x00000065,
	WGPUTextureFormat_Force32              = 0x7FFFFFFF
} WGPUTextureFormat;

typedef enum WGPUTextureSampleType {
	WGPUTextureSampleType_BindingNotUsed    = 0x00000000,
	WGPUTextureSampleType_Undefined         = 0x00000001,
	WGPUTextureSampleType_Float             = 0x00000002,
	WGPUTextureSampleType_UnfilterableFloat = 0x00000003,
	WGPUTextureSampleType_Depth             = 0x00000004,
	WGPUTextureSampleType_Sint              = 0x00000005,
	WGPUTextureSampleType_Uint              = 0x00000006,
	WGPUTextureSampleType_Force32           = 0x7FFFFFFF
} WGPUTextureSampleType;

typedef enum WGPUTextureViewDimension {
	WGPUTextureViewDimension_Undefined = 0x00000000,
	WGPUTextureViewDimension_1D        = 0x00000001,
	WGPUTextureViewDimension_2D        = 0x00000002,
	WGPUTextureViewDimension_2DArray   = 0x00000003,
	WGPUTextureViewDimension_Cube      = 0x00000004,
	WGPUTextureViewDimension_CubeArray = 0x00000005,
	WGPUTextureViewDimension_3D        = 0x00000006,
	WGPUTextureViewDimension_Force32   = 0x7FFFFFFF
} WGPUTextureViewDimension;

typedef enum WGPUVertexFormat {
	WGPUVertexFormat_Uint8           = 0x00000001,
	WGPUVertexFormat_Uint8x2         = 0x00000002,
	WGPUVertexFormat_Uint8x4         = 0x00000003,
	WGPUVertexFormat_Sint8           = 0x00000004,
	WGPUVertexFormat_Sint8x2         = 0x00000005,
	WGPUVertexFormat_Sint8x4         = 0x00000006,
	WGPUVertexFormat_Unorm8          = 0x00000007,
	WGPUVertexFormat_Unorm8x2        = 0x00000008,
	WGPUVertexFormat_Unorm8x4        = 0x00000009,
	WGPUVertexFormat_Snorm8          = 0x0000000A,
	WGPUVertexFormat_Snorm8x2        = 0x0000000B,
	WGPUVertexFormat_Snorm8x4        = 0x0000000C,
	WGPUVertexFormat_Uint16          = 0x0000000D,
	WGPUVertexFormat_Uint16x2        = 0x0000000E,
	WGPUVertexFormat_Uint16x4        = 0x0000000F,
	WGPUVertexFormat_Sint16          = 0x00000010,
	WGPUVertexFormat_Sint16x2        = 0x00000011,
	WGPUVertexFormat_Sint16x4        = 0x00000012,
	WGPUVertexFormat_Unorm16         = 0x00000013,
	WGPUVertexFormat_Unorm16x2       = 0x00000014,
	WGPUVertexFormat_Unorm16x4       = 0x00000015,
	WGPUVertexFormat_Snorm16         = 0x00000016,
	WGPUVertexFormat_Snorm16x2       = 0x00000017,
	WGPUVertexFormat_Snorm16x4       = 0x00000018,
	WGPUVertexFormat_Float16         = 0x00000019,
	WGPUVertexFormat_Float16x2       = 0x0000001A,
	WGPUVertexFormat_Float16x4       = 0x0000001B,
	WGPUVertexFormat_Float32         = 0x0000001C,
	WGPUVertexFormat_Float32x2       = 0x0000001D,
	WGPUVertexFormat_Float32x3       = 0x0000001E,
	WGPUVertexFormat_Float32x4       = 0x0000001F,
	WGPUVertexFormat_Uint32          = 0x00000020,
	WGPUVertexFormat_Uint32x2        = 0x00000021,
	WGPUVertexFormat_Uint32x3        = 0x00000022,
	WGPUVertexFormat_Uint32x4        = 0x00000023,
	WGPUVertexFormat_Sint32          = 0x00000024,
	WGPUVertexFormat_Sint32x2        = 0x00000025,
	WGPUVertexFormat_Sint32x3        = 0x00000026,
	WGPUVertexFormat_Sint32x4        = 0x00000027,
	WGPUVertexFormat_Unorm10_10_10_2 = 0x00000028,
	WGPUVertexFormat_Unorm8x4BGRA    = 0x00000029,
	WGPUVertexFormat_Force32         = 0x7FFFFFFF
} WGPUVertexFormat;

typedef enum WGPUVertexStepMode {
	WGPUVertexStepMode_Undefined = 0x00000000,
	WGPUVertexStepMode_Vertex    = 0x00000001,
	WGPUVertexStepMode_Instance  = 0x00000002,
	WGPUVertexStepMode_Force32   = 0x7FFFFFFF
} WGPUVertexStepMode;

typedef WGPUFlags            WGPUBufferUsage;
static const WGPUBufferUsage WGPUBufferUsage_None         = 0x0000000000000000;
static const WGPUBufferUsage WGPUBufferUsage_MapRead      = 0x0000000000000001;
static const WGPUBufferUsage WGPUBufferUsage_MapWrite     = 0x0000000000000002;
static const WGPUBufferUsage WGPUBufferUsage_CopySrc      = 0x0000000000000004;
static const WGPUBufferUsage WGPUBufferUsage_CopyDst      = 0x0000000000000008;
static const WGPUBufferUsage WGPUBufferUsage_Index        = 0x0000000000000010;
static const WGPUBufferUsage WGPUBufferUsage_Vertex       = 0x0000000000000020;
static const WGPUBufferUsage WGPUBufferUsage_Uniform      = 0x0000000000000040;
static const WGPUBufferUsage WGPUBufferUsage_Storage      = 0x0000000000000080;
static const WGPUBufferUsage WGPUBufferUsage_Indirect     = 0x0000000000000100;
static const WGPUBufferUsage WGPUBufferUsage_QueryResolve = 0x0000000000000200;

typedef WGPUFlags               WGPUColorWriteMask;
static const WGPUColorWriteMask WGPUColorWriteMask_None  = 0x0000000000000000;
static const WGPUColorWriteMask WGPUColorWriteMask_Red   = 0x0000000000000001;
static const WGPUColorWriteMask WGPUColorWriteMask_Green = 0x0000000000000002;
static const WGPUColorWriteMask WGPUColorWriteMask_Blue  = 0x0000000000000004;
static const WGPUColorWriteMask WGPUColorWriteMask_Alpha = 0x0000000000000008;
static const WGPUColorWriteMask WGPUColorWriteMask_All   = 0x000000000000000F;

typedef WGPUFlags        WGPUMapMode;
static const WGPUMapMode WGPUMapMode_None  = 0x0000000000000000;
static const WGPUMapMode WGPUMapMode_Read  = 0x0000000000000001;
static const WGPUMapMode WGPUMapMode_Write = 0x0000000000000002;

typedef WGPUFlags            WGPUShaderStage;
static const WGPUShaderStage WGPUShaderStage_None     = 0x0000000000000000;
static const WGPUShaderStage WGPUShaderStage_Vertex   = 0x0000000000000001;
static const WGPUShaderStage WGPUShaderStage_Fragment = 0x0000000000000002;
static const WGPUShaderStage WGPUShaderStage_Compute  = 0x0000000000000004;

typedef WGPUFlags             WGPUTextureUsage;
static const WGPUTextureUsage WGPUTextureUsage_None             = 0x0000000000000000;
static const WGPUTextureUsage WGPUTextureUsage_CopySrc          = 0x0000000000000001;
static const WGPUTextureUsage WGPUTextureUsage_CopyDst          = 0x0000000000000002;
static const WGPUTextureUsage WGPUTextureUsage_TextureBinding   = 0x0000000000000004;
static const WGPUTextureUsage WGPUTextureUsage_StorageBinding   = 0x0000000000000008;
static const WGPUTextureUsage WGPUTextureUsage_RenderAttachment = 0x0000000000000010;

typedef void (*WGPUDeviceLostCallback)(WGPUDevice const *device, WGPUDeviceLostReason reason, WGPUStringView message, WGPU_NULLABLE void *userdata1,
                                       WGPU_NULLABLE void *userdata2);
typedef void (*WGPURequestAdapterCallback)(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, WGPU_NULLABLE void *userdata1,
                                           WGPU_NULLABLE void *userdata2);
typedef void (*WGPURequestDeviceCallback)(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, WGPU_NULLABLE void *userdata1,
                                          WGPU_NULLABLE void *userdata2);
typedef void (*WGPUUncapturedErrorCallback)(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message, WGPU_NULLABLE void *userdata1,
                                            WGPU_NULLABLE void *userdata2);

typedef struct WGPUChainedStruct {
	struct WGPUChainedStruct *next;
	WGPUSType                 sType;
} WGPUChainedStruct;

typedef struct WGPUDeviceLostCallbackInfo {
	WGPUChainedStruct     *nextInChain;
	WGPUCallbackMode       mode;
	WGPUDeviceLostCallback callback;
	WGPU_NULLABLE void    *userdata1;
	WGPU_NULLABLE void    *userdata2;
} WGPUDeviceLostCallbackInfo;

typedef struct WGPURequestAdapterCallbackInfo {
	WGPUChainedStruct         *nextInChain;
	WGPUCallbackMode           mode;
	WGPURequestAdapterCallback callback;
	WGPU_NULLABLE void        *userdata1;
	WGPU_NULLABLE void        *userdata2;
} WGPURequestAdapterCallbackInfo;

typedef struct WGPURequestDeviceCallbackInfo {
	WGPUChainedStruct        *nextInChain;
	WGPUCallbackMode          mode;
	WGPURequestDeviceCallback callback;
	WGPU_NULLABLE void       *userdata1;
	WGPU_NULLABLE void       *userdata2;
} WGPURequestDeviceCallbackInfo;

typedef struct WGPUUncapturedErrorCallbackInfo {
	WGPUChainedStruct          *nextInChain;
	WGPUUncapturedErrorCallback callback;
	WGPU_NULLABLE void         *userdata1;
	WGPU_NULLABLE void         *userdata2;
} WGPUUncapturedErrorCallbackInfo;

typedef struct WGPUAdapterInfo {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     vendor;
	WGPUStringView     architecture;
	WGPUStringView     device;
	WGPUStringView     description;
	WGPUBackendType    backendType;
	WGPUAdapterType    adapterType;
	uint32_t           vendorID;
	uint32_t           deviceID;
	uint32_t           subgroupMinSize;
	uint32_t           subgroupMaxSize;
} WGPUAdapterInfo;

typedef struct WGPUBindGroupEntry {
	WGPUChainedStruct            *nextInChain;
	uint32_t                      binding;
	WGPU_NULLABLE WGPUBuffer      buffer;
	uint64_t                      offset;
	uint64_t                      size;
	WGPU_NULLABLE WGPUSampler     sampler;
	WGPU_NULLABLE WGPUTextureView textureView;
} WGPUBindGroupEntry;

typedef struct WGPUBlendComponent {
	WGPUBlendOperation operation;
	WGPUBlendFactor    srcFactor;
	WGPUBlendFactor    dstFactor;
} WGPUBlendComponent;

typedef struct WGPUBufferBindingLayout {
	WGPUChainedStruct    *nextInChain;
	WGPUBufferBindingType type;
	WGPUBool              hasDynamicOffset;
	uint64_t              minBindingSize;
} WGPUBufferBindingLayout;

typedef struct WGPUBufferDescriptor {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     label;
	WGPUBufferUsage    usage;
	uint64_t           size;
	WGPUBool           mappedAtCreation;
} WGPUBufferDescriptor;

typedef struct WGPUColor {
	double r;
	double g;
	double b;
	double a;
} WGPUColor;

typedef struct WGPUCommandBufferDescriptor {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     label;
} WGPUCommandBufferDescriptor;

typedef struct WGPUCommandEncoderDescriptor {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     label;
} WGPUCommandEncoderDescriptor;

typedef struct WGPUConstantEntry {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     key;
	double             value;
} WGPUConstantEntry;

typedef struct WGPUExtent3D {
	uint32_t width;
	uint32_t height;
	uint32_t depthOrArrayLayers;
} WGPUExtent3D;

typedef struct WGPUFuture {
	uint64_t id;
} WGPUFuture;

typedef struct WGPUInstanceLimits {
	WGPUChainedStruct *nextInChain;
	size_t             timedWaitAnyMaxCount;
} WGPUInstanceLimits;

typedef struct WGPULimits {
	WGPUChainedStruct *nextInChain;
	uint32_t           maxTextureDimension1D;
	uint32_t           maxTextureDimension2D;
	uint32_t           maxTextureDimension3D;
	uint32_t           maxTextureArrayLayers;
	uint32_t           maxBindGroups;
	uint32_t           maxBindGroupsPlusVertexBuffers;
	uint32_t           maxBindingsPerBindGroup;
	uint32_t           maxDynamicUniformBuffersPerPipelineLayout;
	uint32_t           maxDynamicStorageBuffersPerPipelineLayout;
	uint32_t           maxSampledTexturesPerShaderStage;
	uint32_t           maxSamplersPerShaderStage;
	uint32_t           maxStorageBuffersPerShaderStage;
	uint32_t           maxStorageTexturesPerShaderStage;
	uint32_t           maxUniformBuffersPerShaderStage;
	uint64_t           maxUniformBufferBindingSize;
	uint64_t           maxStorageBufferBindingSize;
	uint32_t           minUniformBufferOffsetAlignment;
	uint32_t           minStorageBufferOffsetAlignment;
	uint32_t           maxVertexBuffers;
	uint64_t           maxBufferSize;
	uint32_t           maxVertexAttributes;
	uint32_t           maxVertexBufferArrayStride;
	uint32_t           maxInterStageShaderVariables;
	uint32_t           maxColorAttachments;
	uint32_t           maxColorAttachmentBytesPerSample;
	uint32_t           maxComputeWorkgroupStorageSize;
	uint32_t           maxComputeInvocationsPerWorkgroup;
	uint32_t           maxComputeWorkgroupSizeX;
	uint32_t           maxComputeWorkgroupSizeY;
	uint32_t           maxComputeWorkgroupSizeZ;
	uint32_t           maxComputeWorkgroupsPerDimension;
	uint32_t           maxImmediateSize;
} WGPULimits;

typedef struct WGPUMultisampleState {
	WGPUChainedStruct *nextInChain;
	uint32_t           count;
	uint32_t           mask;
	WGPUBool           alphaToCoverageEnabled;
} WGPUMultisampleState;

typedef struct WGPUOrigin3D {
	uint32_t x;
	uint32_t y;
	uint32_t z;
} WGPUOrigin3D;

typedef struct WGPUPassTimestampWrites {
	WGPUChainedStruct *nextInChain;
	WGPUQuerySet       querySet;
	uint32_t           beginningOfPassWriteIndex;
	uint32_t           endOfPassWriteIndex;
} WGPUPassTimestampWrites;

typedef struct WGPUPipelineLayoutDescriptor {
	WGPUChainedStruct         *nextInChain;
	WGPUStringView             label;
	size_t                     bindGroupLayoutCount;
	WGPUBindGroupLayout const *bindGroupLayouts;
	uint32_t                   immediateSize;
} WGPUPipelineLayoutDescriptor;

typedef struct WGPUPrimitiveState {
	WGPUChainedStruct    *nextInChain;
	WGPUPrimitiveTopology topology;
	WGPUIndexFormat       stripIndexFormat;
	WGPUFrontFace         frontFace;
	WGPUCullMode          cullMode;
	WGPUBool              unclippedDepth;
} WGPUPrimitiveState;

typedef struct WGPUQueueDescriptor {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     label;
} WGPUQueueDescriptor;

typedef struct WGPURenderPassDepthStencilAttachment {
	WGPUChainedStruct *nextInChain;
	WGPUTextureView    view;
	WGPULoadOp         depthLoadOp;
	WGPUStoreOp        depthStoreOp;
	float              depthClearValue;
	WGPUBool           depthReadOnly;
	WGPULoadOp         stencilLoadOp;
	WGPUStoreOp        stencilStoreOp;
	uint32_t           stencilClearValue;
	WGPUBool           stencilReadOnly;
} WGPURenderPassDepthStencilAttachment;

typedef struct WGPUSamplerBindingLayout {
	WGPUChainedStruct     *nextInChain;
	WGPUSamplerBindingType type;
} WGPUSamplerBindingLayout;

typedef struct WGPUSamplerDescriptor {
	WGPUChainedStruct   *nextInChain;
	WGPUStringView       label;
	WGPUAddressMode      addressModeU;
	WGPUAddressMode      addressModeV;
	WGPUAddressMode      addressModeW;
	WGPUFilterMode       magFilter;
	WGPUFilterMode       minFilter;
	WGPUMipmapFilterMode mipmapFilter;
	float                lodMinClamp;
	float                lodMaxClamp;
	WGPUCompareFunction  compare;
	uint16_t             maxAnisotropy;
} WGPUSamplerDescriptor;

typedef struct WGPUShaderSourceWGSL {
	WGPUChainedStruct chain;
	WGPUStringView    code;
} WGPUShaderSourceWGSL;

typedef struct WGPUStencilFaceState {
	WGPUCompareFunction  compare;
	WGPUStencilOperation failOp;
	WGPUStencilOperation depthFailOp;
	WGPUStencilOperation passOp;
} WGPUStencilFaceState;

typedef struct WGPUStorageTextureBindingLayout {
	WGPUChainedStruct       *nextInChain;
	WGPUStorageTextureAccess access;
	WGPUTextureFormat        format;
	WGPUTextureViewDimension viewDimension;
} WGPUStorageTextureBindingLayout;

typedef struct WGPUSurfaceCapabilities {
	WGPUChainedStruct            *nextInChain;
	WGPUTextureUsage              usages;
	size_t                        formatCount;
	WGPUTextureFormat const      *formats;
	size_t                        presentModeCount;
	WGPUPresentMode const        *presentModes;
	size_t                        alphaModeCount;
	WGPUCompositeAlphaMode const *alphaModes;
} WGPUSurfaceCapabilities;

typedef struct WGPUSurfaceConfiguration {
	WGPUChainedStruct       *nextInChain;
	WGPUDevice               device;
	WGPUTextureFormat        format;
	WGPUTextureUsage         usage;
	uint32_t                 width;
	uint32_t                 height;
	size_t                   viewFormatCount;
	WGPUTextureFormat const *viewFormats;
	WGPUCompositeAlphaMode   alphaMode;
	WGPUPresentMode          presentMode;
} WGPUSurfaceConfiguration;

typedef struct WGPUSurfaceTexture {
	WGPUChainedStruct                 *nextInChain;
	WGPUTexture                        texture;
	WGPUSurfaceGetCurrentTextureStatus status;
} WGPUSurfaceTexture;

typedef struct WGPUTexelCopyBufferLayout {
	uint64_t offset;
	uint32_t bytesPerRow;
	uint32_t rowsPerImage;
} WGPUTexelCopyBufferLayout;

typedef struct WGPUTextureBindingLayout {
	WGPUChainedStruct       *nextInChain;
	WGPUTextureSampleType    sampleType;
	WGPUTextureViewDimension viewDimension;
	WGPUBool                 multisampled;
} WGPUTextureBindingLayout;

typedef struct WGPUTextureViewDescriptor {
	WGPUChainedStruct       *nextInChain;
	WGPUStringView           label;
	WGPUTextureFormat        format;
	WGPUTextureViewDimension dimension;
	uint32_t                 baseMipLevel;
	uint32_t                 mipLevelCount;
	uint32_t                 baseArrayLayer;
	uint32_t                 arrayLayerCount;
	WGPUTextureAspect        aspect;
	WGPUTextureUsage         usage;
} WGPUTextureViewDescriptor;

typedef struct WGPUVertexAttribute {
	WGPUChainedStruct *nextInChain;
	WGPUVertexFormat   format;
	uint64_t           offset;
	uint32_t           shaderLocation;
} WGPUVertexAttribute;

typedef struct WGPUBindGroupDescriptor {
	WGPUChainedStruct        *nextInChain;
	WGPUStringView            label;
	WGPUBindGroupLayout       layout;
	size_t                    entryCount;
	WGPUBindGroupEntry const *entries;
} WGPUBindGroupDescriptor;

typedef struct WGPUBindGroupLayoutEntry {
	WGPUChainedStruct              *nextInChain;
	uint32_t                        binding;
	WGPUShaderStage                 visibility;
	uint32_t                        bindingArraySize;
	WGPUBufferBindingLayout         buffer;
	WGPUSamplerBindingLayout        sampler;
	WGPUTextureBindingLayout        texture;
	WGPUStorageTextureBindingLayout storageTexture;
} WGPUBindGroupLayoutEntry;

typedef struct WGPUBlendState {
	WGPUBlendComponent color;
	WGPUBlendComponent alpha;
} WGPUBlendState;

typedef struct WGPUDepthStencilState {
	WGPUChainedStruct   *nextInChain;
	WGPUTextureFormat    format;
	WGPUOptionalBool     depthWriteEnabled;
	WGPUCompareFunction  depthCompare;
	WGPUStencilFaceState stencilFront;
	WGPUStencilFaceState stencilBack;
	uint32_t             stencilReadMask;
	uint32_t             stencilWriteMask;
	int32_t              depthBias;
	float                depthBiasSlopeScale;
	float                depthBiasClamp;
} WGPUDepthStencilState;

typedef struct WGPUDeviceDescriptor {
	WGPUChainedStruct              *nextInChain;
	WGPUStringView                  label;
	size_t                          requiredFeatureCount;
	WGPUFeatureName const          *requiredFeatures;
	WGPU_NULLABLE WGPULimits const *requiredLimits;
	WGPUQueueDescriptor             defaultQueue;
	WGPUDeviceLostCallbackInfo      deviceLostCallbackInfo;
	WGPUUncapturedErrorCallbackInfo uncapturedErrorCallbackInfo;
} WGPUDeviceDescriptor;

typedef struct WGPUInstanceDescriptor {
	WGPUChainedStruct                      *nextInChain;
	size_t                                  requiredFeatureCount;
	WGPUInstanceFeatureName const          *requiredFeatures;
	WGPU_NULLABLE WGPUInstanceLimits const *requiredLimits;
} WGPUInstanceDescriptor;

typedef struct WGPURenderPassColorAttachment {
	WGPUChainedStruct            *nextInChain;
	WGPU_NULLABLE WGPUTextureView view;
	uint32_t                      depthSlice;
	WGPU_NULLABLE WGPUTextureView resolveTarget;
	WGPULoadOp                    loadOp;
	WGPUStoreOp                   storeOp;
	WGPUColor                     clearValue;
} WGPURenderPassColorAttachment;

typedef struct WGPURequestAdapterOptions {
	WGPUChainedStruct        *nextInChain;
	WGPUFeatureLevel          featureLevel;
	WGPUPowerPreference       powerPreference;
	WGPUBool                  forceFallbackAdapter;
	WGPUBackendType           backendType;
	WGPU_NULLABLE WGPUSurface compatibleSurface;
} WGPURequestAdapterOptions;

typedef struct WGPUShaderModuleDescriptor {
	WGPUChainedStruct *nextInChain;
	WGPUStringView     label;
} WGPUShaderModuleDescriptor;

typedef struct WGPUTexelCopyBufferInfo {
	WGPUTexelCopyBufferLayout layout;
	WGPUBuffer                buffer;
} WGPUTexelCopyBufferInfo;

typedef struct WGPUTexelCopyTextureInfo {
	WGPUTexture       texture;
	uint32_t          mipLevel;
	WGPUOrigin3D      origin;
	WGPUTextureAspect aspect;
} WGPUTexelCopyTextureInfo;

typedef struct WGPUTextureDescriptor {
	WGPUChainedStruct       *nextInChain;
	WGPUStringView           label;
	WGPUTextureUsage         usage;
	WGPUTextureDimension     dimension;
	WGPUExtent3D             size;
	WGPUTextureFormat        format;
	uint32_t                 mipLevelCount;
	uint32_t                 sampleCount;
	size_t                   viewFormatCount;
	WGPUTextureFormat const *viewFormats;
} WGPUTextureDescriptor;

typedef struct WGPUVertexBufferLayout {
	WGPUChainedStruct         *nextInChain;
	WGPUVertexStepMode         stepMode;
	uint64_t                   arrayStride;
	size_t                     attributeCount;
	WGPUVertexAttribute const *attributes;
} WGPUVertexBufferLayout;

typedef struct WGPUBindGroupLayoutDescriptor {
	WGPUChainedStruct              *nextInChain;
	WGPUStringView                  label;
	size_t                          entryCount;
	WGPUBindGroupLayoutEntry const *entries;
} WGPUBindGroupLayoutDescriptor;

typedef struct WGPUColorTargetState {
	WGPUChainedStruct                  *nextInChain;
	WGPUTextureFormat                   format;
	WGPU_NULLABLE WGPUBlendState const *blend;
	WGPUColorWriteMask                  writeMask;
} WGPUColorTargetState;

typedef struct WGPURenderPassDescriptor {
	WGPUChainedStruct                                        *nextInChain;
	WGPUStringView                                            label;
	size_t                                                    colorAttachmentCount;
	WGPURenderPassColorAttachment const                      *colorAttachments;
	WGPU_NULLABLE WGPURenderPassDepthStencilAttachment const *depthStencilAttachment;
	WGPU_NULLABLE WGPUQuerySet                                occlusionQuerySet;
	WGPU_NULLABLE WGPUPassTimestampWrites const              *timestampWrites;
} WGPURenderPassDescriptor;

typedef struct WGPUVertexState {
	WGPUChainedStruct            *nextInChain;
	WGPUShaderModule              module;
	WGPUStringView                entryPoint;
	size_t                        constantCount;
	WGPUConstantEntry const      *constants;
	size_t                        bufferCount;
	WGPUVertexBufferLayout const *buffers;
} WGPUVertexState;

typedef struct WGPUFragmentState {
	WGPUChainedStruct          *nextInChain;
	WGPUShaderModule            module;
	WGPUStringView              entryPoint;
	size_t                      constantCount;
	WGPUConstantEntry const    *constants;
	size_t                      targetCount;
	WGPUColorTargetState const *targets;
} WGPUFragmentState;

typedef struct WGPURenderPipelineDescriptor {
	WGPUChainedStruct                         *nextInChain;
	WGPUStringView                             label;
	WGPU_NULLABLE WGPUPipelineLayout           layout;
	WGPUVertexState                            vertex;
	WGPUPrimitiveState                         primitive;
	WGPU_NULLABLE WGPUDepthStencilState const *depthStencil;
	WGPUMultisampleState                       multisample;
	WGPU_NULLABLE WGPUFragmentState const     *fragment;
} WGPURenderPipelineDescriptor;

#define IMPORT(str) __attribute__((import_module("imports"), import_name(str)))

IMPORT("wgpuDeviceCreateTexture") WGPUTexture wgpuDeviceCreateTexture(WGPUDevice device, WGPUTextureDescriptor const *descriptor);
IMPORT("wgpuTextureCreateView") WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, WGPU_NULLABLE WGPUTextureViewDescriptor const *descriptor);
IMPORT("wgpuCreateInstance") WGPUInstance wgpuCreateInstance(WGPU_NULLABLE WGPUInstanceDescriptor const *descriptor);
// IMPORT("wgpuInstanceRequestAdapter") WGPUFuture wgpuInstanceRequestAdapter(WGPUInstance instance, WGPU_NULLABLE WGPURequestAdapterOptions const * options,
// WGPURequestAdapterCallbackInfo callbackInfo);
IMPORT("wgpuAdapterGetInfo") WGPUStatus wgpuAdapterGetInfo(WGPUAdapter adapter, WGPUAdapterInfo *info);
IMPORT("wgpuAdapterInfoFreeMembers") void wgpuAdapterInfoFreeMembers(WGPUAdapterInfo adapterInfo);
IMPORT("wgpuAdapterRequestDevice")
WGPUFuture wgpuAdapterRequestDevice(WGPUAdapter adapter, WGPU_NULLABLE WGPUDeviceDescriptor const *descriptor, WGPURequestDeviceCallbackInfo callbackInfo);
IMPORT("wgpuDeviceGetQueue") WGPUQueue wgpuDeviceGetQueue(WGPUDevice device);
IMPORT("wgpuDeviceCreateBindGroupLayout")
WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice device, WGPUBindGroupLayoutDescriptor const *descriptor);
IMPORT("wgpuDeviceCreateBuffer") WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, WGPUBufferDescriptor const *descriptor);
IMPORT("wgpuBufferGetMappedRange") void *wgpuBufferGetMappedRange(WGPUBuffer buffer, size_t offset, size_t size);
IMPORT("wgpuBufferUnmap") void wgpuBufferUnmap(WGPUBuffer buffer);
IMPORT("wgpuDeviceCreateCommandEncoder")
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, WGPU_NULLABLE WGPUCommandEncoderDescriptor const *descriptor);
IMPORT("wgpuCommandEncoderCopyBufferToTexture")
void wgpuCommandEncoderCopyBufferToTexture(WGPUCommandEncoder commandEncoder, WGPUTexelCopyBufferInfo const *source,
                                           WGPUTexelCopyTextureInfo const *destination, WGPUExtent3D const *copySize);
IMPORT("wgpuCommandEncoderFinish")
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder commandEncoder, WGPU_NULLABLE WGPUCommandBufferDescriptor const *descriptor);
IMPORT("wgpuQueueSubmit") void wgpuQueueSubmit(WGPUQueue queue, size_t commandCount, WGPUCommandBuffer const *commands);
IMPORT("wgpuBufferRelease") void wgpuBufferRelease(WGPUBuffer buffer);
IMPORT("wgpuDeviceCreateSampler") WGPUSampler wgpuDeviceCreateSampler(WGPUDevice device, WGPU_NULLABLE WGPUSamplerDescriptor const *descriptor);
IMPORT("wgpuSurfaceGetCapabilities") WGPUStatus wgpuSurfaceGetCapabilities(WGPUSurface surface, WGPUAdapter adapter, WGPUSurfaceCapabilities *capabilities);
IMPORT("wgpuSurfaceCapabilitiesFreeMembers") void wgpuSurfaceCapabilitiesFreeMembers(WGPUSurfaceCapabilities surfaceCapabilities);
IMPORT("wgpuBufferDestroy") void wgpuBufferDestroy(WGPUBuffer buffer);
IMPORT("wgpuSurfaceGetCurrentTexture") void wgpuSurfaceGetCurrentTexture(WGPUSurface surface, WGPUSurfaceTexture *surfaceTexture);
IMPORT("wgpuCommandEncoderBeginRenderPass")
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder commandEncoder, WGPURenderPassDescriptor const *descriptor);
IMPORT("wgpuRenderPassEncoderSetViewport")
void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder renderPassEncoder, float x, float y, float width, float height, float minDepth, float maxDepth);
IMPORT("wgpuRenderPassEncoderSetScissorRect")
void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder renderPassEncoder, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
IMPORT("wgpuRenderPassEncoderEnd") void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder renderPassEncoder);
IMPORT("wgpuRenderPassEncoderRelease") void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder renderPassEncoder);
IMPORT("wgpuCommandBufferRelease") void wgpuCommandBufferRelease(WGPUCommandBuffer commandBuffer);
IMPORT("wgpuCommandEncoderRelease") void wgpuCommandEncoderRelease(WGPUCommandEncoder commandEncoder);
IMPORT("wgpuSurfacePresent") WGPUStatus wgpuSurfacePresent(WGPUSurface surface);
IMPORT("wgpuRenderPassEncoderDrawIndexed")
void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder renderPassEncoder, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                      int32_t baseVertex, uint32_t firstInstance);
IMPORT("wgpuRenderPassEncoderSetPipeline") void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder renderPassEncoder, WGPURenderPipeline pipeline);
IMPORT("wgpuRenderPassEncoderSetVertexBuffer")
void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder renderPassEncoder, uint32_t slot, WGPU_NULLABLE WGPUBuffer buffer, uint64_t offset,
                                          uint64_t size);
IMPORT("wgpuRenderPassEncoderSetIndexBuffer")
void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder renderPassEncoder, WGPUBuffer buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size);
IMPORT("wgpuDeviceCreateBindGroup") WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, WGPUBindGroupDescriptor const *descriptor);
IMPORT("wgpuRenderPassEncoderSetBindGroup")
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder renderPassEncoder, uint32_t groupIndex, WGPU_NULLABLE WGPUBindGroup group,
                                       size_t dynamicOffsetCount, uint32_t const *dynamicOffsets);
IMPORT("wgpuBindGroupRelease") void wgpuBindGroupRelease(WGPUBindGroup bindGroup);
IMPORT("wgpuRenderPipelineRelease") void wgpuRenderPipelineRelease(WGPURenderPipeline renderPipeline);
IMPORT("wgpuPipelineLayoutRelease") void wgpuPipelineLayoutRelease(WGPUPipelineLayout pipelineLayout);
IMPORT("wgpuDeviceCreatePipelineLayout") WGPUPipelineLayout wgpuDeviceCreatePipelineLayout(WGPUDevice device, WGPUPipelineLayoutDescriptor const *descriptor);
IMPORT("wgpuDeviceCreateShaderModule") WGPUShaderModule wgpuDeviceCreateShaderModule(WGPUDevice device, WGPUShaderModuleDescriptor const *descriptor);
IMPORT("wgpuDeviceCreateRenderPipeline") WGPURenderPipeline wgpuDeviceCreateRenderPipeline(WGPUDevice device, WGPURenderPipelineDescriptor const *descriptor);
IMPORT("wgpuShaderModuleRelease") void wgpuShaderModuleRelease(WGPUShaderModule shaderModule);
IMPORT("wgpuQueueWriteBuffer") void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t bufferOffset, void const *data, size_t size);
IMPORT("wgpuTextureDestroy") void wgpuTextureDestroy(WGPUTexture texture);
IMPORT("wgpuTextureRelease") void wgpuTextureRelease(WGPUTexture texture);
IMPORT("wgpuTextureViewRelease") void wgpuTextureViewRelease(WGPUTextureView textureView);
IMPORT("wgpuCommandEncoderCopyBufferToBuffer")
void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder commandEncoder, WGPUBuffer source, uint64_t sourceOffset, WGPUBuffer destination,
                                          uint64_t destinationOffset, uint64_t size);
IMPORT("wgpuSurfaceConfigure") void wgpuSurfaceConfigure(WGPUSurface surface, WGPUSurfaceConfiguration const *config);

IMPORT("wgpuInstanceRequestAdapterSync") WGPUAdapter wgpuInstanceRequestAdapterSync();
IMPORT("wgpuAdapterRequestDeviceSync") WGPUDevice wgpuAdapterRequestDeviceSync();
IMPORT("wgpuDeviceHasFeature") WGPUBool wgpuDeviceHasFeature(WGPUDevice device, WGPUFeatureName feature);
