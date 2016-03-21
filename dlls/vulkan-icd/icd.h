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
	PFN_vkDestroyInstance vkDestroyInstance;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
} vulkan_instance_pfn;

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

typedef struct {
	const char *pName;
	void *pfn;
} vulkan_function;

#endif
