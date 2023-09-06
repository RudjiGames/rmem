/*
 * Copyright 2023 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#define _CRT_SECURE_NO_WARNINGS 1

#include "rmem_platform.h"

#if RMEM_PLATFORM_XBOXONE

#include <xmem.h>

static inline uint32_t getOverhead(size_t /*_size*/)
{
	return 8;
}

PVOID WINAPI detour_XMEMALLOC_ROUTINE(SIZE_T dwSize, ULONGLONG dwAttributes)
{
	XALLOC_ATTRIBUTES att;
	att.dwAttributes = dwAttributes;
	PVOID ptr = XMemAllocDefault(dwSize, dwAttributes);
	rmemAlloc(att.s.dwAllocatorId, ptr, (uint32_t)dwSize, getOverhead(dwSize));
	return ptr;
}

VOID WINAPI detour_XMEMFREE_ROUTINE(PVOID lpAddress, ULONGLONG dwAttributes)
{
	XALLOC_ATTRIBUTES att;
	att.dwAttributes = dwAttributes;
	rmemFree(att.s.dwAllocatorId, lpAddress);
	XMemFreeDefault(lpAddress, dwAttributes);
}

extern "C"
{
	void rmemUnhookAllocs();

	void rmemHookAllocs(int _isLinkerBased, int _allocatorID)
	{
		XMemSetAllocationHooks(	&detour_XMEMALLOC_ROUTINE,
								&detour_XMEMFREE_ROUTINE );

		if (!_isLinkerBased)
			atexit(rmemUnhookAllocs);

		rmemInit(0);
	}

	void rmemUnhookAllocs()
	{
		XMemSetAllocationHooks(0, 0);
		rmemShutDown();
	}
};

#endif // RMEM_PLATFORM_XBOXONE
