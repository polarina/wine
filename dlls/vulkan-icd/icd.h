#ifndef __DLLS_VULKAN_ICD_ICD_H
#define __DLLS_VULKAN_ICD_ICD_H

#include <stddef.h>
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include "vk_icd.h"
#include "vulkan.h"

#include "allocator.h"

typedef struct {
	const char *pName;
	void *pfn;
} vulkan_function;

#endif
