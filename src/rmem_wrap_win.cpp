/*
 * Copyright (c) 2017 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#define _CRT_SECURE_NO_WARNINGS 1

#include "rmem_platform.h"

#if RMEM_PLATFORM_WINDOWS

#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>
#include <Shlobj.h>
#include <Psapi.h>

wchar_t g_prefixAppData[512];
typedef HRESULT (WINAPI *fnSHGetFolderPathW)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);

// PSAPI

typedef BOOL  (WINAPI * fnGetModuleInformation)(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb);
typedef BOOL  (WINAPI * fnEnumProcessModules)(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded);
typedef DWORD (WINAPI * fnGetModuleFileNameExW)(HANDLE  hProcess, HMODULE hModule, LPWSTR lpFilename, DWORD nSize);

fnGetModuleInformation	gFn_getModuleInformation	= 0;
fnEnumProcessModules	gFn_enumProcessModules		= 0;
fnGetModuleFileNameExW	gFn_getModuleFileNameExW	= 0;

#if !RMEM_COMPILER_GCC
#pragma warning (push)
#pragma warning (disable:4127)
#endif

#include "../3rd/minhook/include/MinHook.h"
#include "../3rd/minhook/src/buffer.c"
#include "../3rd/minhook/src/hook.c"
#include "../3rd/minhook/src/trampoline.c"
#if RMEM_32BIT
#include "../3rd/minhook/src/hde/hde32.c"
#else
#include "../3rd/minhook/src/hde/hde64.c"
#endif

#if !RMEM_COMPILER_GCC
#pragma warning (pop)
#endif

void rmemAddModuleC(const char* _name, uint64_t _base, uint32_t _size);
void rmemAddModuleW(const wchar_t* _name, uint64_t _base, uint32_t _size);

// RTL HeapAlloc routines
typedef PVOID (WINAPI *fn_RtlAllocateHeap)(PVOID hHeap, ULONG dwFlags, SIZE_T dwBytes);
typedef LPVOID (WINAPI *fn_RtlReAllocateHeap)(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);
typedef BOOLEAN (WINAPI *fn_RtlFreeHeap)(PVOID hHeap, ULONG dwFlags, PVOID lpMem);
typedef HMODULE (WINAPI *fn_LoadLibraryA)(LPCSTR _fileName);
typedef HMODULE (WINAPI *fn_LoadLibraryW)(LPCWSTR _fileName);
typedef HMODULE (WINAPI *fn_LoadLibraryExA)( LPCSTR _fileName, HANDLE _file, DWORD _flags );
typedef HMODULE (WINAPI *fn_LoadLibraryExW)( LPCWSTR _fileName, HANDLE _file, DWORD _flags );
typedef void (__cdecl *fn_exit)( int code );

#define FN_ORIGINAL(name)			\
	fn_##name fn_##name##_t;

#define CALL_ORIGINAL(name)			\
	fn_##name##_t

#define GET_PROC_ADDRESS(hmodule, name)									\
	( (fn_##name)(::GetProcAddress(hmodule, #name)) )

#define CREATE_HOOK(hmodule, name)										\
		MH_CreateHook((void*)GET_PROC_ADDRESS(hmodule, name), (void*)&detour_##name, (void **)&(CALL_ORIGINAL(name)));

#define REMOVE_HOOK(hmodule, name)										\
		MH_RemoveHook((void*)GET_PROC_ADDRESS(hmodule, name));

FN_ORIGINAL(RtlAllocateHeap)
FN_ORIGINAL(RtlReAllocateHeap)
FN_ORIGINAL(RtlFreeHeap)
FN_ORIGINAL(LoadLibraryA)
FN_ORIGINAL(LoadLibraryW)
FN_ORIGINAL(LoadLibraryExA)
FN_ORIGINAL(LoadLibraryExW)
FN_ORIGINAL(exit)

extern HANDLE g_hHeap;

static inline uint32_t RtlGetOverhead(size_t _size)
{
	static uint32_t overheadPerBlock = 0;
	if (overheadPerBlock == 0)
	{
		HANDLE heap = HeapCreate(HEAP_NO_SERIALIZE, 4096, 0);
		char* mem0 = (char*)HeapAlloc(heap, 0x00800000, 140);
		char* mem1 = (char*)HeapAlloc(heap, 0x00800000, 140);
		HeapFree(heap,0x00800000,mem0);
		HeapFree(heap,0x00800000,mem1);
		HeapDestroy(heap);
		overheadPerBlock = (uint32_t)(mem1 - (mem0+140));
	}
	 
	const uint32_t granularityMask = (uint32_t)(sizeof(void*) * 2) - 1; // 32bit - 8 bytes granularity, 64bit 16 bytes
	const uint32_t extraBytes = (_size - 1) & granularityMask;
	const uint32_t overhead = granularityMask - extraBytes;
	return overheadPerBlock + overhead;
}

PVOID WINAPI detour_RtlAllocateHeap(PVOID _heap, ULONG _flags, SIZE_T _size)
{
	void* ret = (CALL_ORIGINAL(RtlAllocateHeap)(_heap, _flags, _size));
	
	if (((_flags & 0x50800163) == 0) && (_heap != g_hHeap))
		rmemAlloc((uint64_t)_heap, ret, (uint32_t)_size, RtlGetOverhead(_size));
	
	return ret;
}

BOOLEAN WINAPI detour_RtlFreeHeap(PVOID _heap, ULONG _flags, PVOID _ptr)
{
	if (!_ptr)
		return TRUE;
	
	if (((_flags & 0x50800063) == 0) && (_heap != g_hHeap))
		rmemFree((uint64_t)_heap, _ptr);
	
	return (CALL_ORIGINAL(RtlFreeHeap)(_heap, _flags, _ptr));
}

LPVOID WINAPI detour_RtlReAllocateHeap(HANDLE _heap, DWORD _flags, LPVOID _ptr, SIZE_T _size)
{
	PVOID ret = (CALL_ORIGINAL(RtlReAllocateHeap)(_heap, _flags, _ptr, _size));
	
	if (((_flags & 0x50800163) == 0) && (_heap != g_hHeap))
		rmemRealloc((uint64_t)_heap, ret, (uint32_t)_size, RtlGetOverhead(_size), _ptr);
	
	return ret;
}

HMODULE WINAPI detour_LoadLibraryA(LPCSTR _fileName)
{
	HMODULE ret = (CALL_ORIGINAL(LoadLibraryA)(_fileName));
	if (ret != NULL)
	{
		MODULEINFO info;
		if (0 == gFn_getModuleInformation(GetCurrentProcess(), ret, &info, sizeof(MODULEINFO)))
			return ret;
		char fullPath[1024];
		if (0 == GetModuleFileNameA(ret, fullPath, 1024))
			return ret;
		rmemAddModuleC(fullPath, (uint64_t)info.lpBaseOfDll, (uint32_t)info.SizeOfImage);
	}
	return ret;
}

HMODULE WINAPI detour_LoadLibraryW(LPCWSTR _fileName)
{
	HMODULE ret = (CALL_ORIGINAL(LoadLibraryW)(_fileName));
	if (ret != NULL)
	{
		MODULEINFO info;
		if (0 == gFn_getModuleInformation(GetCurrentProcess(), ret, &info, sizeof(MODULEINFO)))
			return ret;
		wchar_t fullPath[1024];
		if (0 == GetModuleFileNameW(ret, fullPath, 1024))
			return ret;
		rmemAddModuleW(fullPath, (uint64_t)info.lpBaseOfDll, (uint32_t)info.SizeOfImage);
	}
	return ret;
}

HMODULE WINAPI detour_LoadLibraryExA(LPCSTR _fileName, HANDLE _file, DWORD _flags)
{
	HMODULE ret = (CALL_ORIGINAL(LoadLibraryExA)(_fileName, _file, _flags));
	if (ret != NULL)
	{
		MODULEINFO info;
		if (0 == gFn_getModuleInformation(GetCurrentProcess(), ret, &info, sizeof(MODULEINFO)))
			return ret;
		char fullPath[1024];
		if (0 == GetModuleFileNameA(ret, fullPath, 1024))
			return ret;
		rmemAddModuleC(fullPath, (uint64_t)info.lpBaseOfDll, (uint32_t)info.SizeOfImage);
	}
	return ret;
}

HMODULE WINAPI detour_LoadLibraryExW(LPCWSTR _fileName, HANDLE _file, DWORD _flags)
{
	HMODULE ret = (CALL_ORIGINAL(LoadLibraryExW)(_fileName, _file, _flags));
	if (ret != NULL)
	{
		MODULEINFO info;
		if (0 == gFn_getModuleInformation(GetCurrentProcess(), ret, &info, sizeof(MODULEINFO)))
			return ret;
		wchar_t fullPath[1024];
		if (0 == GetModuleFileNameW(ret, fullPath, 1024))
			return ret;
		rmemAddModuleW(fullPath, (uint64_t)info.lpBaseOfDll, (uint32_t)info.SizeOfImage);
	}
	return ret;
}

bool g_linkerBased;

extern "C"
{
	void rmemUnhookAllocs();

	void detour_exit(int _code)
	{
		rmemUnhookAllocs();
		(CALL_ORIGINAL(exit)(_code));
		MH_RemoveHook((void*)&exit);
		if (g_linkerBased)
			MH_Uninitialize();
	}

	FARPROC loadFunc(HMODULE _kernel, HMODULE _psapi, const char* _name)
	{
		FARPROC ret = ::GetProcAddress(_kernel, _name);
		if (!ret && (_psapi != 0))
			ret = ::GetProcAddress(_psapi, _name);
		return ret;
	}

	void rmemHookAllocs(int _isLinkerBased)
	{
		HMODULE hntdll32	= ::GetModuleHandleA("ntdll");
		HMODULE kerneldll32	= ::GetModuleHandleA("kernel32");
		HMODULE psapiDLL	= ::LoadLibraryA("Psapi.dll");

		gFn_getModuleInformation	= (fnGetModuleInformation)loadFunc(kerneldll32, psapiDLL, "GetModuleInformation");
		gFn_enumProcessModules		= (fnEnumProcessModules)  loadFunc(kerneldll32, psapiDLL, "EnumProcessModules");
		gFn_getModuleFileNameExW	= (fnGetModuleFileNameExW)loadFunc(kerneldll32, psapiDLL, "GetModuleFileNameExW");

		g_prefixAppData[0] = 0;
		HMODULE shelldll32 = ::LoadLibraryA("Shell32");
		if (shelldll32)
		{
			fnSHGetFolderPathW fn = (fnSHGetFolderPathW)::GetProcAddress(shelldll32, "SHGetFolderPathW");
			if (fn)
			{
				if (SUCCEEDED(fn(NULL, CSIDL_APPDATA|CSIDL_FLAG_CREATE, NULL, 0, g_prefixAppData)))
					wcscat(g_prefixAppData, L"\\MTuner\\");
				else
					g_prefixAppData[0] = 0;
			}

			FreeLibrary(shelldll32);
		}

		MH_Initialize();

		CREATE_HOOK(hntdll32, RtlFreeHeap);
		CREATE_HOOK(hntdll32, RtlAllocateHeap);
		CREATE_HOOK(hntdll32, RtlReAllocateHeap);

		CREATE_HOOK(kerneldll32, LoadLibraryA);
		CREATE_HOOK(kerneldll32, LoadLibraryW);
		CREATE_HOOK(kerneldll32, LoadLibraryExA);
		CREATE_HOOK(kerneldll32, LoadLibraryExW);
		
		if (_isLinkerBased)
		{
			MH_CreateHook((void*)&exit, (void*)&detour_exit, (void **)&(CALL_ORIGINAL(exit)));
			g_linkerBased = true;
		}
		else
			g_linkerBased = false;

		MH_EnableHook(MH_ALL_HOOKS);

		rmemInit(g_prefixAppData[0] != 0 ? g_prefixAppData : 0);
	}

	void rmemUnhookAllocs()
	{
		HMODULE hntdll32 = ::GetModuleHandleA("ntdll");
		HMODULE kerneldll32 = ::GetModuleHandleA("kernel32");
		
		REMOVE_HOOK(hntdll32, RtlFreeHeap);
		REMOVE_HOOK(hntdll32, RtlAllocateHeap);
		REMOVE_HOOK(hntdll32, RtlReAllocateHeap);

		REMOVE_HOOK(kerneldll32, LoadLibraryA);
		REMOVE_HOOK(kerneldll32, LoadLibraryW);
		REMOVE_HOOK(kerneldll32, LoadLibraryExA);
		REMOVE_HOOK(kerneldll32, LoadLibraryExW);

		if (!g_linkerBased)
			MH_Uninitialize();

		rmemShutDown();
	}
};

#endif // RMEM_PLATFORM_WINDOWS
