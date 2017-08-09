/*
 * Copyright (c) 2017 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "rmem_utils.h"

#if RMEM_PLATFORM_WINDOWS
#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#if RMEM_COMPILER_MSVC
#pragma comment(lib,"psapi.lib")
#endif // RMEM_COMPILER_GCC
#endif

namespace rmem {

#if RMEM_PLATFORM_WINDOWS

	uint32_t getSymbolInfo(uint8_t* _buffer)
	{
		uint32_t buffPtr = 0;

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
		if (snapshot != INVALID_HANDLE_VALUE)
		{
			MODULEENTRY32W me;
			BOOL cap = Module32FirstW(snapshot, &me);
			if (!cap)
			{
				// fall back on enumerating modules
				HMODULE hMods[1024];
				DWORD cbNeeded;

				if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded))
				{
					for (uint32_t i=0; i<(cbNeeded/sizeof(HMODULE)); ++i)
					{
						wchar_t szModName[MAX_PATH];

						MODULEINFO mi;
						GetModuleInformation(GetCurrentProcess(), hMods[i], &mi, sizeof(mi) );
					
						if (GetModuleFileNameExW(GetCurrentProcess(), hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
						{
							uint64_t modBase = (uint64_t)mi.lpBaseOfDll;
							uint64_t modSize = (uint64_t)mi.SizeOfImage;
							addStrToBuffer( szModName, _buffer, buffPtr, 0x23 );
							addVarToBuffer( modBase, _buffer, buffPtr );
							addVarToBuffer( modSize, _buffer, buffPtr );
						}
					}
				}
			}
			else
			while (cap)
			{
				uint64_t modBase = (uint64_t)me.modBaseAddr;
				uint64_t modSize = (uint64_t)me.modBaseSize;
				addStrToBuffer( me.szExePath, _buffer, buffPtr, 0x23 );
				addVarToBuffer( modBase, _buffer, buffPtr );
				addVarToBuffer( modSize, _buffer, buffPtr );

				cap = Module32NextW(snapshot, &me);
			}

			CloseHandle(snapshot);
		}
	
		return buffPtr;
	}

#elif RMEM_PLATFORM_XBOX360

	uint32_t getSymbolInfo(uint8_t* _buffer)
	{
		HRESULT error;
		PDM_WALK_MODULES pWalkMod = NULL;
		DMN_MODLOAD modLoad;
		uint32_t buffPtr = 0;

		while (XBDM_NOERR == (error = DmWalkLoadedModules(&pWalkMod, &modLoad)))
		{
			// Examine the contents of modLoad.
			uint64_t modBase = (uint64_t)(uint32_t)modLoad.BaseAddress;
			uint64_t modSize = (uint64_t)modLoad.Size;
			addStrToBuffer( modLoad.Name, _buffer, buffPtr, 0x23 );
			addVarToBuffer( modBase, _buffer, buffPtr );
			addVarToBuffer( modSize, _buffer, buffPtr );
		}

		if (error != XBDM_ENDOFLIST)
			return 0;

		DmCloseLoadedModules(pWalkMod);

		return buffPtr;
	}	

#else 

	uint32_t getSymbolInfo(uint8_t* _buffer)
	{
		(void)_buffer;
		return 0;
	}

#endif // RMEM_PLATFORM_WINDOWS

} // namespace rmem
