////////////////////////////////////////////////////////////////////////////////
// pz_stomp_allocator.h - Stomp allocator - Copyright(c) 2016 Pablo Zurita
////////////////////////////////////////////////////////////////////////////////
//
// MIT License
// 
// Copyright(c) 2016 Pablo Zurita
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
////////////////////////////////////////////////////////////////////////////////
//
// This is a very basic allocator used to detect memory stomps, basically
// invalid operations with memory. The supported cases are:
//
//   * Memory overruns - Reading or writing off the end of an allocation.
//   * Memory underrun - Reading or writing off the beginning of an allocation.
//   * Use after free - Reading or writing an allocation that was already freed.
//
// USAGE
// 
// The API contains just 3 functions:
//   * pz_stomp_allocator_alloc() - Allocates memory with a given alignement.
//   * pz_stomp_allocator_free() - Frees a memory allocation.
//   * pz_stomp_allocator_alloc() - Changes the size of an allocation.
// For more details see the comment on each function.
//
// To use this allocator you must set the correct following defines based on
// your target build, platform, and expected behavior:
//   * PZ_STOMP_DETECT_UNDERRUNS - Detect underruns instead of overruns.
//   * PZ_PLATFORM_32BITS - 32-bit builds.
//   * PZ_PLATFORM_64BITS - 64-bit builds.
//   * PZ_PLATFORM_WINDOWS - Windows target platform using Visual Studio.
//   * PZ_PLATFORM_LINUX - Linux target platform. Probably works for Android.
//   * PZ_PLATFORM_MAC - Linux target platform. Probably works for iOS.
//
// Once you have done that then all allocations that you track need to happen
// through the allocator. If you have a proper allocation layer then attach the
// allocator to it, if not then you probably want to override the new and delete
// operators and put breakpoints on malloc(), realloc() and free() to see what
// you are missing.
//
////////////////////////////////////////////////////////////////////////////////

#ifndef PZ_STOMP_ALLOCATOR_H
#define PZ_STOMP_ALLOCATOR_H

#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64) || defined(__ppc64__) || defined(_WIN64) || defined(__LP64__) || defined(_LP64) )
#define PZ_PLATFORM_64BITS
#else
#define PZ_PLATFORM_32BITS
#endif 

#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
#define PZ_PLATFORM_WINDOWS
#elif defined(__linux__) || defined(linux)
#define PZ_PLATFORM_LINUX
#elif defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#define PZ_PLATFORM_MAC
#else
#error 
#endif

static int PZ_STOMP_DETECT_UNDERRUNS = 0;

#if !defined(PZ_PLATFORM_64BITS) && !defined(PZ_PLATFORM_32BITS)
	#error You must define PZ_PLATFORM_64BITS or PZ_PLATFORM_32BITS to based on type of build
#endif // !PZ_PLATFORM_64BITS && !PZ_PLATFORM_32BITS

#if !defined(PZ_PLATFORM_WINDOWS) && !defined(PZ_PLATFORM_LINUX) && !defined(PZ_PLATFORM_MAC)
	#error You must define PZ_PLATFORM_WINDOWS or PZ_PLATFORM_LINUX or PZ_PLATFORM_MAC based on the target platform
#endif // !PZ_PLATFORM_WINDOWS && !PZ_PLATFORM_LINUX && !PZ_PLATFORM_MAC

#if defined(PZ_PLATFORM_WINDOWS)
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h> // Required for VirtualAlloc, VirtualFree, VirtualProtect.

	#if defined(_MSC_VER)
		#define PZ_COMPILER_MSVC
	#endif // _MSC_VER
#elif defined(PZ_PLATFORM_LINUX) || defined(PZ_PLATFORM_MAC)
	#include <sys/mman.h> // Required for mmap, munmap, mprotect.
	#if defined(PZ_PLATFORM_LINUX)
		#include <signal.h> // Required for raise.
	#endif // PZ_PLATFORM_LINUX
#endif // PZ_PLATFORM_WINDOWS

////////////////////////////////////////////////////////////////////////////////
// Internal functions and structures. Shouldn't need to touch them unless you
// are adding support for a new platform.

static void *pz_internal_stomp_allocator_virtual_alloc(const size_t size)
{
#if defined(PZ_PLATFORM_LINUX) || defined(PZ_PLATFORM_MAC)
	return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, (off_t)0);
#elif defined(PZ_PLATFORM_WINDOWS)
	return VirtualAlloc(NULL, (SIZE_T)(size), (DWORD)MEM_COMMIT, (DWORD)PAGE_READWRITE);
#else
	#error pz_stomp_allocator does not support this platform.
#endif // PZ_PLATFORM_LINUX || PZ_PLATFORM_MAC
}

static void pz_internal_stomp_allocator_virtual_free(void * const ptr, const size_t size)
{
#if defined(PZ_PLATFORM_LINUX) || defined(PZ_PLATFORM_MAC)
	munmap(ptr, size);
#elif defined(PZ_PLATFORM_WINDOWS)
	size;
	VirtualFree(ptr, (SIZE_T)0U, (DWORD)MEM_RELEASE);
#else
	#error pz_stomp_allocator does not support this platform.
#endif // PZ_PLATFORM_LINUX || PZ_PLATFORM_MAC
}

static void pz_internal_stomp_allocator_virtual_protect(void * const ptr, const size_t size)
{
#if defined(PZ_PLATFORM_LINUX) || defined(PZ_PLATFORM_MAC)
	mprotect(ptr, size, PROT_NONE);
#elif defined(PZ_PLATFORM_WINDOWS)
	DWORD flOldProtect;
	VirtualProtect(ptr, (SIZE_T)size, (DWORD)PAGE_NOACCESS, &flOldProtect);
#else
	#error pz_stomp_allocator does not support this platform.
#endif // PZ_PLATFORM_LINUX || PZ_PLATFORM_MAC
}

static void pz_internal_stomp_allocator_debug_break()
{
#if defined(PZ_PLATFORM_WINDOWS)
	__debugbreak();
#elif defined(PZ_PLATFORM_MAC)
	__asm__("int $3");
#elif defined(PZ_PLATFORM_LINUX)
	raise(SIGTRAP);
#else
	#error pz_stomp_allocator does not support this platform.
#endif // PZ_PLATFORM_WINDOWS
}

#if defined(PZ_PLATFORM_64BITS)
/** Expected value to be found in the sentinel. */
static const size_t scSentinelExpectedValue = 0xdeadbeefdeadbeef;
#elif defined(PZ_PLATFORM_32BITS)
/** Expected value to be found in the sentinel. */
static const size_t scSentinelExpectedValue = 0xdeadbeef;
#else
#error pz_stomp_allocator does not support this platform.
#endif

struct pz_internal_stomp_allocator_per_allocation_data
{
	/** Pointer to the full allocation. Needed so the OS knows what to free. */
	void	*mFullAllocationPointer;
	/** Full size of the allocation including the extra page. Only needed in some platforms. */
	size_t	mFullSize;
	/** Size of the allocation requested. */
	size_t	mSize;
	/** Sentinel used to check for underruns. */
	size_t	mSentinel;
};

/** Size of each page. */
static const size_t scPageSize = 4096U;

////////////////////////////////////////////////////////////////////////////////
// Public API.

/**
 * Allocates a block of a given number of bytes of memory with the required
 * alignment. In the process it allocates as many pages as necessary plus one
 * that will be protected making it unaccessible and causing an exception. The
 * actual allocation will be pushed to the end of the last valid unprotected
 * page. To deal with underrun errors a sentinel is added right before the
 * allocation in page which is checked on free.
 *
 * @param size Size in bytes of the memory block to allocate.
 * @param alignment Alignment in bytes of the memory block to allocate.
 * @return A pointer to the beginning of the memory block.
 */
static void *pz_stomp_allocator_alloc(const size_t size, const uint32_t alignment)
{
	if (size == 0U)
		return NULL;

	const size_t alignedSize = (alignment > 0U) ? ((size + alignment - 1U) & -((int32_t)alignment)) : size;
	const size_t allocFullPageSize = alignedSize + sizeof(struct pz_internal_stomp_allocator_per_allocation_data) + (scPageSize - 1) & ~(scPageSize - 1U);

	void * const fullAllocationPointer = pz_internal_stomp_allocator_virtual_alloc(allocFullPageSize + scPageSize);

	void *returnedPointer = NULL;
	static const size_t allocationDataSize = sizeof(struct pz_internal_stomp_allocator_per_allocation_data);

	if (PZ_STOMP_DETECT_UNDERRUNS)
	{
		const size_t alignedAllocationData = (alignment > 0U) ? ((allocationDataSize + alignment - 1U) & -((int32_t)alignment)) : allocationDataSize;
		returnedPointer = (void*)((uint8_t*)fullAllocationPointer + scPageSize + alignedAllocationData);

		struct pz_internal_stomp_allocator_per_allocation_data * const allocDataPtr = (struct pz_internal_stomp_allocator_per_allocation_data*)((uint8_t*)fullAllocationPointer + scPageSize);
#if defined(PZ_COMPILER_MSVC)
		// "nonstandard extension used : non-constant aggregate initializer"
		// This is valid C99.
		#pragma warning(push)
		#pragma warning(disable : 4204)
#endif // PZ_COMPILER_MSVC
		const struct pz_internal_stomp_allocator_per_allocation_data allocData = { fullAllocationPointer, allocFullPageSize + scPageSize, alignedSize, scSentinelExpectedValue };
#if defined(PZ_COMPILER_MSVC)
		#pragma warning(pop)
#endif // PZ_COMPILER_MSVC

		*allocDataPtr = allocData;

		// Page protect the first page, this will cause the exception in case
		// there is an underrun.
		pz_internal_stomp_allocator_virtual_protect(fullAllocationPointer, scPageSize);
	}
	else
	{
		returnedPointer = (void*)((uint8_t*)fullAllocationPointer + allocFullPageSize - alignedSize);

		struct pz_internal_stomp_allocator_per_allocation_data * const allocDataPtr = (struct pz_internal_stomp_allocator_per_allocation_data*)((uint8_t*)returnedPointer - allocationDataSize);
#if defined(PZ_COMPILER_MSVC)
		// "nonstandard extension used : non-constant aggregate initializer"
		// This is valid C99.
		#pragma warning(push)
		#pragma warning(disable : 4204)
#endif // PZ_COMPILER_MSVC
		const struct pz_internal_stomp_allocator_per_allocation_data allocData = { fullAllocationPointer, allocFullPageSize + scPageSize, alignedSize, scSentinelExpectedValue };
#if defined(PZ_COMPILER_MSVC)
		#pragma warning(pop)
#endif // PZ_COMPILER_MSVC
		*allocDataPtr = allocData;

		// Page protect the last page, this will cause the exception in case
		// there is an overrun.
		pz_internal_stomp_allocator_virtual_protect((void*)((uint8_t*)(fullAllocationPointer)+allocFullPageSize), scPageSize);
	}

	return returnedPointer;
}

/**
 * Frees a memory allocation and verifies the sentinel in the process.
 *
 * @param ptr Pointer of the data to free.
 */
static void pz_stomp_allocator_free(const void * const ptr)
{
	if (ptr == NULL)
	{
		return;
	}

	const struct pz_internal_stomp_allocator_per_allocation_data * allocDataPtr = (const struct pz_internal_stomp_allocator_per_allocation_data *)ptr;
	allocDataPtr--;

	// Check that our sentinel is intact.
	if (allocDataPtr->mSentinel != scSentinelExpectedValue)
	{
		// There was a memory underrun related to this allocation.
		pz_internal_stomp_allocator_debug_break();
	}

	pz_internal_stomp_allocator_virtual_free(allocDataPtr->mFullAllocationPointer, allocDataPtr->mFullSize);
}

/**
 * Changes the size of the memory block pointed to by ptr. This function will
 * move the memory block to a new location.
 *
 * NOTE: This function follows the MSVC realloc convention of freeing memory
 * when newSize is 0. That is not the same of POSIX or C11.
 *
 * @param ptr Pointer to a memory block previously allocated with Malloc.
 * @param newSize New size in bytes for the memory block.
 * @param alignment Alignment in bytes for the reallocation.
 * @return A pointer to the reallocated memory block.
 */
static void* pz_stomp_allocator_realloc(const void * const ptr, const size_t newSize, const uint32_t alignment)
{
	if (newSize == 0U)
	{
		pz_stomp_allocator_free(ptr);
		return NULL;
	}

	void * const returnPtr = pz_stomp_allocator_alloc(newSize, alignment);

	if (ptr != NULL)
	{
		const struct pz_internal_stomp_allocator_per_allocation_data * const allocDataPtr = (const struct pz_internal_stomp_allocator_per_allocation_data * const)((const uint8_t * const)(ptr)-sizeof(struct pz_internal_stomp_allocator_per_allocation_data));
		memcpy(returnPtr, ptr, (allocDataPtr->mSize < newSize) ? allocDataPtr->mSize : newSize);
		pz_stomp_allocator_free(ptr);
	}

	return returnPtr;
}


/**
 * Frees a memory allocation and verifies the sentinel in the process.
 *
 * @param ptr Pointer of the data to free.
 */
static size_t pz_stomp_allocator_msize(const void * const ptr)
{
	if (ptr == NULL)
	{
		return 0;
	}

	const struct pz_internal_stomp_allocator_per_allocation_data * allocDataPtr = (const struct pz_internal_stomp_allocator_per_allocation_data *)ptr;
	allocDataPtr--;

	// Check that our sentinel is intact.
	if (allocDataPtr->mSentinel != scSentinelExpectedValue)
	{
		// There was a memory underrun related to this allocation.
		pz_internal_stomp_allocator_debug_break();
	}

	return allocDataPtr->mFullSize;
}

#endif // PZ_STOMP_ALLOCATOR_H
