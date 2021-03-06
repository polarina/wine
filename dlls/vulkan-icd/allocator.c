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

#include "allocator.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define VK_NO_PROTOTYPES
#include "vk_icd.h"
#include "vulkan.h"

#include "windef.h"

typedef void* (WINAPI *WPFN_vkAllocationFunction)(
	void                    *pUserData,
	size_t                   size,
	size_t                   alignment,
	VkSystemAllocationScope  allocationScope);

typedef void* (WINAPI *WPFN_vkReallocationFunction)(
	void                    *pUserData,
	void                    *pOriginal,
	size_t                   size,
	size_t                   alignment,
	VkSystemAllocationScope  allocationScope);

typedef void (WINAPI *WPFN_vkFreeFunction)(
	void *pUserData,
	void *pMemory);

typedef void (WINAPI *WPFN_vkInternalAllocationNotification)(
	void                     *pUserData,
	size_t                    size,
	VkInternalAllocationType  allocationType,
	VkSystemAllocationScope   allocationScope);

typedef void (WINAPI *WPFN_vkInternalFreeNotification)(
	void                     *pUserData,
	size_t                    size,
	VkInternalAllocationType  allocationType,
	VkSystemAllocationScope   allocationScope);

struct allocator_store_entry {
	struct wine_rb_entry entry;
	uint64_t handle;
	struct allocator *pAllocator;
	struct allocator allocator;
	uint32_t referenceCount;
};

static int rbtree_compare(const void *key, const struct wine_rb_entry *entry)
{
	uint64_t handle = *(uint64_t *)key;
	const struct allocator_store_entry *allocator =
		(const struct allocator_store_entry *)entry;

	if (handle == allocator->handle)
		return 0;

	if (handle < allocator->handle)
		return -1;

	return 1;
}

static const struct wine_rb_functions rbtree_functions = {
	NULL,
	NULL,
	NULL,
	&rbtree_compare
};

static void *allocator_callback_vkAllocationFunction(
	void                    *pUserData,
	size_t                   size,
	size_t                   alignment,
	VkSystemAllocationScope  allocationScope)
{
	const struct allocator *allocator = pUserData;

	WPFN_vkAllocationFunction vkAllocationFunction =
		(WPFN_vkAllocationFunction)allocator->pfnAllocation;

	return vkAllocationFunction(
		allocator->pUserData, size, alignment, allocationScope);
}

static void *allocator_callback_vkReallocationFunction(
	void                    *pUserData,
	void                    *pOriginal,
	size_t                   size,
	size_t                   alignment,
	VkSystemAllocationScope  allocationScope)
{
	const struct allocator *allocator = pUserData;

	WPFN_vkReallocationFunction vkReallocationFunction =
		(WPFN_vkReallocationFunction)allocator->pfnReallocation;

	return vkReallocationFunction(
		allocator->pUserData, pOriginal, size, alignment, allocationScope);
}

static void allocator_callback_vkFreeFunction(
	void *pUserData,
	void *pMemory)
{
	const struct allocator *allocator = pUserData;

	WPFN_vkFreeFunction vkFreeFunction =
		(WPFN_vkFreeFunction)allocator->pfnFree;

	vkFreeFunction(allocator->pUserData, pMemory);
}

static void allocator_callback_vkInternalAllocationNotification(
	void                     *pUserData,
	size_t                    size,
	VkInternalAllocationType  allocationType,
	VkSystemAllocationScope   allocationScope)
{
	const struct allocator *allocator = pUserData;

	WPFN_vkInternalAllocationNotification vkInternalAllocationNotification =
		(WPFN_vkInternalAllocationNotification)allocator->pfnInternalAllocation;

	vkInternalAllocationNotification(
		allocator->pUserData, size, allocationType, allocationScope);
}

static void allocator_callback_vkInternalFreeNotification(
	void                     *pUserData,
	size_t                    size,
	VkInternalAllocationType  allocationType,
	VkSystemAllocationScope   allocationScope)
{
	const struct allocator *allocator = pUserData;

	WPFN_vkInternalFreeNotification vkInternalFreeNotification =
		(WPFN_vkInternalFreeNotification)allocator->pfnInternalFree;

	vkInternalFreeNotification(
		allocator->pUserData, size, allocationType, allocationScope);
}

struct allocator *allocator_initialize(
	const VkAllocationCallbacks *pAllocator,
	const struct allocator      *parent,
	struct allocator            *allocator)
{
	if (pAllocator == NULL)
	{
		if (parent == NULL)
			return NULL;

		*allocator = *parent;
	}
	else
	{
		allocator->pUserData = pAllocator->pUserData;
		allocator->pfnAllocation = pAllocator->pfnAllocation;
		allocator->pfnReallocation = pAllocator->pfnReallocation;
		allocator->pfnFree = pAllocator->pfnFree;

		if (pAllocator->pfnInternalAllocation != NULL)
		{
			allocator->pInternalUserData = pAllocator->pUserData;
			allocator->pfnInternalAllocation = pAllocator->pfnInternalAllocation;
			allocator->pfnInternalFree = pAllocator->pfnInternalFree;
		}
		else if (parent != NULL)
		{
			allocator->pInternalUserData = parent->pInternalUserData;
			allocator->pfnInternalAllocation = parent->pfnInternalAllocation;
			allocator->pfnInternalFree = parent->pfnInternalFree;
		}
		else
		{
			allocator->pInternalUserData = NULL;
			allocator->pfnInternalAllocation = NULL;
			allocator->pfnInternalFree = NULL;
		}
	}

	return allocator;
}

VkAllocationCallbacks *allocator_callbacks(
	const struct allocator *allocator,
	VkAllocationCallbacks  *pAllocator)
{
	if (allocator == NULL)
		return NULL;

	pAllocator->pUserData = (void *)allocator;
	pAllocator->pfnAllocation = allocator_callback_vkAllocationFunction;
	pAllocator->pfnReallocation = allocator_callback_vkReallocationFunction;
	pAllocator->pfnFree = allocator_callback_vkFreeFunction;

	if (allocator->pfnInternalAllocation == NULL)
	{
		pAllocator->pfnInternalAllocation = NULL;
		pAllocator->pfnInternalFree = NULL;
	}
	else
	{
		pAllocator->pfnInternalAllocation = allocator_callback_vkInternalAllocationNotification;
		pAllocator->pfnInternalFree = allocator_callback_vkInternalFreeNotification;
	}

	return pAllocator;
}

void *allocator_allocate(
	const struct allocator  *allocator,
	size_t                   size,
	size_t                   alignment,
	VkSystemAllocationScope  allocationScope)
{
	void *pMemory;

	if (allocator == NULL)
	{
		if (size == 0)
			return NULL;

		/* allocator_allocate with NULL for allocator is only called by us,
		   where the alignment guarantees of malloc suffice */
		pMemory = malloc(size);
	}
	else
	{
		WPFN_vkAllocationFunction vkAllocationFunction =
			(WPFN_vkAllocationFunction)allocator->pfnAllocation;

		pMemory = vkAllocationFunction(
			allocator->pUserData,
			size,
			alignment,
			allocationScope);
	}

	return pMemory;
}

void allocator_free(
	const struct allocator *allocator,
	void                   *pMemory)
{
	if (allocator == NULL)
	{
		free(pMemory);
	}
	else
	{
		WPFN_vkFreeFunction vkFreeFunction =
			(WPFN_vkFreeFunction)allocator->pfnFree;

		vkFreeFunction(allocator->pUserData, pMemory);
	}
}

void allocator_store_initialize(
	struct allocator_store  *allocatorStore)
{
	wine_rb_init(&allocatorStore->tree, &rbtree_functions);
}

bool allocator_store_add(
	struct allocator_store  *allocatorStore,
	uint64_t                 handle,
	struct allocator        *allocator,
	VkSystemAllocationScope  scope)
{
	struct wine_rb_entry *entry = wine_rb_get(&allocatorStore->tree, &handle);
	struct allocator_store_entry *store;

	if (entry == NULL)
	{
		store = allocator_allocate(
			allocator,
			sizeof(*store),
			8, /* alignof(*store) */
			scope);

		if (store == NULL)
			return false;

		store->handle = handle;
		store->pAllocator = allocator;
		store->referenceCount = 1;

		if (allocator != NULL)
		{
			store->allocator = *allocator;
		}

		wine_rb_put(&allocatorStore->tree, &handle, (struct wine_rb_entry *)store);
	}
	else
	{
		store = (struct allocator_store_entry *)entry;
		++store->referenceCount;
	}

	return true;
}

void allocator_store_remove(
	struct allocator_store *allocatorStore,
	uint64_t                handle)
{
	struct wine_rb_entry *entry = wine_rb_get(&allocatorStore->tree, &handle);
	struct allocator_store_entry *store = (struct allocator_store_entry *)entry;

	if (--store->referenceCount == 0)
	{
		allocator_free(store->pAllocator, store);
	}
}
