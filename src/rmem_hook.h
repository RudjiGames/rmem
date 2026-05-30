//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RMEM_HOOK_H
#define RMEM_HOOK_H

#include "../inc/rmem.h"
#include "rmem_config.h"
#include "rmem_utils.h"
#include "rmem_mutex.h"

#include <stdio.h>	//< file ops

// File writes (and LZ4 compression, when enabled) can stall the profiled application. When
// enabled, they are performed on a dedicated background thread using the second half of the
// double buffer, so recording keeps going while the previous half is written. Platforms
// without a supported thread / condition-variable implementation fall back to synchronous
// writes (and can be forced off by defining RMEM_ENABLE_ASYNC_WRITE=0 in the build).
#ifndef RMEM_ENABLE_ASYNC_WRITE
	#if RMEM_PLATFORM_WINDOWS || RMEM_PLATFORM_LINUX || RMEM_PLATFORM_OSX || RMEM_PLATFORM_ANDROID
		#define RMEM_ENABLE_ASYNC_WRITE				1
	#else
		#define RMEM_ENABLE_ASYNC_WRITE				0
	#endif
#endif

namespace rmem {

	/// Memory hook interface
	class MemoryHook
	{
	public:
		enum { OpBufferSize = 96 + (RMEM_STACK_TRACE_MAX * sizeof(uintptr_t)) };
		enum { BufferSize = RMEM_BUFFER_SIZE };

	private:
		// Set true only during construction (after the header write, around the module-info
		// gathering that itself allocates), then cleared - never written once hooks are live.
		// volatile keeps the hot-path read from being hoisted across the re-entrant
		// writeModuleInfo() call on the same thread.
		volatile bool	m_ignoreAllocs;
		uint8_t*	m_excessBufferPtr;
		size_t		m_bufferBytesWritten;
		uint8_t*	m_bufferPtr;
		uint8_t		m_bufferData[BufferSize * 3];
#if RMEM_ENABLE_LZ4_COMPRESSION
		// Sized to the LZ4 worst-case compression bound (LZ4_COMPRESSBOUND) so that
		// compressing a full, incompressible BufferSize block can never fail.
		uint8_t		m_bufferCompressed[BufferSize + (BufferSize / 255) + 16];
#endif // RMEM_ENABLE_LZ4_COMPRESSION
		Mutex		m_mutexInternalBufferPtrs;
		Mutex		m_mutexWriteToFile;
#if RMEM_PLATFORM_WINDOWS
		wchar_t		m_fileName[512];
#else
		char		m_fileName[256];
#endif // RMEM_PLATFORM_WINDOWS
		FILE*		m_file;
		size_t		m_excessBufferSize;
		int64_t		m_startTime;

		enum Enum
		{
			HashArraySize	= RMEM_STACK_TRACE_HASH_TABLE_SIZE,
			HashArrayMask	= HashArraySize - 1
		};
		uint32_t	m_stackTraceHashes[MemoryHook::HashArraySize];

		// Secondary 64-bit hash per slot, used to confirm a 32-bit slot-hash match is
		// not a collision. Replaces a per-slot copy of all RMEM_STACK_TRACE_MAX frames
		// (which alone made this injected DLL ~1.5 GB) - 8 bytes/slot instead of 384.
		uint64_t	m_stackTraceHash2[MemoryHook::HashArraySize];

#if RMEM_ENABLE_ASYNC_WRITE
		// Background writer: the producer posts a filled buffer half and keeps recording into
		// the other half while this thread compresses/writes the posted one. Only one write is
		// outstanding at a time (there are two halves), so the producer blocks here only when
		// I/O falls behind. See writerPost / writerWaitIdle / writerLoop.
		void*		m_writeJobPtr;		// buffer half pending write (null = writer idle)
		size_t		m_writeJobSize;
		volatile uint64_t m_writerThreadId;	// writer's OS thread id (Windows guard; published by writerStart before the thread runs, 0 until then)
		bool		m_writerStop;
		bool		m_writerStarted;
	#if RMEM_PLATFORM_WINDOWS
		CRITICAL_SECTION	m_writerLock;
		CONDITION_VARIABLE	m_writerJobCv;
		CONDITION_VARIABLE	m_writerDoneCv;
		HANDLE				m_writerThread;
	#else
		pthread_mutex_t		m_writerLock;
		pthread_cond_t		m_writerJobCv;
		pthread_cond_t		m_writerDoneCv;
		pthread_t			m_writerThread;
	#endif
#endif // RMEM_ENABLE_ASYNC_WRITE

	public:
		MemoryHook(const char* _rootPathOverride);
		~MemoryHook();

		/// Called on shut down to flush any queued data
		void flush();

		/// Called for each memory tag instantiation
		void registerTag(const char* _name, const char* _parentName);

		/// Called for each start of memory tag scope
		void enterTag(RMemTag& _tag);

		/// Called for each end of memory tag scope
		void leaveTag( RMemTag& _tag);

		/// Called for each memory marker instantiation
		void registerMarker(RMemMarker& _marker);

		/// Called for each memory marker occurance
		void marker(RMemMarker& _marker);

		/// Called for each heap registration
		void registerAllocator(const char* _name, uint64_t _handle);

		/// Called for each allocation
		void alloc(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead);

		/// Called for each reallocation
		void realloc(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead, void* _prevPtr);

		/// Called for each aligned allocation
		void allocAligned(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead, uint32_t _alignment);

		/// Called for each aligned reallocation
		void reallocAligned(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead, void* _prevPtr, uint32_t _alignment);

		/// Called for each free
		void free(uint64_t _handle, void* _ptr);

		/// Called for each loaded module
		void registerModule(const char* _name, uint64_t _base, uint32_t _size);

		/// Called for each loaded module
		void registerModule(const wchar_t* _name, uint64_t _base, uint32_t _size);

		/// Called for each unloaded module
		void unregisterModule(const char* _name, uint64_t _base, uint32_t _size);

		/// Called for each unloaded module
		void unregisterModule(const wchar_t* _name, uint64_t _base, uint32_t _size);

	private:
		/// Writes out a full stack trace (carrying its 32-bit hash, v1.4 format)
		void addStackTrace_new(uint8_t* _tmpBuffer, size_t& _tmpBuffPtr, uintptr_t* _stackTrace, uint32_t _numFrames, uint32_t _stackHash32);

		/// Called on each memory operation
		void addStackTrace(uint8_t* _tmpBuffer, size_t& _tmpBuffPtr, uintptr_t* _stackTrace, uint32_t _numTraces, uint64_t _stackHash);

		/// Writes data to the internal buffer
		void writeToBuffer(void* _ptr, size_t _size, uintptr_t* _stackTrace = 0, uint32_t _numFrames = 0);
		
		/// Writes data to file, used internally by writeToBuffer
		void writeToFile(void* _ptr, size_t _bytesToWrite);

		/// Dump additional debug info to help resolving symbols
		void writeModuleInfo();

		/// swap buffers
		uint8_t* doubleBuffer();

#if RMEM_ENABLE_ASYNC_WRITE
		/// background writer thread: lifecycle and producer-side handoff
		void writerStart();
		void writerStop();
		void writerPost(void* _ptr, size_t _size);	// hand a filled buffer half to the writer
		void writerWaitIdle();						// block until no write is outstanding
		void writerLoop();							// writer thread body
	#if RMEM_PLATFORM_WINDOWS
		static DWORD WINAPI writerThreadEntry(LPVOID _arg);
	#else
		static void* writerThreadEntry(void* _arg);
	#endif
#endif // RMEM_ENABLE_ASYNC_WRITE
	};

} // namespace rmem

#endif // RMEM_HOOK_H
