/* Vulkan ICD implementation
 *
 * Copyright (c) 2016 Gabríel Arthúr Pétursson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "windef.h"
#include "wine/debug.h"
#include "wine/library.h"

#include "icd.h"

#define SONAME_LIBVULKAN "libvulkan.so.1"

static vulkan_function vulkan_device_functions[];
static size_t vulkan_device_function_count;

WINE_DEFAULT_DEBUG_CHANNEL(vulkan_icd);

static int compar(const void *elt_a, const void *elt_b)
{
	return strcmp(
		((const vulkan_function *) elt_a)->pName,
		((const vulkan_function *) elt_b)->pName);
}

static void load_global_pfn(
	PFN_vkGetInstanceProcAddr  get,
	vulkan_global_pfn         *pfn)
{
	#define GET(f) pfn->f = (PFN_##f)get(NULL, #f)

	GET(vkCreateInstance);

	#undef GET
}

static void load_instance_pfn(
	VkInstance                 instance,
	PFN_vkGetInstanceProcAddr  get,
	vulkan_instance_pfn       *pfn)
{
	#define GET(f) pfn->f = (PFN_##f)get(instance, #f)

	GET(vkCreateDevice);
	GET(vkDestroyInstance);
	GET(vkEnumeratePhysicalDevices);
	GET(vkGetDeviceProcAddr);
	GET(vkGetPhysicalDeviceFeatures);
	GET(vkGetPhysicalDeviceFormatProperties);
	GET(vkGetPhysicalDeviceImageFormatProperties);
	GET(vkGetPhysicalDeviceMemoryProperties);
	GET(vkGetPhysicalDeviceProperties);
	GET(vkGetPhysicalDeviceQueueFamilyProperties);
	GET(vkGetPhysicalDeviceSparseImageFormatProperties);

	#undef GET
}

static void load_device_pfn(
	VkDevice                 device,
	PFN_vkGetDeviceProcAddr  get,
	vulkan_device_pfn       *pfn)
{
	#define GET(f) pfn->f = (PFN_##f)get(device, #f);

	GET(vkAllocateCommandBuffers);
	GET(vkCmdExecuteCommands);
	GET(vkCreateCommandPool);
	GET(vkDestroyCommandPool);
	GET(vkDestroyDevice);
	GET(vkFreeCommandBuffers);
	GET(vkGetDeviceQueue);
	GET(vkQueueSubmit);

	#undef GET
}

VkResult WINAPI vkCreateInstance(
	const VkInstanceCreateInfo  *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkInstance                  *pInstance)
{
	VkInstance instance;
	VkResult result;
	VkAllocationCallbacks callbacks;
	struct allocator allocator;
	PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;

	TRACE("(%p, %p, %p)\n", pCreateInfo, pAllocator, pInstance);

	if (pCreateInfo->enabledLayerCount > 0)
		return VK_ERROR_LAYER_NOT_PRESENT;

	if (pCreateInfo->enabledExtensionCount > 0)
		return VK_ERROR_EXTENSION_NOT_PRESENT;

	instance = allocator_allocate(
		allocator_initialize(pAllocator, NULL, &allocator),
		sizeof(*instance),
		8, /* alignof(*instance) */
		VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

	if (instance == NULL)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	set_loader_magic_value(&instance->loader_data);
	instance->pAllocator = allocator_initialize(pAllocator, NULL, &instance->allocator);
	instance->physicalDeviceCount = 0;
	instance->physicalDevices = NULL;
	instance->libvulkan_handle = wine_dlopen(SONAME_LIBVULKAN, RTLD_NOW, NULL, 0);

	if (instance->libvulkan_handle == NULL)
	{
		WARN("Vulkan loader not found (%s)\n", SONAME_LIBVULKAN);
		allocator_free(instance->pAllocator, instance);

		return VK_ERROR_INITIALIZATION_FAILED;
	}

	pfn_vkGetInstanceProcAddr = wine_dlsym(
		instance->libvulkan_handle, "vkGetInstanceProcAddr", NULL, 0);

	if (pfn_vkGetInstanceProcAddr == NULL)
	{
		WARN("Could not dlsym vkGetInstanceProcAddr (%s)\n", SONAME_LIBVULKAN);

		wine_dlclose(instance->libvulkan_handle, NULL, 0);
		allocator_free(instance->pAllocator, instance);

		return VK_ERROR_INITIALIZATION_FAILED;
	}

	load_global_pfn(pfn_vkGetInstanceProcAddr, &instance->pfng);

	result = instance->pfng.vkCreateInstance(
		pCreateInfo,
		allocator_callbacks(instance->pAllocator, &callbacks),
		&instance->instance);

	if (result != VK_SUCCESS)
	{
		wine_dlclose(instance->libvulkan_handle, NULL, 0);
		allocator_free(instance->pAllocator, instance);

		return result;
	}

	load_instance_pfn(instance->instance, pfn_vkGetInstanceProcAddr, &instance->pfn);

	*pInstance = instance;

	return VK_SUCCESS;
}

void WINAPI vkDestroyInstance(
	VkInstance                   instance,
	const VkAllocationCallbacks *pAllocator)
{
	struct allocator *allocatorp;
	struct allocator allocator;
	VkAllocationCallbacks callbacks;

	TRACE("(%p, %p)\n", instance, pAllocator);

	allocatorp = allocator_initialize(pAllocator, NULL, &allocator);

	instance->pfn.vkDestroyInstance(
		instance->instance,
		allocator_callbacks(allocatorp, &callbacks));

	wine_dlclose(instance->libvulkan_handle, NULL, 0);
	allocator_free(allocatorp, instance->physicalDevices);
	allocator_free(allocatorp, instance);
}

VkResult WINAPI vkEnumerateDeviceExtensionProperties(
	VkPhysicalDevice       physicalDevice,
	const char            *pLayerName,
	uint32_t              *pPropertyCount,
	VkExtensionProperties *pProperties)
{
	TRACE("(%p, %s, %p, %p)\n", physicalDevice, pLayerName, pPropertyCount, pProperties);

	*pPropertyCount = 0;

	return VK_SUCCESS;
}

VkResult WINAPI vkEnumerateInstanceExtensionProperties(
	const char              *pLayerName,
	uint32_t                *pPropertyCount,
	VkExtensionProperties   *pProperties)
{
	TRACE("(%s, %p, %p)\n", pLayerName, pPropertyCount, pProperties);

	*pPropertyCount = 0;

	return VK_SUCCESS;
}

VkResult WINAPI vkEnumeratePhysicalDevices(
	VkInstance        instance,
	uint32_t         *pPhysicalDeviceCount,
	VkPhysicalDevice *pPhysicalDevices)
{
	uint32_t i;

	TRACE("(%p, %p, %p)\n", instance, pPhysicalDeviceCount, pPhysicalDevices);

	if (instance->physicalDevices == NULL)
	{
		VkResult result = instance->pfn.vkEnumeratePhysicalDevices(
			instance->instance,
			&instance->physicalDeviceCount,
			NULL);

		if (result != VK_SUCCESS)
			return result;

		if (instance->physicalDeviceCount > 0)
		{
			uint32_t i;

			VkPhysicalDevice *physicalDevices = allocator_allocate(
				instance->pAllocator,
				sizeof(*physicalDevices) * instance->physicalDeviceCount,
				8, /* alignof(*physicalDevices) */
				VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

			if (physicalDevices == NULL)
				return VK_ERROR_OUT_OF_HOST_MEMORY;

			instance->physicalDevices = allocator_allocate(
				instance->pAllocator,
				sizeof(*instance->physicalDevices) * instance->physicalDeviceCount,
				8, /* alignof(*instance->physicalDevices) */
				VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

			if (instance->physicalDevices == NULL)
			{
				allocator_free(instance->pAllocator, physicalDevices);

				return VK_ERROR_OUT_OF_HOST_MEMORY;
			}

			result = instance->pfn.vkEnumeratePhysicalDevices(
				instance->instance,
				&instance->physicalDeviceCount,
				physicalDevices);

			if (result != VK_SUCCESS)
			{
				allocator_free(instance->pAllocator, instance->physicalDevices);
				allocator_free(instance->pAllocator, physicalDevices);
				instance->physicalDevices = NULL;

				return result;
			}

			for (i = 0; i < instance->physicalDeviceCount; ++i)
			{
				VkPhysicalDevice dev = &instance->physicalDevices[i];

				set_loader_magic_value(&dev->loader_data);
				dev->instance = instance;
				dev->physicalDevice = physicalDevices[i];
			}

			allocator_free(instance->pAllocator, physicalDevices);
		}
	}

	if (pPhysicalDevices == NULL)
	{
		*pPhysicalDeviceCount = instance->physicalDeviceCount;

		return VK_SUCCESS;
	}

	*pPhysicalDeviceCount = min(*pPhysicalDeviceCount, instance->physicalDeviceCount);

	for (i = 0; i < *pPhysicalDeviceCount; ++i)
	{
		pPhysicalDevices[i] = &instance->physicalDevices[i];
	}

	return VK_SUCCESS;
}

PFN_vkVoidFunction WINAPI vkGetDeviceProcAddr(
	VkDevice    device,
	const char *pName)
{
	vulkan_function func;
	const vulkan_function *func_ret;

	TRACE("(%p, %s)\n", device, pName);

	func.pName = pName;
	func.pfn = NULL;

	func_ret = bsearch(
		&func,
		vulkan_device_functions,
		vulkan_device_function_count,
		sizeof(vulkan_device_functions[0]),
		compar);

	return func_ret ? func_ret->pfn : NULL;
}

void WINAPI vkGetPhysicalDeviceFeatures(
	VkPhysicalDevice          physicalDevice,
	VkPhysicalDeviceFeatures *pFeatures)
{
	TRACE("(%p, %p)\n", physicalDevice, pFeatures);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceFeatures(
		physicalDevice->physicalDevice,
		pFeatures);
}

void WINAPI vkGetPhysicalDeviceFormatProperties(
	VkPhysicalDevice    physicalDevice,
	VkFormat            format,
	VkFormatProperties *pFormatProperties)
{
	TRACE("(%p, %d, %p)\n", physicalDevice, format, pFormatProperties);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceFormatProperties(
		physicalDevice->physicalDevice,
		format,
		pFormatProperties);
}

VkResult WINAPI vkGetPhysicalDeviceImageFormatProperties(
	VkPhysicalDevice         physicalDevice,
	VkFormat                 format,
	VkImageType              type,
	VkImageTiling            tiling,
	VkImageUsageFlags        usage,
	VkImageCreateFlags       flags,
	VkImageFormatProperties *pImageFormatProperties)
{
	TRACE("(%p, %d, %d, %d, 0x%04x, 0x%04x, %p)\n", physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);

	return physicalDevice->instance->pfn.vkGetPhysicalDeviceImageFormatProperties(
		physicalDevice->physicalDevice,
		format,
		type,
		tiling,
		usage,
		flags,
		pImageFormatProperties);
}

void WINAPI vkGetPhysicalDeviceMemoryProperties(
	VkPhysicalDevice                  physicalDevice,
	VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
	TRACE("(%p, %p)\n", physicalDevice, pMemoryProperties);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceMemoryProperties(
		physicalDevice->physicalDevice,
		pMemoryProperties);
}

void WINAPI vkGetPhysicalDeviceProperties(
	VkPhysicalDevice            physicalDevice,
	VkPhysicalDeviceProperties *pProperties)
{
	TRACE("(%p, %p)\n", physicalDevice, pProperties);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceProperties(
		physicalDevice->physicalDevice,
		pProperties);
}

void WINAPI vkGetPhysicalDeviceQueueFamilyProperties(
	VkPhysicalDevice         physicalDevice,
	uint32_t                *pQueueFamilyPropertyCount,
	VkQueueFamilyProperties *pQueueFamilyProperties)
{
	TRACE("(%p, %p, %p)\n", physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice->physicalDevice,
		pQueueFamilyPropertyCount,
		pQueueFamilyProperties);
}

void WINAPI vkGetPhysicalDeviceSparseImageFormatProperties(
	VkPhysicalDevice               physicalDevice,
	VkFormat                       format,
	VkImageType                    type,
	VkSampleCountFlagBits          samples,
	VkImageUsageFlags              usage,
	VkImageTiling                  tiling,
	uint32_t                      *pPropertyCount,
	VkSparseImageFormatProperties *pProperties)
{
	TRACE("(%p, %d, %d, 0x%04x, 0x%04x, %d, %p, %p)\n", physicalDevice, format, type, samples, usage, tiling, pPropertyCount, pProperties);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceSparseImageFormatProperties(
		physicalDevice->physicalDevice,
		format,
		type,
		samples,
		usage,
		tiling,
		pPropertyCount,
		pProperties);
}

VkResult WINAPI vkCreateDevice(
	VkPhysicalDevice             physicalDevice,
	const VkDeviceCreateInfo    *pCreateInfo,
	const VkAllocationCallbacks *pAllocator,
	VkDevice                    *pDevice)
{
	VkDevice device;
	VkResult result;
	struct allocator allocator;
	VkAllocationCallbacks callbacks;
	uint32_t maxQueueFamilyIndex = 0;
	uint32_t queueCount = 0;
	uint32_t i;
	uint32_t j;

	TRACE("(%p, %p, %p, %p)\n", physicalDevice, pCreateInfo, pAllocator, pDevice);

	if (pCreateInfo->enabledLayerCount > 0)
		return VK_ERROR_LAYER_NOT_PRESENT;

	if (pCreateInfo->enabledExtensionCount > 0)
		return VK_ERROR_EXTENSION_NOT_PRESENT;

	for (i = 0; i < pCreateInfo->queueCreateInfoCount; ++i)
	{
		maxQueueFamilyIndex = max(
			maxQueueFamilyIndex,
			pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex);

		queueCount += pCreateInfo->pQueueCreateInfos[i].queueCount;
	}

	device = allocator_allocate(
		allocator_initialize(pAllocator, physicalDevice->instance->pAllocator, &allocator),
		sizeof(*device),
		8, /* alignof(*device) */
		VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

	if (device == NULL)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	set_loader_magic_value(&device->loader_data);
	device->instance = physicalDevice->instance;
	device->pAllocator = allocator_initialize(
		pAllocator, physicalDevice->instance->pAllocator, &device->allocator);

	allocator_store_initialize(&device->commandPoolAllocators);

	device->queueFamilyCount = maxQueueFamilyIndex + 1;
	device->queueFamilies = allocator_allocate(
		device->pAllocator,
		sizeof(*device->queueFamilies) * device->queueFamilyCount,
		8, /* alignof(*device->queueFamilies) */
		VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

	if (device->queueFamilies == NULL)
	{
		allocator_free(device->pAllocator, device);

		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	memset(device->queueFamilies, 0, sizeof(*device->queueFamilies) * maxQueueFamilyIndex);

	for (i = 0; i < pCreateInfo->queueCreateInfoCount; ++i)
	{
		VkDeviceQueueCreateInfo createInfo = pCreateInfo->pQueueCreateInfos[i];
		VkQueue *queues = &device->queueFamilies[createInfo.queueFamilyIndex];

		*queues = allocator_allocate(
			device->pAllocator,
			sizeof(**queues) * createInfo.queueCount,
			8, /* alignof(*queues) */
			VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

		if (*queues == NULL)
		{
			for (i = 0; i < device->queueFamilyCount; ++i)
			{
				allocator_free(device->pAllocator, device->queueFamilies[i]);
			}

			allocator_free(device->pAllocator, device);

			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
	}

	result = physicalDevice->instance->pfn.vkCreateDevice(
		physicalDevice->physicalDevice,
		pCreateInfo,
		allocator_callbacks(device->pAllocator, &callbacks),
		&device->device);

	if (result != VK_SUCCESS)
	{
		for (i = 0; i < device->queueFamilyCount; ++i)
		{
			allocator_free(device->pAllocator, device->queueFamilies[i]);
		}

		allocator_free(device->pAllocator, device->queueFamilies);
		allocator_free(device->pAllocator, device);

		return result;
	}

	load_device_pfn(
		device->device,
		(PFN_vkGetDeviceProcAddr)device->instance->pfn.vkGetDeviceProcAddr(
			device->device, "vkGetDeviceProcAddr"),
		&device->pfn);

	for (i = 0; i < pCreateInfo->queueCreateInfoCount; ++i)
	{
		VkDeviceQueueCreateInfo createInfo = pCreateInfo->pQueueCreateInfos[i];
		VkQueue queues = device->queueFamilies[createInfo.queueFamilyIndex];

		for (j = 0; j < createInfo.queueCount; ++j)
		{
			set_loader_magic_value(&queues[j].loader_data);
			queues[j].device = device;

			device->pfn.vkGetDeviceQueue(
				device->device,
				createInfo.queueFamilyIndex,
				j,
				&queues[j].queue);
		}
	}

	*pDevice = device;

	return VK_SUCCESS;
}

void WINAPI vkDestroyDevice(
	VkDevice                     device,
	const VkAllocationCallbacks *pAllocator)
{
	struct allocator *allocatorp;
	struct allocator allocator;
	VkAllocationCallbacks callbacks;
	uint32_t i;

	TRACE("(%p, %p)\n", device, pAllocator);

	allocatorp = allocator_initialize(pAllocator, device->pAllocator, &allocator);

	for (i = 0; i < device->queueFamilyCount; ++i)
	{
		allocator_free(allocatorp, device->queueFamilies[i]);
	}

	device->pfn.vkDestroyDevice(
		device->device,
		allocator_callbacks(allocatorp, &callbacks));

	allocator_free(allocatorp, device->queueFamilies);
	allocator_free(allocatorp, device);
}

VkResult WINAPI vkCreateCommandPool(
	VkDevice                       device,
	const VkCommandPoolCreateInfo *pCreateInfo,
	const VkAllocationCallbacks   *pAllocator,
	VkCommandPool                 *pCommandPool)
{
	struct allocator *allocatorp;
	struct allocator allocator;
	VkAllocationCallbacks callbacks;
	VkResult result;

	TRACE("(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pCommandPool);

	allocatorp = allocator_initialize(pAllocator, device->pAllocator, &allocator);

	result = device->pfn.vkCreateCommandPool(
		device->device,
		pCreateInfo,
		allocator_callbacks(allocatorp, &callbacks),
		pCommandPool);

	if (result == VK_SUCCESS)
	{
		bool success = allocator_store_add(
			&device->commandPoolAllocators,
			(uint64_t)*pCommandPool,
			allocatorp,
			VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

		if (!success)
		{
			device->pfn.vkDestroyCommandPool(
				device->device,
				*pCommandPool,
				allocator_callbacks(allocatorp, &callbacks));

			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
	}

	return result;
}

void WINAPI vkDestroyCommandPool(
	VkDevice                     device,
	VkCommandPool                commandPool,
	const VkAllocationCallbacks* pAllocator)
{
	struct allocator *allocatorp;
	struct allocator allocator;
	VkAllocationCallbacks callbacks;

	TRACE("(%p, "DBGDYNF", %p)\n", device, DBGDYNV(commandPool), pAllocator);

	allocatorp = allocator_initialize(pAllocator, device->pAllocator, &allocator);

	device->pfn.vkDestroyCommandPool(
		device->device,
		commandPool,
		allocator_callbacks(allocatorp, &callbacks));
}

VkResult WINAPI vkCreateDescriptorPool(
	VkDevice                          device,
	const VkDescriptorPoolCreateInfo *pCreateInfo,
	const VkAllocationCallbacks      *pAllocator,
	VkDescriptorPool                 *pDescriptorPool)
{
	TRACE("(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pDescriptorPool);

	return device->pfn.vkCreateDescriptorPool(device->device, pCreateInfo, NULL, pDescriptorPool);
}

void WINAPI vkDestroyDescriptorPool(
	VkDevice                     device,
	VkDescriptorPool             descriptorPool,
	const VkAllocationCallbacks *pAllocator)
{
	TRACE("(%p, "DBGDYNF", %p)\n", device, DBGDYNV(descriptorPool), pAllocator);

	return device->pfn.vkDestroyDescriptorPool(device->device, descriptorPool, NULL);
}

VkResult WINAPI vkCreatePipelineCache(
	VkDevice                         device,
	const VkPipelineCacheCreateInfo *pCreateInfo,
	const VkAllocationCallbacks     *pAllocator,
	VkPipelineCache                 *pPipelineCache)
{
	TRACE("(%p, %p, %p, %p)\n", device, pCreateInfo, pAllocator, pPipelineCache);

	return device->pfn.vkCreatePipelineCache(device->device, pCreateInfo, NULL, pPipelineCache);
}

void WINAPI vkDestroyPipelineCache(
	VkDevice                     device,
	VkPipelineCache              pipelineCache,
	const VkAllocationCallbacks *pAllocator)
{
	TRACE("(%p, "DBGDYNF", %p)\n", device, DBGDYNV(pipelineCache), pAllocator);

	return device->pfn.vkDestroyPipelineCache(device->device, pipelineCache, NULL);
}

VkResult WINAPI vkAllocateCommandBuffers(
	VkDevice                           device,
	const VkCommandBufferAllocateInfo *pAllocateInfo,
	VkCommandBuffer                   *pCommandBuffers)
{
	VkCommandBuffer *commandBuffers;
	VkResult result;
	uint32_t i;

	TRACE("(%p, %p, %p)\n", device, pAllocateInfo, pCommandBuffers);

	commandBuffers = allocator_allocate(
		device->pAllocator,
		sizeof(*commandBuffers) * pAllocateInfo->commandBufferCount,
		8, /* alignof(*commandBuffers) */
		VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

	if (commandBuffers == NULL)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	for (i = 0; i < pAllocateInfo->commandBufferCount; ++i)
	{
		commandBuffers[i] = allocator_allocate(
			device->pAllocator,
			sizeof(*commandBuffers[0]),
			8, /* alignof(*commandBuffers[i]) */
			VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

		if (commandBuffers[i] == NULL)
		{
			while (i > 0)
			{
				allocator_free(device->pAllocator, commandBuffers[--i]);
			}

			allocator_free(device->pAllocator, commandBuffers);

			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
	}

	result = device->pfn.vkAllocateCommandBuffers(
		device->device,
		pAllocateInfo,
		pCommandBuffers);

	if (result != VK_SUCCESS)
	{
		for (i = 0; i < pAllocateInfo->commandBufferCount; ++i)
		{
			allocator_free(device->pAllocator, commandBuffers[i]);
		}

		allocator_free(device->pAllocator, commandBuffers);

		return result;
	}

	for (i = 0; i < pAllocateInfo->commandBufferCount; ++i)
	{
		VkCommandBuffer commandBuffer = pCommandBuffers[i];
		pCommandBuffers[i] = commandBuffers[i];

		set_loader_magic_value(&pCommandBuffers[i]->loader_data);
		pCommandBuffers[i]->device = device;
		pCommandBuffers[i]->commandBuffer = commandBuffer;
	}

	allocator_free(device->pAllocator, commandBuffers);

	return result;
}

void WINAPI vkFreeCommandBuffers(
	VkDevice               device,
	VkCommandPool          commandPool,
	uint32_t               commandBufferCount,
	const VkCommandBuffer *pCommandBuffers)
{
	TRACE("(%p, "DBGDYNF", %u, %p)\n", device, DBGDYNV(commandPool), commandBufferCount, pCommandBuffers);

	while (commandBufferCount > 0)
	{
		VkCommandBuffer commandBuffer = pCommandBuffers[--commandBufferCount];

		device->pfn.vkFreeCommandBuffers(
			device->device,
			commandPool,
			1,
			&commandBuffer->commandBuffer);

		allocator_free(device->pAllocator, commandBuffer);
	}
}

void WINAPI vkCmdExecuteCommands(
	VkCommandBuffer        commandBuffer,
	uint32_t               commandBufferCount,
	const VkCommandBuffer *pCommandBuffers)
{
	#define COMMAND_BUFFER_BATCH_SIZE 16

	TRACE("(%p, %u, %p)\n", commandBuffer, commandBufferCount, pCommandBuffers);

	while (commandBufferCount >= COMMAND_BUFFER_BATCH_SIZE)
	{
		uint32_t i;
		VkCommandBuffer commandBuffers[COMMAND_BUFFER_BATCH_SIZE];

		for (i = 0; i < min(COMMAND_BUFFER_BATCH_SIZE, commandBufferCount); ++i)
		{
			commandBuffers[i] = pCommandBuffers[i]->commandBuffer;
		}

		commandBuffer->device->pfn.vkCmdExecuteCommands(
			commandBuffer->commandBuffer,
			min(COMMAND_BUFFER_BATCH_SIZE, commandBufferCount),
			commandBuffers);

		commandBufferCount -= COMMAND_BUFFER_BATCH_SIZE;
		pCommandBuffers += COMMAND_BUFFER_BATCH_SIZE;
	}
}

void WINAPI vkGetDeviceQueue(
	VkDevice  device,
	uint32_t  queueFamilyIndex,
	uint32_t  queueIndex,
	VkQueue  *pQueue)
{
	TRACE("(%p, %u, %u, %p)\n", device, queueFamilyIndex, queueIndex, pQueue);

	*pQueue = &device->queueFamilies[queueFamilyIndex][queueIndex];
}

VkResult WINAPI vkQueueSubmit(
	VkQueue             queue,
	uint32_t            submitCount,
	const VkSubmitInfo *pSubmits,
	VkFence             fence)
{
	VkSubmitInfo *submits;
	VkCommandBuffer *commandBuffers;
	VkCommandBuffer *commandBufferIter;
	VkResult result;
	uint32_t commandBufferCount = 0;
	uint32_t i;
	uint32_t j;

	TRACE("(%p, %u, %p, "DBGDYNF")\n", queue, submitCount, pSubmits, DBGDYNV(fence));

	for (i = 0; i < submitCount; ++i)
	{
		commandBufferCount += pSubmits[i].commandBufferCount;
	}

	if (commandBufferCount == 0)
	{
		return queue->device->pfn.vkQueueSubmit(
			queue->queue,
			submitCount,
			pSubmits,
			fence);
	}

	submits = allocator_allocate(
		queue->device->instance->pAllocator,
		sizeof(*submits) * submitCount,
		8, /* alignof(*submits) */
		VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

	if (submits == NULL)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	commandBuffers = allocator_allocate(
		queue->device->instance->pAllocator,
		sizeof(*commandBuffers) * commandBufferCount,
		8, /* alignof(*commandBuffers) */
	   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

	if (commandBuffers == NULL)
	{
		allocator_free(queue->device->instance->pAllocator, submits);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	memcpy(submits, pSubmits, sizeof(*submits) * submitCount);
	commandBufferIter = commandBuffers;

	for (i = 0; i < submitCount; ++i)
	{
		for (j = 0; j < submits[i].commandBufferCount; ++j)
		{
			commandBufferIter[j] = submits[i].pCommandBuffers[j]->commandBuffer;
		}

		submits[i].pCommandBuffers = commandBufferIter;
		commandBufferIter += submits[i].commandBufferCount;
	}

	result = queue->device->pfn.vkQueueSubmit(
		queue->queue,
		submitCount,
		submits,
		fence);

	allocator_free(queue->device->instance->pAllocator, commandBuffers);
	allocator_free(queue->device->instance->pAllocator, submits);

	return result;
}

static const vulkan_function vulkan_instance_functions[] = {
	{ "vkCreateDevice", vkCreateDevice },
	{ "vkCreateInstance", vkCreateInstance },
	{ "vkDestroyInstance", vkDestroyInstance },
	{ "vkEnumerateDeviceExtensionProperties", vkEnumerateDeviceExtensionProperties },
	{ "vkEnumerateInstanceExtensionProperties", vkEnumerateInstanceExtensionProperties },
	{ "vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices },
	{ "vkGetDeviceProcAddr", vkGetDeviceProcAddr },
	{ "vkGetPhysicalDeviceFeatures", vkGetPhysicalDeviceFeatures },
	{ "vkGetPhysicalDeviceFormatProperties", vkGetPhysicalDeviceFormatProperties },
	{ "vkGetPhysicalDeviceImageFormatProperties", vkGetPhysicalDeviceImageFormatProperties },
	{ "vkGetPhysicalDeviceMemoryProperties", vkGetPhysicalDeviceMemoryProperties },
	{ "vkGetPhysicalDeviceProperties", vkGetPhysicalDeviceProperties },
	{ "vkGetPhysicalDeviceQueueFamilyProperties", vkGetPhysicalDeviceQueueFamilyProperties },
	{ "vkGetPhysicalDeviceSparseImageFormatProperties", vkGetPhysicalDeviceSparseImageFormatProperties },
};

static const size_t vulkan_instance_function_count =
	sizeof(vulkan_instance_functions) / sizeof(vulkan_instance_functions[0]);

static vulkan_function vulkan_device_functions[] = {
	{ "vkAllocateCommandBuffers", vkAllocateCommandBuffers },
	{ "vkCmdExecuteCommands", vkCmdExecuteCommands },
	{ "vkCreateCommandPool", vkCreateCommandPool },
	{ "vkDestroyCommandPool", vkDestroyCommandPool },
	{ "vkDestroyDevice", vkDestroyDevice },
	{ "vkFreeCommandBuffers", vkFreeCommandBuffers },
	{ "vkGetDeviceProcAddr", vkGetDeviceProcAddr },
	{ "vkGetDeviceQueue", vkGetDeviceQueue },
	{ "vkQueueSubmit", vkQueueSubmit },
};

static size_t vulkan_device_function_count =
	sizeof(vulkan_device_functions) / sizeof(vulkan_device_functions[0]);

PFN_vkVoidFunction WINAPI vk_icdGetInstanceProcAddr(
	VkInstance  instance,
	const char *pName)
{
	vulkan_function func;
	const vulkan_function *func_ret;

	TRACE("(%p, %s)\n", instance, pName);

	func.pName = pName;
	func.pfn = NULL;

	func_ret = bsearch(
		&func,
		vulkan_instance_functions,
		vulkan_instance_function_count,
		sizeof(vulkan_instance_functions[0]),
		compar);

	return func_ret ? func_ret->pfn : NULL;
}
