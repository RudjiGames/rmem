//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include "rmem_platform.h"
#include "rmem_hook.h"
#include "rmem_enums.h"

#include "../3rd/lz4/lz4.h"
#if RMEM_ENABLE_LZ4_COMPRESSION
#if !RMEM_LZ4_NO_DEFINE
#if RMEM_COMPILER_MSVC
#pragma warning (push)
#pragma warning (disable:4996) // This function is deprecated and unsafe
#endif
#if RMEM_COMPILER_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "../3rd/lz4/lz4.c"
#if RMEM_COMPILER_CLANG
#pragma clang diagnostic pop
#endif
#if RMEM_COMPILER_MSVC
#pragma warning (pop)
#endif
#endif // !RMEM_LZ4_NO_DEFINE
#endif // RMEM_ENABLE_LZ4_COMPRESSION

#if RMEM_PLATFORM_WINDOWS
typedef HRESULT (WINAPI *fnSHGetFolderPathW)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);
#define CSIDL_APPDATA                   0x001a        // <user name>\Application Data
#define CSIDL_FLAG_CREATE               0x8000        // combine with CSIDL_ value to force folder creation in SHGetFolderPath()
#endif

#include <stdlib.h>
#include <time.h>	//< time for composing output file name

template <typename CHAR>
static inline CHAR* printNumReverse(CHAR* _dst, int _num)
{
	if (_num == 0)
	{
		*_dst-- = CHAR('0');
	}
	while (_num)
	{
		*_dst-- = CHAR('0') + _num % 10;
		_num /= 10;
	}
	*_dst-- = CHAR('.');
	return _dst;
}

template <typename CHAR>
static CHAR* sGetTimeString(CHAR _buff[256])
{
#if RMEM_PLATFORM_WINDOWS || RMEM_PLATFORM_XBOX360 || RMEM_PLATFORM_XBOXONE
	SYSTEMTIME stime;
	GetLocalTime(&stime);
	_buff[255] = CHAR('\0');
	CHAR* nextdest = printNumReverse(&_buff[254], stime.wMilliseconds);
	nextdest = printNumReverse(nextdest, stime.wSecond);
	nextdest = printNumReverse(nextdest, stime.wMinute);
	nextdest = printNumReverse(nextdest, stime.wHour);
	nextdest = printNumReverse(nextdest, stime.wDay);
	nextdest = printNumReverse(nextdest, stime.wMonth);
	nextdest = printNumReverse(nextdest, stime.wYear);
	return nextdest+1;
#else
	time_t rawtime;
	time( &rawtime );
	struct tm* timeinfo;
	timeinfo = localtime( &rawtime );
	_buff[255] = CHAR('\0');
	CHAR* nextdest = printNumReverse(&_buff[254], timeinfo->tm_sec);
	nextdest = printNumReverse(nextdest, timeinfo->tm_min);
	nextdest = printNumReverse(nextdest, timeinfo->tm_hour);
	nextdest = printNumReverse(nextdest, timeinfo->tm_mday);
	nextdest = printNumReverse(nextdest, timeinfo->tm_mon+1);
	nextdest = printNumReverse(nextdest, timeinfo->tm_year+1900);
	return nextdest+1;
#endif
}

namespace rmem {

/// Once the buffer is full we will write it to the file.
/// The problem is that writing to a file will likely perform more
/// allocations which will, in turn, call this function again thus
/// we need to guarantee that the internal buffer is writable.
/// Since the buffer is full we need to copy the data so we can
/// reset the buffer pointer and practically make the buffer empty again.
/// The code assumes that buffer is big enough to store all information 
/// resulting from file writing calls. Generally that's not a problem as
/// it already has to be big enough to store the CRT initialization data.
/// Since the buffer is static, it is also assumed that this is single
/// threaded operation which is guaranteed by a lock on a higher level 
/// (dispatcher).
static uint8_t s_tempBuffer[MemoryHook::BufferSize];

//--------------------------------------------------------------------------
/// Constructor
//--------------------------------------------------------------------------
MemoryHook::MemoryHook(const char* _rootPathOverride)
	: m_ignoreAllocs(false)
{
	m_startTime = getCPUClock();
	m_bufferPtr	= m_bufferData;

	for (uint32_t i=0; i<HashArraySize; ++i)
	{
		m_stackTraceHashes[i]	= 0;
		m_stackTraceHash2[i]	= 0;
	}

	m_file					= nullptr;
	m_bufferBytesWritten	= 0;
	m_excessBufferPtr		= nullptr;
	m_excessBufferSize		= 0;

#if RMEM_ENABLE_ASYNC_WRITE
	// Initialized before the first writeToBuffer (header below); the writer thread itself is
	// started at the end of the constructor. Until then writerPost/writerWaitIdle no-op.
	m_writeJobPtr			= 0;
	m_writeJobSize			= 0;
	m_writerThreadId		= 0;
	m_writerStop			= false;
	m_writerStarted			= false;
#endif

#if RMEM_LITTLE_ENDIAN
	uint8_t endianess		= 0x00;
#else
	uint8_t endianess		= 0xff;
#endif

	uint8_t pointerSize		= (sizeof(void*) == 4) ? 32 : 64;
	uint8_t verHigh			= RMEM_VER_HIGH;
	uint8_t verLow			= RMEM_VER_LOW;
	uint8_t toolChain		= ToolChain::Unknown;

#if RMEM_PLATFORM_WINDOWS
	#if RMEM_COMPILER_MSVC
		toolChain	= ToolChain::Win_MSVC;
	#elif RMEM_COMPILER_GCC
		toolChain	= ToolChain::Win_gcc;
	#endif

#elif RMEM_PLATFORM_LINUX
	#if RMEM_COMPILER_CLANG
		toolChain	= ToolChain::Linux_clang;
	#elif RMEM_COMPILER_GCC
		toolChain	= ToolChain::Linux_gcc;
	#endif

#elif RMEM_PLATFORM_OSX
	#if RMEM_COMPILER_GCC
		toolChain	= ToolChain::OSX_gcc;
	#else
		toolChain	= ToolChain::OSX_clang;
	#endif

#elif RMEM_PLATFORM_PS3
	#if RMEM_COMPILER_GCC
		toolChain	= ToolChain::PS3_gcc;
	#else
		toolChain	= ToolChain::PS3_snc;
	#endif

#elif RMEM_PLATFORM_PS4
	toolChain		= ToolChain::PS4_clang;

#elif RMEM_PLATFORM_PS5
	toolChain		= ToolChain::PS5_clang;

#elif RMEM_PLATFORM_ANDROID

	#if	RMEM_CPU_ARM
		toolChain	= ToolChain::Android_arm;
	#elif RMEM_CPU_MIPS
		toolChain	= ToolChain::Android_mips;
	#elif RMEM_CPU_X86
		toolChain	= ToolChain::Android_x86;
	#else
		#error
	#endif

#elif RMEM_PLATFORM_XBOX360
	toolChain		= ToolChain::Xbox_360;

#elif RMEM_PLATFORM_XBOXONE
	toolChain		= ToolChain::Xbox_One;

#elif RMEM_PLATFORM_SWITCH
	toolChain		= ToolChain::Nintendo_Switch;

#else
	#error "Unsupported platform!"
#endif

	int64_t cpuFrequency = getCPUFrequency();

	writeToBuffer(&endianess, sizeof(endianess));
	writeToBuffer(&pointerSize, sizeof(pointerSize));
	writeToBuffer(&verHigh, sizeof(verHigh));
	writeToBuffer(&verLow, sizeof(verLow));
	writeToBuffer(&toolChain, sizeof(toolChain));
	writeToBuffer(&cpuFrequency, sizeof(cpuFrequency));
		
	//----------- Header data written, below code can allocate memory ---------
	m_ignoreAllocs = true;

	writeModuleInfo();

#if RMEM_PLATFORM_WINDOWS
	wchar_t secBuff[256];
	wchar_t* timeString = sGetTimeString(secBuff);

	if (_rootPathOverride)
	{
		size_t len = strlen(_rootPathOverride) + 1; // include nil
		for (size_t s=0; s<len; ++s)
		{
			m_fileName[s] = _rootPathOverride[s];
		}
	}
	else
	{
		m_fileName[0] = 0;
		HMODULE shelldll32 = ::LoadLibraryA("Shell32");
		if (shelldll32)
		{
			fnSHGetFolderPathW fn = reinterpret_cast<fnSHGetFolderPathW>(reinterpret_cast<void*>(::GetProcAddress(shelldll32, "SHGetFolderPathW")));
			if (fn)
			{
				if (SUCCEEDED(fn(nullptr, CSIDL_APPDATA|CSIDL_FLAG_CREATE, nullptr, 0, m_fileName)))
				{
					wcscat_s(m_fileName, 512, L"\\");
					::CreateDirectoryW(m_fileName, 0);
					wcscat_s(m_fileName, 512, L"MTuner\\");
					::CreateDirectoryW(m_fileName, 0);
				}
			}
			else
			{
				m_fileName[0] = 0;
			}

			FreeLibrary(shelldll32);
		}
		else
		{
			wcscpy_s(m_fileName, 512, L"");
		}
	}

	wchar_t currFile[512];
	GetModuleFileNameW(nullptr, currFile, 512);
	int len = (int)wcslen(currFile);
	while ((len > 0) && (currFile[len] != L'\\') && (currFile[len] != L'/'))
	{
		--len;
	}
	wchar_t* name = &currFile[len+1];
	wchar_t* endName = wcsstr(name, L".");
	if (endName)
	{
		*endName = L'\0';
	}
	wcscat_s(m_fileName, 512, name);
#else
	char secBuff[256];
	char* timeString = sGetTimeString(secBuff);

	if (_rootPathOverride)
		strcpy(m_fileName, _rootPathOverride);
	else
	{
		strcpy(m_fileName, "");

#if RMEM_PLATFORM_ANDROID
		strcpy(m_fileName, "/sdcard/");
#endif // RMEM_PLATFORM_ANDROID

#if RMEM_PLATFORM_PS3
		strcpy(m_fileName, "/app_home/");
#endif // RMEM_PLATFORM_PS3

#if RMEM_PLATFORM_PS4
		strcpy(m_fileName, "/hostapp/");
#endif // RMEM_PLATFORM_PS4

#if RMEM_PLATFORM_PS5
		strcpy(m_fileName, "/app0/");
#endif // RMEM_PLATFORM_PS5

#if RMEM_PLATFORM_XBOX360
		strcpy(m_fileName, "game:\\");
#endif // RMEM_PLATFORM_XBOX360
	}

	strcat(m_fileName, "mtuner_capture_");
#endif

#if RMEM_PLATFORM_WINDOWS
	wcscat_s(m_fileName, 512, timeString);
	wcscat_s(m_fileName, 512, L".MTuner");
#else
	strcat(m_fileName, timeString);
	strcat(m_fileName, ".MTuner");
#endif

	m_ignoreAllocs = false;

#if RMEM_ENABLE_ASYNC_WRITE
	// Start the background writer now that the buffers and output path are set up. Spawned via
	// rmemInit (not DllMain), so creating/joining the thread does not run under the loader lock.
	writerStart();
#endif
}

//--------------------------------------------------------------------------
/// Destructor
//--------------------------------------------------------------------------
MemoryHook::~MemoryHook()
{
	flush();
#if RMEM_ENABLE_ASYNC_WRITE
	writerStop();	// drain handled by flush() above; this joins and tears down the writer thread
#endif
}

/// Called on shut down to flush any queued data
void MemoryHook::flush()
{
	// Snapshot and reset the shared buffer state under the same lock that
	// writeToBuffer uses, so an in-flight hook on another thread cannot tear
	// the read of m_bufferBytesWritten / m_bufferPtr.
	m_mutexInternalBufferPtrs.lock();
	memcpy(s_tempBuffer, m_bufferPtr, m_bufferBytesWritten);
	size_t BytesToWrite = m_bufferBytesWritten;
	m_bufferBytesWritten = 0;
	m_mutexInternalBufferPtrs.unlock();

#if RMEM_ENABLE_ASYNC_WRITE
	// Let the writer finish any posted chunk first so this trailing data is written last,
	// preserving the on-disk order of operations.
	writerWaitIdle();
#endif

	writeToFile(s_tempBuffer, BytesToWrite);
	if (m_file)
	{
		fclose(m_file);
		m_file = nullptr;
	}
}

/// Called for each memory tag instantiation
void MemoryHook::registerTag(const char* _name, const char* _parentName)
{
	/// When we write an entry to a file we need to make sure entire
	/// data block is written in an 'atomic' way - writing all the data
	/// related to a single entry in a single go.
	/// Calling writeToBuffer directly can make an internal write buffer
	/// full and result in writing to a file which in turn could do more
	/// allocations - this would result in new data being written between
	/// writeToBuffer calls.
	uint8_t		tmpBuffer[512];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::RegisterTag;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_name, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_parentName, tmpBuffer, tmpBufferPtr);

	uint32_t nameHash = hashStr(_name);
	addVarToBuffer(nameHash, tmpBuffer, tmpBufferPtr);

	if (_parentName)
	{
		uint32_t parentHash = hashStr(_parentName);
		addVarToBuffer(parentHash, tmpBuffer, tmpBufferPtr);
	}
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

/// Called for each start of memory tag scope
void MemoryHook::enterTag(RMemTag& _tag)
{
	uint8_t		tmpBuffer[512];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::EnterTag;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_tag.m_HashedName, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

/// Called for each end of memory tag scope
void MemoryHook::leaveTag(RMemTag& _tag)
{
	uint8_t		tmpBuffer[512];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::LeaveTag;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_tag.m_HashedName, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

/// Called for each memory marker instantiation
void MemoryHook::registerMarker(RMemMarker& _marker)
{
	uint8_t		tmpBuffer[512];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::RegisterMarker;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_marker.m_name, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_marker.m_nameHash, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_marker.m_color, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

/// Called for each memory marker occurance
void MemoryHook::marker(RMemMarker& _marker)
{
	uint8_t		tmpBuffer[512];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::Marker;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_marker.m_nameHash, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);
	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

//--------------------------------------------------------------------------
/// Called for each allocation
//--------------------------------------------------------------------------
void MemoryHook::registerAllocator(const char* _name, uint64_t _handle)
{
	uint8_t		tmpBuffer[512];
	size_t		tmpBufferPtr = 0;
	
	uint8_t Marker = LogMarkers::Allocator;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_name, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_handle, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

#if RMEM_ENABLE_DELAYED_CAPTURE
extern "C" {
bool rmemIsCaptureEnabled(bool _enable = false);
};
#else
static inline bool rmemIsCaptureEnabled(bool) { return true; }
#endif

// The background writer thread may itself allocate (e.g. fopen opening the capture file); its
// own allocations must never be recorded or they recurse into writeToBuffer and deadlock against
// the producer/handoff. We identify the writer thread to suppress them.
//
// On Windows rmem is injected into an already-running process, so we must NOT use thread_local
// here: this guard runs on every hooked RtlAllocateHeap/RtlFreeHeap from threads that predate
// the DLL (and even during loader/TLS init), and accessing a freshly-loaded DLL's thread_local
// from such a thread crashes. GetCurrentThreadId() (via getThreadID) reads the TEB and is always
// safe and cheap. On preloaded platforms a thread_local bool is safe and avoids a per-allocation
// gettid syscall on the hot path.
#if RMEM_ENABLE_ASYNC_WRITE
	#if RMEM_PLATFORM_WINDOWS
		#define RMEM_WRITER_GUARD	if (m_writerThreadId && (getThreadID() == m_writerThreadId)) return;
	#else
		static thread_local bool s_rmemIsWriterThread = false;
		#define RMEM_WRITER_GUARD	if (s_rmemIsWriterThread) return;
	#endif
#else
	#define RMEM_WRITER_GUARD
#endif

#define RMEM_DELAYED_CAPTURE					\
	if (!rmemIsCaptureEnabled(false)) return;	\
	if (m_ignoreAllocs) return;					\
	RMEM_WRITER_GUARD

//--------------------------------------------------------------------------
/// Called for each allocation
//--------------------------------------------------------------------------
void MemoryHook::alloc(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead)
{
	RMEM_DELAYED_CAPTURE

	uint8_t		tmpBuffer[MemoryHook::OpBufferSize];
	size_t		tmpBufferPtr = 0;

	const uint8_t opMarker = LogMarkers::OpAlloc;
	addVarToBuffer(opMarker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_handle, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);

#if RMEM_32BIT
	addVarToBuffer((uint32_t)_ptr, tmpBuffer, tmpBufferPtr);
#else
	addVarToBuffer((uint64_t)_ptr, tmpBuffer, tmpBufferPtr);
#endif

	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_overhead, tmpBuffer, tmpBufferPtr);

	uintptr_t backTrace[RMEM_STACK_TRACE_MAX];
	uint32_t numTraces = getStackTrace(backTrace, RMEM_STACK_TRACE_MAX, 0);
	writeToBuffer(tmpBuffer, tmpBufferPtr, backTrace, numTraces);
}

//--------------------------------------------------------------------------
/// Called for each reallocation
//--------------------------------------------------------------------------
void MemoryHook::realloc(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead, void* _prevPtr)
{
	RMEM_DELAYED_CAPTURE

	uint8_t		tmpBuffer[MemoryHook::OpBufferSize];
	size_t		tmpBufferPtr = 0;

	const uint8_t opMarker = LogMarkers::OpRealloc;
	addVarToBuffer(opMarker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_handle, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);

#if RMEM_32BIT
	addVarToBuffer((uint32_t)_ptr, tmpBuffer, tmpBufferPtr);
	addVarToBuffer((uint32_t)_prevPtr, tmpBuffer, tmpBufferPtr);
#else
	addVarToBuffer((uint64_t)_ptr, tmpBuffer, tmpBufferPtr);
	addVarToBuffer((uint64_t)_prevPtr, tmpBuffer, tmpBufferPtr);
#endif

	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_overhead, tmpBuffer, tmpBufferPtr);

	uintptr_t backTrace[RMEM_STACK_TRACE_MAX];
	uint32_t numTraces = getStackTrace(backTrace, RMEM_STACK_TRACE_MAX, 0);
	writeToBuffer(tmpBuffer, tmpBufferPtr, backTrace, numTraces);
}

//--------------------------------------------------------------------------
/// Called for each aligned allocation
//--------------------------------------------------------------------------
void MemoryHook::allocAligned(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead, uint32_t _alignment)
{
	RMEM_DELAYED_CAPTURE

	uint8_t		tmpBuffer[MemoryHook::OpBufferSize];
	size_t		tmpBufferPtr = 0;

	const uint8_t opMarker = LogMarkers::OpAllocAligned;
	addVarToBuffer(opMarker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_handle, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);

#if RMEM_32BIT
	addVarToBuffer((uint32_t)_ptr, tmpBuffer, tmpBufferPtr);
#else
	addVarToBuffer((uint64_t)_ptr, tmpBuffer, tmpBufferPtr);
#endif

	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	uint32_t bitIndex = uint32_cnttzl(_alignment);
	addVarToBuffer((uint8_t)bitIndex, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_overhead, tmpBuffer, tmpBufferPtr);

	uintptr_t backTrace[RMEM_STACK_TRACE_MAX];
	uint32_t numTraces = getStackTrace(backTrace, RMEM_STACK_TRACE_MAX, 0);
	writeToBuffer(tmpBuffer, tmpBufferPtr, backTrace, numTraces);
}

//--------------------------------------------------------------------------
/// Called for each aligned reallocation
//--------------------------------------------------------------------------
void MemoryHook::reallocAligned(uint64_t _handle, void* _ptr, uint32_t _size, uint32_t _overhead, void* _prevPtr, uint32_t _alignment)
{
	RMEM_DELAYED_CAPTURE

	uint8_t		tmpBuffer[MemoryHook::OpBufferSize];
	size_t		tmpBufferPtr = 0;

	const uint8_t opMarker = LogMarkers::OpReallocAligned;
	addVarToBuffer(opMarker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_handle, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);

#if RMEM_32BIT
	addVarToBuffer((uint32_t)_ptr, tmpBuffer, tmpBufferPtr);
	addVarToBuffer((uint32_t)_prevPtr, tmpBuffer, tmpBufferPtr);
#else
	addVarToBuffer((uint64_t)_ptr, tmpBuffer, tmpBufferPtr);
	addVarToBuffer((uint64_t)_prevPtr, tmpBuffer, tmpBufferPtr);
#endif

	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	uint32_t bitIndex = uint32_cnttzl(_alignment);
	addVarToBuffer((uint8_t)bitIndex, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_overhead, tmpBuffer, tmpBufferPtr);

	uintptr_t backTrace[RMEM_STACK_TRACE_MAX];
	uint32_t numTraces = getStackTrace(backTrace, RMEM_STACK_TRACE_MAX, 0);
	writeToBuffer(tmpBuffer, tmpBufferPtr, backTrace, numTraces);
}

//--------------------------------------------------------------------------
/// Called for each free
//--------------------------------------------------------------------------
void MemoryHook::free(uint64_t _handle, void* _ptr)
{
	RMEM_DELAYED_CAPTURE

	uint8_t		tmpBuffer[MemoryHook::OpBufferSize];
	size_t		tmpBufferPtr = 0;

	const uint8_t opMarker = LogMarkers::OpFree;
	addVarToBuffer(opMarker, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_handle, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(getThreadID(), tmpBuffer, tmpBufferPtr);

#if RMEM_32BIT
	addVarToBuffer((uint32_t)_ptr, tmpBuffer, tmpBufferPtr);
#else
	addVarToBuffer((uint64_t)_ptr, tmpBuffer, tmpBufferPtr);
#endif

	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);

	uintptr_t backTrace[RMEM_STACK_TRACE_MAX];
	uint32_t numTraces = getStackTrace(backTrace, RMEM_STACK_TRACE_MAX, 0);
	writeToBuffer(tmpBuffer, tmpBufferPtr, backTrace, numTraces);
}

//--------------------------------------------------------------------------
/// Called for each loaded module
//--------------------------------------------------------------------------
void MemoryHook::registerModule(const char* _name, uint64_t _base, uint32_t _size)
{
	uint8_t		tmpBuffer[1024];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::Module;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	Marker = 1; // char
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_name, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_base, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

//--------------------------------------------------------------------------
/// Called for each loaded module
//--------------------------------------------------------------------------
void MemoryHook::registerModule(const wchar_t* _name, uint64_t _base, uint32_t _size)
{
	uint8_t		tmpBuffer[1024];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::Module;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	Marker = 2; // wchar_t
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_name, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_base, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

//--------------------------------------------------------------------------
/// Called for each unloaded module
//--------------------------------------------------------------------------
void MemoryHook::unregisterModule(const char* _name, uint64_t _base, uint32_t _size)
{
	uint8_t		tmpBuffer[1024];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::ModuleUnload;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	Marker = 1; // char
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_name, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_base, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

//--------------------------------------------------------------------------
/// Called for each unloaded module
//--------------------------------------------------------------------------
void MemoryHook::unregisterModule(const wchar_t* _name, uint64_t _base, uint32_t _size)
{
	uint8_t		tmpBuffer[1024];
	size_t		tmpBufferPtr = 0;

	uint8_t Marker = LogMarkers::ModuleUnload;
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	Marker = 2; // wchar_t
	addVarToBuffer(Marker, tmpBuffer, tmpBufferPtr);
	addStrToBuffer(_name, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_base, tmpBuffer, tmpBufferPtr);
	addVarToBuffer(_size, tmpBuffer, tmpBufferPtr);
	const int64_t clock = getCPUClock() - m_startTime;
	addVarToBuffer(clock, tmpBuffer, tmpBufferPtr);
	writeToBuffer(tmpBuffer, tmpBufferPtr);
}

//--------------------------------------------------------------------------
/// Writes out a full stack trace
//--------------------------------------------------------------------------
void MemoryHook::addStackTrace_new(uint8_t* _tmpBuffer, size_t& _tmpBuffPtr, uintptr_t* _stackTrace, uint32_t _numFrames)
{
	uint8_t hashTag = (uint8_t)EntryTags::Add;
	addVarToBuffer(hashTag, _tmpBuffer, _tmpBuffPtr);
	uint16_t numTraces = (uint16_t)_numFrames;
	addVarToBuffer(numTraces, _tmpBuffer, _tmpBuffPtr);
	addPtrToBuffer(_stackTrace, sizeof(uintptr_t)*_numFrames, _tmpBuffer, _tmpBuffPtr);
}

//--------------------------------------------------------------------------
/// Called on each memory operation
//--------------------------------------------------------------------------
void MemoryHook::addStackTrace(uint8_t* _tmpBuffer, size_t& _tmpBuffPtr, uintptr_t* _stackTrace, uint32_t _numFrames, uint64_t _stackHash)
{
#if	RMEM_STACK_TRACE_ENABLE_HASHING

	const uint32_t stackHash32 = (uint32_t)_stackHash;					// 32-bit hash is the wire/dedup key
	const uint32_t stackIndex  = stackHash32 & HashArrayMask;

	if (m_stackTraceHashes[stackIndex] == stackHash32)
	{
		/// Confirm the 32-bit slot match isn't a collision using the full 64-bit hash.
		if (m_stackTraceHash2[stackIndex] != _stackHash)
		{
			/// hash collision (same 32-bit hash, different stack) - emit the full stack
			addStackTrace_new(_tmpBuffer, _tmpBuffPtr, _stackTrace, _numFrames);
			return;
		}

		/// write the existing hash
		uint8_t hashTag = (uint8_t)EntryTags::Exists;
		addVarToBuffer(hashTag, _tmpBuffer, _tmpBuffPtr);
		addVarToBuffer(stackHash32, _tmpBuffer, _tmpBuffPtr);
	}
	else
	{
		if (m_stackTraceHashes[stackIndex] == 0)
		{
			m_stackTraceHashes[stackIndex]	= stackHash32;
			m_stackTraceHash2[stackIndex]	= _stackHash;
			/// write stack strace
			addStackTrace_new(_tmpBuffer, _tmpBuffPtr, _stackTrace, _numFrames);
		}
		else
		{
			/// different hash - write full stack
			addStackTrace_new(_tmpBuffer, _tmpBuffPtr, _stackTrace, _numFrames);
		}
	}

#else //RMEM_STACK_TRACE_ENABLE_HASHING
	addStackTrace_new(_tmpBuffer, _tmpBuffPtr, _stackTrace, _numFrames);
#endif // RMEM_STACK_TRACE_ENABLE_HASHING
}

//--------------------------------------------------------------------------
/// Writes data to an internal buffer
//--------------------------------------------------------------------------
void MemoryHook::writeToBuffer(void* _ptr, size_t _size, uintptr_t* _stackTrace, uint32_t _numFrames)
{
	uint64_t stackHash	= 0;

#if	RMEM_STACK_TRACE_ENABLE_HASHING
	if (_stackTrace)
	{
		stackHash = (uint64_t)hashStackTrace(_stackTrace, _numFrames);	// full 64-bit; addStackTrace uses the low 32 as the wire/dedup key
	}
#endif // RMEM_STACK_TRACE_ENABLE_HASHING

	m_mutexInternalBufferPtrs.lock();

	if (_stackTrace)
	{
		addStackTrace((uint8_t*)_ptr, _size, _stackTrace, _numFrames, stackHash);
	}

	uint8_t* writeBuffer		= 0;
	size_t   writeBufferBytes	= 0;

	if (_size + m_bufferBytesWritten <= MemoryHook::BufferSize)
	{
		memcpy(&m_bufferPtr[m_bufferBytesWritten], _ptr, _size);

		if (_size + m_bufferBytesWritten == MemoryHook::BufferSize)
		{
#if RMEM_ENABLE_ASYNC_WRITE
			// The other half may still be in flight on the writer - wait before reusing it.
			writerWaitIdle();
#endif
			writeBuffer			= doubleBuffer();
			writeBufferBytes	= MemoryHook::BufferSize;	// record ends exactly on the boundary - chunk stays record-aligned
		}
		else
		{
			m_bufferBytesWritten += _size;
		}
	}
	else
	{
		// The record doesn't fit in the remaining space. Flush the current (partial) buffer
		// as its own chunk and start this record at the beginning of a fresh buffer, so every
		// chunk holds only whole records (lets the loader decompress/parse chunks in parallel).
		// Records are always far smaller than BufferSize, so a fresh buffer always fits one.
#if RMEM_ENABLE_ASYNC_WRITE
		// The other half may still be in flight on the writer - wait before reusing/writing it.
		writerWaitIdle();
#endif
		writeBufferBytes	= m_bufferBytesWritten;
		writeBuffer			= doubleBuffer();				// returns the partial buffer, resets m_bufferBytesWritten

		memcpy(m_bufferPtr, _ptr, _size);
		m_bufferBytesWritten = (uint32_t)_size;
	}

	if (writeBuffer)
	{
#if RMEM_ENABLE_ASYNC_WRITE
		writerPost(writeBuffer, writeBufferBytes);			// hand off; producer keeps filling the other half
#else
		writeToFile(writeBuffer, writeBufferBytes);
#endif
	}

	m_mutexInternalBufferPtrs.unlock();
}

//--------------------------------------------------------------------------
/// Writes data to file, used internally by writeToBuffer
//--------------------------------------------------------------------------
void MemoryHook::writeToFile(void* _ptr, size_t _bytesToWrite)
{
#if RMEM_ENABLE_LZ4_COMPRESSION
	static const uint32_t compressedSig = 0x23234646;
#endif // RMEM_ENABLE_LZ4_COMPRESSION

	m_mutexWriteToFile.lock();

	if (!m_file)
	{
#if RMEM_PLATFORM_WINDOWS
		_wfopen_s(&m_file, m_fileName, L"ab");
#else
		m_file = fopen(m_fileName, "ab");
#endif
	}

	if (m_file)
	{
		if (m_excessBufferPtr)
		{
#if RMEM_ENABLE_LZ4_COMPRESSION
			uint32_t compSize = LZ4_compress_default((const char*)&m_bufferData[BufferSize * 2], (char*)m_bufferCompressed, (int)m_excessBufferSize, (int)sizeof(m_bufferCompressed));
			fwrite(&compressedSig, sizeof(uint32_t), 1, m_file);
			fwrite(&compSize, sizeof(uint32_t), 1, m_file);
			fwrite(m_bufferCompressed, compSize, 1, m_file);
#else
			fwrite(m_excessBufferPtr, m_excessBufferSize, 1, m_file);
#endif // RMEM_ENABLE_LZ4_COMPRESSION
			m_excessBufferPtr	= nullptr;
			m_excessBufferSize	= 0;
		}

#if RMEM_ENABLE_LZ4_COMPRESSION
		uint32_t compSize = LZ4_compress_default((const char*)_ptr, (char*)m_bufferCompressed, (int)_bytesToWrite, (int)sizeof(m_bufferCompressed));
		fwrite(&compressedSig, sizeof(uint32_t), 1, m_file);
		fwrite(&compSize, sizeof(uint32_t), 1, m_file);
		fwrite(m_bufferCompressed, compSize, 1, m_file);
#else
		fwrite(_ptr, _bytesToWrite, 1, m_file);
#endif // RMEM_ENABLE_LZ4_COMPRESSION
	}
	else
	{
		// no room left and file could not be opened -> no choice but to drop data :/
		if (m_excessBufferSize + _bytesToWrite > MemoryHook::BufferSize)
		{
			m_mutexWriteToFile.unlock();
			return;
		}

		m_excessBufferPtr = &m_bufferData[BufferSize * 2];
		memcpy(&m_bufferData[BufferSize * 2 + m_excessBufferSize], _ptr, _bytesToWrite);
		m_excessBufferSize += _bytesToWrite;
	}
	
#if RMEM_FLUSH_FILE_WRITES
	if (m_file)
		fflush(m_file);
#endif // RMEM_FLUSH_FILE_WRITES

	m_mutexWriteToFile.unlock();
}

//--------------------------------------------------------------------------
/// Dump additional debug info to help resolving symbols
//--------------------------------------------------------------------------
extern size_t getModuleInfo(uint8_t* _buffer, size_t _bufferSize);
void MemoryHook::writeModuleInfo()
{
	// Module-heavy processes (Qt/Chromium/games) can have hundreds of modules; a
	// small buffer truncates the list so the tail of modules can never be resolved.
	// Use a large static buffer (BSS, not stack/heap) - writeModuleInfo runs once at init.
	static uint8_t buffer[1024*1024];
	uint32_t symbolDataSize = (uint32_t)getModuleInfo(buffer, sizeof(buffer));

	writeToBuffer(&symbolDataSize, sizeof(uint32_t));
	writeToBuffer(buffer, symbolDataSize);
}

uint8_t* MemoryHook::doubleBuffer()
{
	uint8_t* ret = m_bufferPtr;

	if (m_bufferPtr != m_bufferData)
		m_bufferPtr = m_bufferData;
	else
		m_bufferPtr = m_bufferData + BufferSize;

	m_bufferBytesWritten = 0;

	return ret;
}

#if RMEM_ENABLE_ASYNC_WRITE

//--------------------------------------------------------------------------
/// Background writer thread: lifecycle + producer-side handoff
//--------------------------------------------------------------------------
void MemoryHook::writerStart()
{
	m_writeJobPtr	= 0;
	m_writeJobSize	= 0;
	m_writerStop	= false;
	m_writerStarted	= false;

#if RMEM_PLATFORM_WINDOWS
	InitializeCriticalSection(&m_writerLock);
	InitializeConditionVariable(&m_writerJobCv);
	InitializeConditionVariable(&m_writerDoneCv);
	m_writerThread	= CreateThread(0, 0, &MemoryHook::writerThreadEntry, this, 0, 0);
	m_writerStarted	= (m_writerThread != 0);
#else
	pthread_mutex_init(&m_writerLock, 0);
	pthread_cond_init(&m_writerJobCv, 0);
	pthread_cond_init(&m_writerDoneCv, 0);
	m_writerStarted	= (pthread_create(&m_writerThread, 0, &MemoryHook::writerThreadEntry, this) == 0);
#endif
}

void MemoryHook::writerStop()
{
#if RMEM_PLATFORM_WINDOWS
	if (m_writerStarted)
	{
		EnterCriticalSection(&m_writerLock);
		m_writerStop = true;
		WakeConditionVariable(&m_writerJobCv);
		LeaveCriticalSection(&m_writerLock);
		WaitForSingleObject(m_writerThread, INFINITE);
		CloseHandle(m_writerThread);
		m_writerThread	= 0;
		m_writerStarted	= false;
	}
	DeleteCriticalSection(&m_writerLock);
#else
	if (m_writerStarted)
	{
		pthread_mutex_lock(&m_writerLock);
		m_writerStop = true;
		pthread_cond_signal(&m_writerJobCv);
		pthread_mutex_unlock(&m_writerLock);
		pthread_join(m_writerThread, 0);
		m_writerStarted = false;
	}
	pthread_cond_destroy(&m_writerJobCv);
	pthread_cond_destroy(&m_writerDoneCv);
	pthread_mutex_destroy(&m_writerLock);
#endif
}

void MemoryHook::writerWaitIdle()
{
	if (!m_writerStarted)
		return;

#if RMEM_PLATFORM_WINDOWS
	EnterCriticalSection(&m_writerLock);
	while (m_writeJobPtr)
		SleepConditionVariableCS(&m_writerDoneCv, &m_writerLock, INFINITE);
	LeaveCriticalSection(&m_writerLock);
#else
	pthread_mutex_lock(&m_writerLock);
	while (m_writeJobPtr)
		pthread_cond_wait(&m_writerDoneCv, &m_writerLock);
	pthread_mutex_unlock(&m_writerLock);
#endif
}

void MemoryHook::writerPost(void* _ptr, size_t _size)
{
	if (!m_writerStarted)
	{
		writeToFile(_ptr, _size);	// no writer thread - write synchronously
		return;
	}

	// Caller guarantees the writer is idle (writerWaitIdle precedes the buffer swap), so the
	// job slot is free here.
#if RMEM_PLATFORM_WINDOWS
	EnterCriticalSection(&m_writerLock);
	m_writeJobPtr	= _ptr;
	m_writeJobSize	= _size;
	WakeConditionVariable(&m_writerJobCv);
	LeaveCriticalSection(&m_writerLock);
#else
	pthread_mutex_lock(&m_writerLock);
	m_writeJobPtr	= _ptr;
	m_writeJobSize	= _size;
	pthread_cond_signal(&m_writerJobCv);
	pthread_mutex_unlock(&m_writerLock);
#endif
}

void MemoryHook::writerLoop()
{
	// Mark this thread before doing anything that could allocate, so RMEM_WRITER_GUARD
	// suppresses this thread's own allocations (see the macro).
#if RMEM_PLATFORM_WINDOWS
	m_writerThreadId = getThreadID();
#else
	s_rmemIsWriterThread = true;
#endif

	for (;;)
	{
		void*	jobPtr;
		size_t	jobSize;

#if RMEM_PLATFORM_WINDOWS
		EnterCriticalSection(&m_writerLock);
		while (!m_writeJobPtr && !m_writerStop)
			SleepConditionVariableCS(&m_writerJobCv, &m_writerLock, INFINITE);
		if (!m_writeJobPtr && m_writerStop)
		{
			LeaveCriticalSection(&m_writerLock);
			break;
		}
		jobPtr	= m_writeJobPtr;
		jobSize	= m_writeJobSize;
		LeaveCriticalSection(&m_writerLock);
#else
		pthread_mutex_lock(&m_writerLock);
		while (!m_writeJobPtr && !m_writerStop)
			pthread_cond_wait(&m_writerJobCv, &m_writerLock);
		if (!m_writeJobPtr && m_writerStop)
		{
			pthread_mutex_unlock(&m_writerLock);
			break;
		}
		jobPtr	= m_writeJobPtr;
		jobSize	= m_writeJobSize;
		pthread_mutex_unlock(&m_writerLock);
#endif

		writeToFile(jobPtr, jobSize);

#if RMEM_PLATFORM_WINDOWS
		EnterCriticalSection(&m_writerLock);
		m_writeJobPtr = 0;
		WakeConditionVariable(&m_writerDoneCv);
		LeaveCriticalSection(&m_writerLock);
#else
		pthread_mutex_lock(&m_writerLock);
		m_writeJobPtr = 0;
		pthread_cond_signal(&m_writerDoneCv);
		pthread_mutex_unlock(&m_writerLock);
#endif
	}
}

#if RMEM_PLATFORM_WINDOWS
DWORD WINAPI MemoryHook::writerThreadEntry(LPVOID _arg)
{
	((MemoryHook*)_arg)->writerLoop();
	return 0;
}
#else
void* MemoryHook::writerThreadEntry(void* _arg)
{
	((MemoryHook*)_arg)->writerLoop();
	return 0;
}
#endif

#endif // RMEM_ENABLE_ASYNC_WRITE

} // namespace rmem
