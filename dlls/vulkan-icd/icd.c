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

WINE_DEFAULT_DEBUG_CHANNEL(vulkan_icd);

static int compar(const void *elt_a, const void *elt_b)
{
	return strcmp(
		((const vulkan_function *) elt_a)->pName,
		((const vulkan_function *) elt_b)->pName);
}

static const vulkan_function vulkan_instance_functions[] = {
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
