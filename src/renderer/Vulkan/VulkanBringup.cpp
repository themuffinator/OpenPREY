// Copyright (C) 2026 DarkMatter Productions
//

// the engine PCH poisons snprintf/vsnprintf toward idStr; this bring-up TU
// keeps its C-style formatting (services-only surface, no idStr dependency)
#undef snprintf
#undef vsnprintf
#include <climits>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "volk.h"
#include "vk_mem_alloc.h"

#include "../RenderModuleAPI.h"
#include "VulkanBringup.h"

/*
===============================================================================

	Phase A bring-up probe. Every step checks its VkResult and reports
	through the engine services table; a missing Vulkan loader or an
	unsuitable device must produce a clear message, never a crash, so the
	engine-side fallback ladder can always land on OpenGL.

===============================================================================
*/

static const renderModuleServices_t *vk_services = NULL;
static bool vk_volkInitialized = false;

// the feature floor the real Vulkan renderer is designed against
// (docs/dev/plans/2026-07-16-vulkan-renderer.md, "Vulkan technical design")
#define VK_BRINGUP_REQUIRED_API_VERSION		VK_API_VERSION_1_3
// mirrors VK_REQUIRED_BOUND_DESCRIPTOR_SETS in VulkanDevice.h; kept as its own
// literal because the probe deliberately links none of the device back end
#define VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS	8

typedef struct vkBringupDeviceInfo_s {
	VkPhysicalDevice					physicalDevice;
	VkPhysicalDeviceProperties			props;
	uint32_t							graphicsQueueFamily;
	uint32_t							transferQueueFamily;		// dedicated transfer family when present, else graphics
	bool								hasGraphicsQueue;
	bool								hasDedicatedTransferQueue;
	VkDeviceSize						deviceLocalBytes;
	bool								hasDynamicRendering;
	bool								hasSynchronization2;
	bool								hasTimelineSemaphore;
	bool								hasDescriptorIndexing;
	bool								hasBufferDeviceAddress;
	bool								hasSamplerAnisotropy;
	bool								hasTextureCompressionBC;
	bool								hasDepthBounds;
	bool								meetsRequirements;
	int									score;
} vkBringupDeviceInfo_t;

void VK_Bringup_SetServices( const renderModuleServices_t *services ) {
	vk_services = services;
}

static void VK_Printf( const char *fmt, ... ) {
	va_list	args;
	char	text[ 2048 ];

	if ( vk_services == NULL || vk_services->Printf == NULL ) {
		return;
	}
	va_start( args, fmt );
	vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );
	text[ sizeof( text ) - 1 ] = '\0';
	vk_services->Printf( "%s", text );
}

static void VK_Warning( const char *fmt, ... ) {
	va_list	args;
	char	text[ 2048 ];

	if ( vk_services == NULL || vk_services->Warning == NULL ) {
		return;
	}
	va_start( args, fmt );
	vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );
	text[ sizeof( text ) - 1 ] = '\0';
	vk_services->Warning( "%s", text );
}

static const char *VK_ResultName( VkResult result ) {
	switch ( result ) {
		case VK_SUCCESS:							return "VK_SUCCESS";
		case VK_ERROR_OUT_OF_HOST_MEMORY:			return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:			return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:		return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:			return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:		return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:			return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:			return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_DEVICE_LOST:					return "VK_ERROR_DEVICE_LOST";
		case VK_TIMEOUT:							return "VK_TIMEOUT";
		default:									return "VK_ERROR (unlisted)";
	}
}

static const char *VK_DeviceTypeName( VkPhysicalDeviceType type ) {
	switch ( type ) {
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:		return "discrete";
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:	return "integrated";
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:		return "virtual";
		case VK_PHYSICAL_DEVICE_TYPE_CPU:				return "cpu";
		default:										return "other";
	}
}

static bool VK_CVarGetBool( const char *name ) {
	if ( vk_services == NULL || vk_services->CVar_GetBool == NULL ) {
		return false;
	}
	return vk_services->CVar_GetBool( name );
}

static int VK_CVarGetInteger( const char *name ) {
	if ( vk_services == NULL || vk_services->CVar_GetInteger == NULL ) {
		return -1;
	}
	return vk_services->CVar_GetInteger( name );
}

/*
====================
VK_Bringup_DeviceExtensionSupported
====================
*/
static bool VK_Bringup_DeviceExtensionSupported( VkPhysicalDevice physicalDevice, const char *name ) {
	uint32_t count = 0;
	if ( vkEnumerateDeviceExtensionProperties( physicalDevice, NULL, &count, NULL ) != VK_SUCCESS || count == 0 ) {
		return false;
	}
	if ( count > 512 ) {
		count = 512;
	}
	static VkExtensionProperties properties[ 512 ];
	if ( vkEnumerateDeviceExtensionProperties( physicalDevice, NULL, &count, properties ) != VK_SUCCESS ) {
		return false;
	}
	for ( uint32_t i = 0; i < count; i++ ) {
		if ( strcmp( properties[ i ].extensionName, name ) == 0 ) {
			return true;
		}
	}
	return false;
}

/*
====================
VK_Bringup_ReportPortability

Records the portability facts a Metal-backed implementation decides at runtime.
This is the evidence artifact the macOS MoltenVK lane publishes: every capability
the renderer negotiates rather than assumes is printed here so a CI log answers
them without needing a debugger on an Apple machine.
====================
*/
static void VK_Bringup_ReportPortability( VkPhysicalDevice physicalDevice, const vkBringupDeviceInfo_t &info ) {
	const bool subset = VK_Bringup_DeviceExtensionSupported(
			physicalDevice, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME );

	VK_Printf( "  portability: subset=%s maxBoundDescriptorSets=%u (need %d)\n",
			subset ? "yes" : "no",
			info.props.limits.maxBoundDescriptorSets,
			VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS );

	if ( subset ) {
		VkPhysicalDevicePortabilitySubsetFeaturesKHR portability;
		memset( &portability, 0, sizeof( portability ) );
		portability.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
		VkPhysicalDeviceFeatures2 query;
		memset( &query, 0, sizeof( query ) );
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		query.pNext = &portability;
		vkGetPhysicalDeviceFeatures2( physicalDevice, &query );

		VK_Printf( "  portability subset: imageViewFormatSwizzle=%d imageViewFormatReinterpretation=%d "
				"mutableComparisonSamplers=%d samplerMipLodBias=%d triangleFans=%d "
				"vertexAttributeAccessBeyondStride=%d constantAlphaColorBlendFactors=%d separateStencilMaskRef=%d\n",
				portability.imageViewFormatSwizzle ? 1 : 0,
				portability.imageViewFormatReinterpretation ? 1 : 0,
				portability.mutableComparisonSamplers ? 1 : 0,
				portability.samplerMipLodBias ? 1 : 0,
				portability.triangleFans ? 1 : 0,
				portability.vertexAttributeAccessBeyondStride ? 1 : 0,
				portability.constantAlphaColorBlendFactors ? 1 : 0,
				portability.separateStencilMaskRef ? 1 : 0 );
	}

	// formats the back end uses without a fallback today
	static const struct {
		VkFormat		format;
		const char *	name;
	} probedFormats[] = {
		{ VK_FORMAT_R5G6B5_UNORM_PACK16,	"R5G6B5 (light cookies)" },
		{ VK_FORMAT_BC1_RGB_UNORM_BLOCK,	"BC1" },
		{ VK_FORMAT_BC3_UNORM_BLOCK,		"BC3" },
		{ VK_FORMAT_BC7_UNORM_BLOCK,		"BC7" },
		{ VK_FORMAT_R16G16B16A16_SFLOAT,	"RGBA16F (HDR scene)" },
	};
	for ( int i = 0; i < (int)( sizeof( probedFormats ) / sizeof( probedFormats[ 0 ] ) ); i++ ) {
		VkFormatProperties props;
		memset( &props, 0, sizeof( props ) );
		vkGetPhysicalDeviceFormatProperties( physicalDevice, probedFormats[ i ].format, &props );
		VK_Printf( "  format %s: sampled=%d filterLinear=%d colorAttachment=%d\n",
				probedFormats[ i ].name,
				( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) != 0 ? 1 : 0,
				( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) != 0 ? 1 : 0,
				( props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ) != 0 ? 1 : 0 );
	}

	// VkFormatProperties3 population (core 1.3): the shadow depth-format probe
	// depends on it, and the depth-comparison bit has no 1.0 equivalent
	VkFormatProperties3 props3;
	memset( &props3, 0, sizeof( props3 ) );
	props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
	VkFormatProperties2 props2;
	memset( &props2, 0, sizeof( props2 ) );
	props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
	props2.pNext = &props3;
	vkGetPhysicalDeviceFormatProperties2( physicalDevice, VK_FORMAT_D32_SFLOAT, &props2 );
	VK_Printf( "  formatProperties3 populated=%d depthComparison(D32_SFLOAT)=%d\n",
			props3.optimalTilingFeatures != 0 ? 1 : 0,
			( props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT ) != 0 ? 1 : 0 );

	VkPhysicalDeviceDepthStencilResolveProperties resolveProperties;
	memset( &resolveProperties, 0, sizeof( resolveProperties ) );
	resolveProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
	VkPhysicalDeviceProperties2 properties2;
	memset( &properties2, 0, sizeof( properties2 ) );
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties2.pNext = &resolveProperties;
	vkGetPhysicalDeviceProperties2( physicalDevice, &properties2 );
	VK_Printf( "  depthResolveModes=0x%x stencilResolveModes=0x%x sampleCounts(color)=0x%x\n",
			(unsigned)resolveProperties.supportedDepthResolveModes,
			(unsigned)resolveProperties.supportedStencilResolveModes,
			(unsigned)info.props.limits.framebufferColorSampleCounts );
}

/*
====================
VK_Bringup_QueryDevice
====================
*/
static void VK_Bringup_QueryDevice( VkPhysicalDevice physicalDevice, vkBringupDeviceInfo_t &info ) {
	memset( &info, 0, sizeof( info ) );
	info.physicalDevice = physicalDevice;
	info.graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	info.transferQueueFamily = VK_QUEUE_FAMILY_IGNORED;

	vkGetPhysicalDeviceProperties( physicalDevice, &info.props );

	VkPhysicalDeviceMemoryProperties memoryProps;
	vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memoryProps );
	for ( uint32_t i = 0; i < memoryProps.memoryHeapCount; i++ ) {
		if ( ( memoryProps.memoryHeaps[ i ].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) != 0 ) {
			info.deviceLocalBytes += memoryProps.memoryHeaps[ i ].size;
		}
	}

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, NULL );
	VkQueueFamilyProperties queueFamilies[ 32 ];
	if ( queueFamilyCount > 32 ) {
		queueFamilyCount = 32;
	}
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyCount, queueFamilies );
	for ( uint32_t i = 0; i < queueFamilyCount; i++ ) {
		const VkQueueFlags flags = queueFamilies[ i ].queueFlags;
		if ( !info.hasGraphicsQueue && ( flags & VK_QUEUE_GRAPHICS_BIT ) != 0 ) {
			info.hasGraphicsQueue = true;
			info.graphicsQueueFamily = i;
		}
		// a transfer-only family (no graphics/compute) marks a DMA queue we
		// want for streaming uploads
		if ( !info.hasDedicatedTransferQueue
				&& ( flags & VK_QUEUE_TRANSFER_BIT ) != 0
				&& ( flags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) == 0 ) {
			info.hasDedicatedTransferQueue = true;
			info.transferQueueFamily = i;
		}
	}
	if ( !info.hasDedicatedTransferQueue ) {
		info.transferQueueFamily = info.graphicsQueueFamily;
	}

	VkPhysicalDeviceVulkan13Features features13;
	memset( &features13, 0, sizeof( features13 ) );
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

	VkPhysicalDeviceVulkan12Features features12;
	memset( &features12, 0, sizeof( features12 ) );
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.pNext = &features13;

	VkPhysicalDeviceFeatures2 features2;
	memset( &features2, 0, sizeof( features2 ) );
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features12;

	if ( info.props.apiVersion >= VK_API_VERSION_1_2 ) {
		vkGetPhysicalDeviceFeatures2( physicalDevice, &features2 );
	}

	info.hasDynamicRendering = features13.dynamicRendering == VK_TRUE;
	info.hasSynchronization2 = features13.synchronization2 == VK_TRUE;
	info.hasTimelineSemaphore = features12.timelineSemaphore == VK_TRUE;
	info.hasDescriptorIndexing = features12.descriptorIndexing == VK_TRUE
			&& features12.runtimeDescriptorArray == VK_TRUE
			&& features12.descriptorBindingPartiallyBound == VK_TRUE
			&& features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
	info.hasBufferDeviceAddress = features12.bufferDeviceAddress == VK_TRUE;
	info.hasSamplerAnisotropy = features2.features.samplerAnisotropy == VK_TRUE;
	info.hasTextureCompressionBC = features2.features.textureCompressionBC == VK_TRUE;
	info.hasDepthBounds = features2.features.depthBounds == VK_TRUE;

	// What the shipping renderer actually requires of a device: the 1.3 core
	// floor, a graphics+present queue, and enough bound descriptor sets for the
	// shadowed-interaction pipeline layout. Timeline semaphores, descriptor
	// indexing and BC compression are reported for information only -- the back
	// end uses none of them for device creation, and gating on them made this
	// probe report FAIL on otherwise perfectly capable portability devices.
	info.meetsRequirements = info.props.apiVersion >= VK_BRINGUP_REQUIRED_API_VERSION
			&& info.hasGraphicsQueue
			&& info.hasDynamicRendering
			&& info.hasSynchronization2
			&& info.props.limits.maxBoundDescriptorSets >= (uint32_t)VK_BRINGUP_REQUIRED_BOUND_DESCRIPTOR_SETS;

	info.score = 0;
	if ( info.meetsRequirements ) {
		info.score += 10000;
	}
	if ( info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
		info.score += 1000;
	} else if ( info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) {
		info.score += 100;
	}
	if ( info.hasDedicatedTransferQueue ) {
		info.score += 10;
	}
	info.score += ( int )( info.deviceLocalBytes / ( 1024ull * 1024ull * 1024ull ) );
}

/*
====================
VK_Bringup_ReportFormatSupport
====================
*/
static void VK_Bringup_ReportFormatSupport( VkPhysicalDevice physicalDevice, bool verbose ) {
	typedef struct {
		VkFormat				format;
		const char *			name;
		VkFormatFeatureFlags	requiredOptimalFeatures;
	} formatCheck_t;

	static const formatCheck_t checks[] = {
		{ VK_FORMAT_BC1_RGB_UNORM_BLOCK,	"BC1(DXT1)",		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_BC3_UNORM_BLOCK,		"BC3(DXT5)",		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_BC7_UNORM_BLOCK,		"BC7",				VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_R16G16B16A16_SFLOAT,	"RGBA16F",			VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT },
		{ VK_FORMAT_D24_UNORM_S8_UINT,		"D24S8",			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
		{ VK_FORMAT_D32_SFLOAT_S8_UINT,		"D32FS8",			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
		{ VK_FORMAT_D32_SFLOAT,				"D32F",				VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT },
	};

	char supported[ 256 ];
	char missing[ 256 ];
	supported[ 0 ] = '\0';
	missing[ 0 ] = '\0';

	for ( size_t i = 0; i < sizeof( checks ) / sizeof( checks[ 0 ] ); i++ ) {
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties( physicalDevice, checks[ i ].format, &formatProps );
		const bool ok = ( formatProps.optimalTilingFeatures & checks[ i ].requiredOptimalFeatures ) == checks[ i ].requiredOptimalFeatures;
		char *target = ok ? supported : missing;
		const size_t targetSize = ok ? sizeof( supported ) : sizeof( missing );
		if ( target[ 0 ] != '\0' ) {
			strncat( target, " ", targetSize - strlen( target ) - 1 );
		}
		strncat( target, checks[ i ].name, targetSize - strlen( target ) - 1 );
	}

	if ( verbose ) {
		VK_Printf( "  formats supported: %s\n", supported[ 0 ] != '\0' ? supported : "(none)" );
	}
	if ( missing[ 0 ] != '\0' ) {
		VK_Printf( "  formats MISSING: %s\n", missing );
	}
}

/*
====================
VK_Bringup_RunProbeInternal
====================
*/
static bool VK_Bringup_RunProbeInternal( bool verbose, char *outSummary, int summaryLength ) {
	VkResult result;

	if ( outSummary != NULL && summaryLength > 0 ) {
		outSummary[ 0 ] = '\0';
	}

	// 1. loader
	if ( !vk_volkInitialized ) {
		result = volkInitialize();
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: no Vulkan loader available on this system (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "no Vulkan loader" );
			}
			return false;
		}
		vk_volkInitialized = true;
	}

	uint32_t loaderVersion = VK_API_VERSION_1_0;
	if ( vkEnumerateInstanceVersion != NULL ) {
		vkEnumerateInstanceVersion( &loaderVersion );
	}
	if ( verbose ) {
		VK_Printf( "Vulkan loader: instance API %u.%u.%u\n",
				VK_API_VERSION_MAJOR( loaderVersion ), VK_API_VERSION_MINOR( loaderVersion ), VK_API_VERSION_PATCH( loaderVersion ) );
	}

	// 2. layers / instance extensions
	bool validationRequested = VK_CVarGetBool( "r_vkValidation" );
	bool validationAvailable = false;
	{
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties( &layerCount, NULL );
		if ( layerCount > 64 ) {
			layerCount = 64;
		}
		VkLayerProperties layers[ 64 ];
		vkEnumerateInstanceLayerProperties( &layerCount, layers );
		for ( uint32_t i = 0; i < layerCount; i++ ) {
			if ( strcmp( layers[ i ].layerName, "VK_LAYER_KHRONOS_validation" ) == 0 ) {
				validationAvailable = true;
				break;
			}
		}
	}
	if ( validationRequested && !validationAvailable ) {
		VK_Warning( "r_vkValidation is set but VK_LAYER_KHRONOS_validation is not installed; continuing without validation" );
	}
	const bool enableValidation = validationRequested && validationAvailable;

	bool hasSurfaceExtension = false;
	bool hasPlatformSurfaceExtension = false;
	bool hasDebugUtils = false;
	bool hasPortabilityEnumeration = false;
	{
		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties( NULL, &extensionCount, NULL );
		if ( extensionCount > 256 ) {
			extensionCount = 256;
		}
		static VkExtensionProperties extensions[ 256 ];
		vkEnumerateInstanceExtensionProperties( NULL, &extensionCount, extensions );
		for ( uint32_t i = 0; i < extensionCount; i++ ) {
			const char *name = extensions[ i ].extensionName;
			if ( strcmp( name, VK_KHR_SURFACE_EXTENSION_NAME ) == 0 ) {
				hasSurfaceExtension = true;
			} else if ( strcmp( name, "VK_KHR_win32_surface" ) == 0
					|| strcmp( name, "VK_KHR_xlib_surface" ) == 0
					|| strcmp( name, "VK_KHR_xcb_surface" ) == 0
					|| strcmp( name, "VK_KHR_wayland_surface" ) == 0
					|| strcmp( name, "VK_EXT_metal_surface" ) == 0 ) {
				hasPlatformSurfaceExtension = true;
			} else if ( strcmp( name, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) == 0 ) {
				hasDebugUtils = true;
			} else if ( strcmp( name, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) == 0 ) {
				hasPortabilityEnumeration = true;
			}
		}
	}
	if ( verbose ) {
		VK_Printf( "Vulkan instance: validation=%s (requested=%d available=%d) surface=%d platformSurface=%d debugUtils=%d\n",
				enableValidation ? "on" : "off", validationRequested ? 1 : 0, validationAvailable ? 1 : 0,
				hasSurfaceExtension ? 1 : 0, hasPlatformSurfaceExtension ? 1 : 0, hasDebugUtils ? 1 : 0 );
	}
	if ( !hasSurfaceExtension || !hasPlatformSurfaceExtension ) {
		VK_Warning( "Vulkan bring-up: presentation extensions incomplete (VK_KHR_surface=%d, platform surface=%d); the full renderer will need them",
				hasSurfaceExtension ? 1 : 0, hasPlatformSurfaceExtension ? 1 : 0 );
	}

	// 3. instance
	VkApplicationInfo appInfo;
	memset( &appInfo, 0, sizeof( appInfo ) );
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "openPREY";
	appInfo.pEngineName = "openPREY";
	appInfo.apiVersion = VK_BRINGUP_REQUIRED_API_VERSION;

	const char *enabledLayers[ 1 ];
	uint32_t enabledLayerCount = 0;
	if ( enableValidation ) {
		enabledLayers[ enabledLayerCount++ ] = "VK_LAYER_KHRONOS_validation";
	}
	const char *enabledExtensions[ 2 ];
	uint32_t enabledExtensionCount = 0;
	if ( hasDebugUtils && enableValidation ) {
		enabledExtensions[ enabledExtensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	// mirror the real device path: without portability enumeration the Khronos
	// loader hides portability ICDs (MoltenVK) and this probe would report a
	// driverless machine on a perfectly working Mac
	if ( hasPortabilityEnumeration ) {
		enabledExtensions[ enabledExtensionCount++ ] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
	}

	VkInstanceCreateInfo instanceInfo;
	memset( &instanceInfo, 0, sizeof( instanceInfo ) );
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledLayerCount = enabledLayerCount;
	instanceInfo.ppEnabledLayerNames = enabledLayerCount > 0 ? enabledLayers : NULL;
	instanceInfo.enabledExtensionCount = enabledExtensionCount;
	instanceInfo.ppEnabledExtensionNames = enabledExtensionCount > 0 ? enabledExtensions : NULL;
	if ( hasPortabilityEnumeration ) {
		instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	VkInstance instance = VK_NULL_HANDLE;
	result = vkCreateInstance( &instanceInfo, NULL, &instance );
	if ( result != VK_SUCCESS ) {
		VK_Printf( "Vulkan bring-up: vkCreateInstance failed (%s)\n", VK_ResultName( result ) );
		if ( outSummary != NULL ) {
			snprintf( outSummary, summaryLength, "instance creation failed: %s", VK_ResultName( result ) );
		}
		return false;
	}
	volkLoadInstance( instance );

	bool probePassed = false;
	VkDevice device = VK_NULL_HANDLE;
	VmaAllocator allocator = VK_NULL_HANDLE;

	// scope with explicit teardown at the bottom; every failure jumps there
	do {
		// 4. physical devices
		uint32_t deviceCount = 0;
		result = vkEnumeratePhysicalDevices( instance, &deviceCount, NULL );
		if ( result != VK_SUCCESS || deviceCount == 0 ) {
			VK_Printf( "Vulkan bring-up: no Vulkan physical devices (%s, count=%u)\n", VK_ResultName( result ), deviceCount );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "no physical devices" );
			}
			break;
		}
		if ( deviceCount > 16 ) {
			deviceCount = 16;
		}
		VkPhysicalDevice physicalDevices[ 16 ];
		vkEnumeratePhysicalDevices( instance, &deviceCount, physicalDevices );

		vkBringupDeviceInfo_t deviceInfos[ 16 ];
		int bestIndex = -1;
		for ( uint32_t i = 0; i < deviceCount; i++ ) {
			vkBringupDeviceInfo_t &info = deviceInfos[ i ];
			VK_Bringup_QueryDevice( physicalDevices[ i ], info );
			if ( verbose ) {
				VK_Printf( "Vulkan device %u: %s (%s) api=%u.%u.%u vram=%llu MB queues: gfx=%d xfer=%s\n",
						i, info.props.deviceName, VK_DeviceTypeName( info.props.deviceType ),
						VK_API_VERSION_MAJOR( info.props.apiVersion ), VK_API_VERSION_MINOR( info.props.apiVersion ), VK_API_VERSION_PATCH( info.props.apiVersion ),
						( unsigned long long )( info.deviceLocalBytes / ( 1024ull * 1024ull ) ),
						info.hasGraphicsQueue ? ( int )info.graphicsQueueFamily : -1,
						info.hasDedicatedTransferQueue ? "dedicated" : "shared" );
				VK_Printf( "  features: dynamicRendering=%d sync2=%d timelineSemaphore=%d descriptorIndexing=%d bufferDeviceAddress=%d anisotropy=%d bc=%d depthBounds=%d -> %s\n",
						info.hasDynamicRendering ? 1 : 0, info.hasSynchronization2 ? 1 : 0, info.hasTimelineSemaphore ? 1 : 0,
						info.hasDescriptorIndexing ? 1 : 0, info.hasBufferDeviceAddress ? 1 : 0, info.hasSamplerAnisotropy ? 1 : 0,
						info.hasTextureCompressionBC ? 1 : 0, info.hasDepthBounds ? 1 : 0,
						info.meetsRequirements ? "SUITABLE" : "unsuitable" );
				VK_Bringup_ReportPortability( physicalDevices[ i ], info );
			VK_Bringup_ReportFormatSupport( physicalDevices[ i ], verbose );
			}
			if ( bestIndex < 0 || info.score > deviceInfos[ bestIndex ].score ) {
				bestIndex = ( int )i;
			}
		}

		const int overrideIndex = VK_CVarGetInteger( "r_vkDevice" );
		int selectedIndex = bestIndex;
		if ( overrideIndex >= 0 ) {
			if ( overrideIndex < ( int )deviceCount ) {
				selectedIndex = overrideIndex;
				if ( verbose ) {
					VK_Printf( "Vulkan device selection: r_vkDevice override -> %d\n", selectedIndex );
				}
			} else {
				VK_Warning( "r_vkDevice %d is out of range (%u devices); using automatic selection", overrideIndex, deviceCount );
			}
		}
		if ( selectedIndex < 0 ) {
			VK_Printf( "Vulkan bring-up: no device could be selected\n" );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "no selectable device" );
			}
			break;
		}

		const vkBringupDeviceInfo_t &selected = deviceInfos[ selectedIndex ];
		VK_Printf( "Vulkan selected device: %s (%s)\n", selected.props.deviceName, VK_DeviceTypeName( selected.props.deviceType ) );
		if ( !selected.meetsRequirements ) {
			VK_Printf( "Vulkan bring-up: selected device does not meet the Vulkan 1.3 feature floor (dynamicRendering/sync2/timelineSemaphore/descriptorIndexing/anisotropy/BC)\n" );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "device '%s' below 1.3 feature floor", selected.props.deviceName );
			}
			break;
		}

		// 5. logical device with the renderer's required feature set
		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfos[ 2 ];
		memset( queueInfos, 0, sizeof( queueInfos ) );
		uint32_t queueInfoCount = 0;
		queueInfos[ queueInfoCount ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[ queueInfoCount ].queueFamilyIndex = selected.graphicsQueueFamily;
		queueInfos[ queueInfoCount ].queueCount = 1;
		queueInfos[ queueInfoCount ].pQueuePriorities = &queuePriority;
		queueInfoCount++;
		if ( selected.hasDedicatedTransferQueue ) {
			queueInfos[ queueInfoCount ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfos[ queueInfoCount ].queueFamilyIndex = selected.transferQueueFamily;
			queueInfos[ queueInfoCount ].queueCount = 1;
			queueInfos[ queueInfoCount ].pQueuePriorities = &queuePriority;
			queueInfoCount++;
		}

		VkPhysicalDeviceVulkan13Features enable13;
		memset( &enable13, 0, sizeof( enable13 ) );
		enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		enable13.dynamicRendering = VK_TRUE;
		enable13.synchronization2 = VK_TRUE;

		VkPhysicalDeviceVulkan12Features enable12;
		memset( &enable12, 0, sizeof( enable12 ) );
		enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		enable12.pNext = &enable13;
		// every optional 1.2 feature is requested only where the device reported
		// it, so a portability implementation that lacks update-after-bind
		// descriptor indexing still creates a probe device instead of failing
		enable12.timelineSemaphore = selected.hasTimelineSemaphore ? VK_TRUE : VK_FALSE;
		enable12.descriptorIndexing = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.runtimeDescriptorArray = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.descriptorBindingPartiallyBound = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.descriptorBindingSampledImageUpdateAfterBind = selected.hasDescriptorIndexing ? VK_TRUE : VK_FALSE;
		enable12.bufferDeviceAddress = selected.hasBufferDeviceAddress ? VK_TRUE : VK_FALSE;

		VkPhysicalDeviceFeatures2 enableFeatures2;
		memset( &enableFeatures2, 0, sizeof( enableFeatures2 ) );
		enableFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		enableFeatures2.pNext = &enable12;
		enableFeatures2.features.samplerAnisotropy = selected.hasSamplerAnisotropy ? VK_TRUE : VK_FALSE;
		enableFeatures2.features.textureCompressionBC = selected.hasTextureCompressionBC ? VK_TRUE : VK_FALSE;
		enableFeatures2.features.depthBounds = selected.hasDepthBounds ? VK_TRUE : VK_FALSE;

		// portability devices (MoltenVK) require their subset extension to be
		// enabled whenever they advertise it
		const char *probeDeviceExtensions[ 1 ];
		uint32_t probeDeviceExtensionCount = 0;
		if ( VK_Bringup_DeviceExtensionSupported( selected.physicalDevice,
					VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME ) ) {
			probeDeviceExtensions[ probeDeviceExtensionCount++ ] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
		}

		VkDeviceCreateInfo deviceInfo;
		memset( &deviceInfo, 0, sizeof( deviceInfo ) );
		deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceInfo.pNext = &enableFeatures2;
		deviceInfo.queueCreateInfoCount = queueInfoCount;
		deviceInfo.pQueueCreateInfos = queueInfos;
		deviceInfo.enabledExtensionCount = probeDeviceExtensionCount;
		deviceInfo.ppEnabledExtensionNames = probeDeviceExtensionCount > 0 ? probeDeviceExtensions : NULL;

		result = vkCreateDevice( selected.physicalDevice, &deviceInfo, NULL, &device );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: vkCreateDevice failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "device creation failed: %s", VK_ResultName( result ) );
			}
			break;
		}
		volkLoadDevice( device );

		VkQueue graphicsQueue = VK_NULL_HANDLE;
		vkGetDeviceQueue( device, selected.graphicsQueueFamily, 0, &graphicsQueue );
		if ( graphicsQueue == VK_NULL_HANDLE ) {
			VK_Printf( "Vulkan bring-up: graphics queue retrieval failed\n" );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "graphics queue retrieval failed" );
			}
			break;
		}

		// 6. VMA + a device-local and a host-visible allocation
		VmaVulkanFunctions vmaFunctions;
		memset( &vmaFunctions, 0, sizeof( vmaFunctions ) );
		vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo;
		memset( &allocatorInfo, 0, sizeof( allocatorInfo ) );
		allocatorInfo.physicalDevice = selected.physicalDevice;
		allocatorInfo.device = device;
		allocatorInfo.instance = instance;
		allocatorInfo.vulkanApiVersion = VK_BRINGUP_REQUIRED_API_VERSION;
		allocatorInfo.pVulkanFunctions = &vmaFunctions;
		if ( selected.hasBufferDeviceAddress ) {
			allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		}

		result = vmaCreateAllocator( &allocatorInfo, &allocator );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: vmaCreateAllocator failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "VMA init failed: %s", VK_ResultName( result ) );
			}
			break;
		}

		VkBufferCreateInfo bufferInfo;
		memset( &bufferInfo, 0, sizeof( bufferInfo ) );
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = 1024 * 1024;
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		VmaAllocationCreateInfo allocInfo;
		memset( &allocInfo, 0, sizeof( allocInfo ) );
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VkBuffer deviceLocalBuffer = VK_NULL_HANDLE;
		VmaAllocation deviceLocalAllocation = VK_NULL_HANDLE;
		result = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &deviceLocalBuffer, &deviceLocalAllocation, NULL );
		if ( result != VK_SUCCESS ) {
			VK_Printf( "Vulkan bring-up: device-local buffer allocation failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "device-local allocation failed: %s", VK_ResultName( result ) );
			}
			break;
		}

		bufferInfo.size = 64 * 1024;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo stagingInfo;
		memset( &stagingInfo, 0, sizeof( stagingInfo ) );
		result = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingInfo );
		bool stagingOk = false;
		if ( result == VK_SUCCESS && stagingInfo.pMappedData != NULL ) {
			memset( stagingInfo.pMappedData, 0xA4, 256 );
			stagingOk = true;
		}
		if ( stagingBuffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( allocator, stagingBuffer, stagingAllocation );
		}
		vmaDestroyBuffer( allocator, deviceLocalBuffer, deviceLocalAllocation );
		if ( !stagingOk ) {
			VK_Printf( "Vulkan bring-up: host-visible mapped staging allocation failed (%s)\n", VK_ResultName( result ) );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "staging allocation failed: %s", VK_ResultName( result ) );
			}
			break;
		}
		if ( verbose ) {
			VK_Printf( "Vulkan memory: VMA allocator + device-local and mapped staging buffers OK\n" );
		}

		// 7. timeline semaphore host signal/query round trip
		VkSemaphoreTypeCreateInfo semaphoreTypeInfo;
		memset( &semaphoreTypeInfo, 0, sizeof( semaphoreTypeInfo ) );
		semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		semaphoreTypeInfo.initialValue = 0;

		VkSemaphoreCreateInfo semaphoreInfo;
		memset( &semaphoreInfo, 0, sizeof( semaphoreInfo ) );
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreInfo.pNext = &semaphoreTypeInfo;

		VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
		result = vkCreateSemaphore( device, &semaphoreInfo, NULL, &timelineSemaphore );
		bool timelineOk = false;
		if ( result == VK_SUCCESS ) {
			VkSemaphoreSignalInfo signalInfo;
			memset( &signalInfo, 0, sizeof( signalInfo ) );
			signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
			signalInfo.semaphore = timelineSemaphore;
			signalInfo.value = 1;
			if ( vkSignalSemaphore( device, &signalInfo ) == VK_SUCCESS ) {
				uint64_t value = 0;
				if ( vkGetSemaphoreCounterValue( device, timelineSemaphore, &value ) == VK_SUCCESS && value == 1 ) {
					timelineOk = true;
				}
			}
			vkDestroySemaphore( device, timelineSemaphore, NULL );
		}
		if ( !timelineOk ) {
			VK_Printf( "Vulkan bring-up: timeline semaphore round trip failed\n" );
			if ( outSummary != NULL ) {
				snprintf( outSummary, summaryLength, "timeline semaphore round trip failed" );
			}
			break;
		}
		if ( verbose ) {
			VK_Printf( "Vulkan sync: timeline semaphore host signal/query OK\n" );
		}

		if ( outSummary != NULL ) {
			snprintf( outSummary, summaryLength, "device '%s' passed bring-up (api %u.%u, %llu MB VRAM)",
					selected.props.deviceName,
					VK_API_VERSION_MAJOR( selected.props.apiVersion ), VK_API_VERSION_MINOR( selected.props.apiVersion ),
					( unsigned long long )( selected.deviceLocalBytes / ( 1024ull * 1024ull ) ) );
		}
		probePassed = true;
	} while ( false );

	// teardown in reverse order
	if ( allocator != VK_NULL_HANDLE ) {
		vmaDestroyAllocator( allocator );
	}
	if ( device != VK_NULL_HANDLE ) {
		vkDestroyDevice( device, NULL );
	}
	vkDestroyInstance( instance, NULL );
	// volk keeps only the loader handle after this; safe for repeat probes

	return probePassed;
}

/*
====================
VK_Bringup_RunProbe
====================
*/
bool VK_Bringup_RunProbe( bool verbose ) {
	return VK_Bringup_RunProbeInternal( verbose, NULL, 0 );
}

/*
====================
VK_Bringup_RunDeviceSelfTest
====================
*/
bool VK_Bringup_RunDeviceSelfTest( char *outSummary, int summaryLength ) {
	return VK_Bringup_RunProbeInternal( false, outSummary, summaryLength );
}

/*
====================
VK_Bringup_Shutdown
====================
*/
void VK_Bringup_Shutdown( void ) {
	// per-probe objects are torn down inside the probe; nothing persists here
	vk_services = NULL;
}
