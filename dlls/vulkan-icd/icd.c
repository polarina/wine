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

	GET(vkDestroyInstance);
	GET(vkEnumeratePhysicalDevices);
	GET(vkGetPhysicalDeviceFeatures);
	GET(vkGetPhysicalDeviceProperties);

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

void WINAPI vkGetPhysicalDeviceFeatures(
	VkPhysicalDevice          physicalDevice,
	VkPhysicalDeviceFeatures *pFeatures)
{
	TRACE("(%p, %p)\n", physicalDevice, pFeatures);

	physicalDevice->instance->pfn.vkGetPhysicalDeviceFeatures(
		physicalDevice->physicalDevice,
		pFeatures);
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

static const vulkan_function vulkan_instance_functions[] = {
	{ "vkCreateInstance", vkCreateInstance },
	{ "vkDestroyInstance", vkDestroyInstance },
	{ "vkEnumerateInstanceExtensionProperties", vkEnumerateInstanceExtensionProperties },
	{ "vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices },
	{ "vkGetPhysicalDeviceFeatures", vkGetPhysicalDeviceFeatures },
	{ "vkGetPhysicalDeviceProperties", vkGetPhysicalDeviceProperties },
};

static const size_t vulkan_instance_function_count =
	sizeof(vulkan_instance_functions) / sizeof(vulkan_instance_functions[0]);

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
