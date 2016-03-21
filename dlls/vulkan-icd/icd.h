#ifndef __DLLS_VULKAN_ICD_ICD_H
#define __DLLS_VULKAN_ICD_ICD_H

#include <stddef.h>
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include "vk_icd.h"
#include "vulkan.h"

#include "allocator.h"

typedef struct {
	PFN_vkCreateInstance vkCreateInstance;
} vulkan_global_pfn;

typedef struct {
	PFN_vkCreateDevice vkCreateDevice;
	PFN_vkDestroyInstance vkDestroyInstance;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
	PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
	PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
} vulkan_instance_pfn;

typedef struct {
	PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
	PFN_vkDestroyDevice vkDestroyDevice;
	PFN_vkGetDeviceQueue vkGetDeviceQueue;
	PFN_vkQueueSubmit vkQueueSubmit;
} vulkan_device_pfn;

struct VkDevice_T {
	VK_LOADER_DATA loader_data;
	VkInstance instance;
	VkDevice device;

	struct allocator *pAllocator;
	struct allocator allocator;

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

#endif
