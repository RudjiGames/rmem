/*
 * Copyright (c) 2019 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "rmem_allocator.h"
#include "../inc/rmem.h"

#include <new>	// placement new
#include "rpmalloc/rpmalloc.h"

#define RMEM_ALLOCATOR_DECLARE(_name)	static char buff_##_name[sizeof(_name)]
#define RMEM_ALLOCATOR_CONSTRUCT(_name)	new (buff_##_name) _name
#define RMEM_ALLOCATOR_GETPTR(_name)	(_name*)buff_##_name

namespace rmem {

#if RMEM_PLATFORM_WINDOWS
struct allocator_rpmalloc : public Allocator
{
	int		init()								{ int ret = rpmalloc_initialize(); return ret; }
	void	shutDown()							{ rpmalloc_finalize(); }
	void	initThread()						{ rpmalloc_thread_initialize(); }
	void	shutDownThread()					{ rpmalloc_thread_finalize(); }
	void*	malloc(size_t _size)				{ return rpmalloc(_size); }
	void	free(void* _ptr)					{ rpfree(_ptr); }
	void*	realloc(void* _ptr, size_t _size)	{ return rprealloc(_ptr, _size); }
};
#endif // RMEM_PLATFORM_WINDOWS

Allocator* Allocator::getAllocator(int _allocator)
{
	RMEM_ALLOCATOR_DECLARE(allocator_rpmalloc);
	RMEM_ALLOCATOR_CONSTRUCT(allocator_rpmalloc);

	switch (_allocator & ~RMEM_ALLOCATOR_NOPROFILING)
	{
#if RMEM_PLATFORM_WINDOWS
	case RMEM_ALLOCATOR_RPMALLOC:	return RMEM_ALLOCATOR_GETPTR(allocator_rpmalloc);
#endif

	default:
		return 0;
	};
}

} // namespace rmem
