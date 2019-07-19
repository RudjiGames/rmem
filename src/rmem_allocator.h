/*
 * Copyright (c) 2019 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RMEM_ALLOCATOR_H
#define RMEM_ALLOCATOR_H

namespace rmem {

//--------------------------------------------------------------------------
/// Allocator interface
//--------------------------------------------------------------------------
struct Allocator
{
	virtual int		init() = 0;
	virtual void	shutDown() = 0;
	virtual void	initThread() = 0;
	virtual void	shutDownThread() = 0;
	virtual void*	malloc(size_t _size) = 0;
	virtual void	free(void* _ptr) = 0;
	virtual void*	realloc(void* _ptr, size_t _size) = 0;

	static Allocator*	getAllocator(int _allocator);
};

} // namespace rmem

#endif // RMEM_ALLOCATOR_H
