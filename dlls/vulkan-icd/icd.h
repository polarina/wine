#ifndef __DLLS_VULKAN_ICD_ICD_H
#define __DLLS_VULKAN_ICD_ICD_H

#include <stddef.h>
#include <stdint.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#include "vk_icd.h"
#include "vulkan.h"

#include "allocator.h"
#include "icd_autogen.h"

typedef struct {
	PFN_vkCreateInstance vkCreateInstance;
} vulkan_global_pfn;

typedef struct {
	PFN_vkCreateDevice vkCreateDevice;
	PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
	PFN_vkDestroyInstance vkDestroyInstance;
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
	PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
	PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR vkGetPhysicalDeviceXlibPresentationSupportKHR;
} vulkan_instance_pfn;

struct VkCommandBuffer_T {
	VK_LOADER_DATA loader_data;
	VkDevice device;
	VkCommandBuffer commandBuffer;
};

struct VkDevice_T {
	VK_LOADER_DATA loader_data;
	VkInstance instance;
	VkDevice device;

	struct allocator *pAllocator;
	struct allocator allocator;

	struct allocator_store commandPoolAllocators;

	uint32_t queueFamilyCount;
	VkQueue *queueFamilies;

	vulkan_device_pfn pfn;
};

struct VkInstance_T {
	VK_LOADER_DATA loader_data;
	void *libvulkan_handle;
	VkInstance instance;

	struct allocator *pAllocator;
	struct allocator allocator;

	uint32_t physicalDeviceCount;
	VkPhysicalDevice physicalDevices;

	vulkan_global_pfn pfng;
	vulkan_instance_pfn pfn;
};

struct VkPhysicalDevice_T {
	VK_LOADER_DATA loader_data;
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
};

struct VkQueue_T {
	VK_LOADER_DATA loader_data;
	VkDevice device;
	VkQueue queue;
};

typedef struct {
	const char *pName;
	void *pfn;
} vulkan_function;

extern const vulkan_function vulkan_device_functions[];
extern const size_t vulkan_device_function_count;

void load_device_pfn(
	VkDevice                 device,
	PFN_vkGetDeviceProcAddr  get,
	vulkan_device_pfn       *pfn);

#endif
