#ifndef __DLLS_VULKAN_ICD_ALLOCATOR_H
#define __DLLS_VULKAN_ICD_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wine/rbtree.h"

#define VK_NO_PROTOTYPES
#include "vulkan.h"

struct allocator {
	void *pUserData;
	void *pInternalUserData;

	PFN_vkAllocationFunction pfnAllocation;
	PFN_vkReallocationFunction pfnReallocation;
	PFN_vkFreeFunction pfnFree;

	PFN_vkInternalAllocationNotification pfnInternalAllocation;
	PFN_vkInternalFreeNotification pfnInternalFree;
};

struct allocator_store {
	struct wine_rb_tree tree;
};

struct allocator *allocator_initialize(
	const VkAllocationCallbacks *pAllocator,
	const struct allocator      *parent,
	struct allocator            *allocator);

VkAllocationCallbacks *allocator_callbacks(
	const struct allocator *allocator,
	VkAllocationCallbacks  *pAllocator);

void *allocator_allocate(
	const struct allocator  *allocator,
	size_t                   size,
	size_t                   alignment,
	VkSystemAllocationScope  allocationScope);

void allocator_free(
	const struct allocator *allocator,
	void                   *pMemory);

void allocator_store_initialize(
	struct allocator_store  *allocatorStore);

bool allocator_store_add(
	struct allocator_store   *allocatorStore,
	uint64_t                  handle,
	struct allocator         *allocator,
	VkSystemAllocationScope  scope);

void allocator_store_remove(
	struct allocator_store *allocatorStore,
	uint64_t                handle);

#endif
