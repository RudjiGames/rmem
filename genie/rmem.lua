--
-- Copyright (c) 2017 Milos Tosic. All rights reserved.
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectExtraConfigExecutable_linker()
	configuration { "gmake" }
		if ("mingw-gcc"   == _OPTIONS["gcc"]) or -- on windows, we patch heap functions, no need to wrap malloc family of funcs
		   ("mingw-clang" == _OPTIONS["gcc"]) then -- on windows, we patch heap functions, no need to wrap malloc family of funcs
			linkoptions { "-Wl,--wrap=_malloc_init--export-all-symbols" }
			links { "psapi" }
		else 
			linkoptions { "-Wl,--wrap=_malloc_init,--wrap=malloc,--wrap=realloc,--wrap=calloc,--wrap=free,--wrap=memalign,--wrap=reallocalign" }
			if not os.is("linux") and not (_OPTIONS["gcc"] == "orbis") then
				linkoptions { "--export-all-symbols,--wrap=_expand" }
			end
		end
		
	configuration { "vs*", "orbis" }
		linkoptions { "-Wl,--wrap=_malloc_init,--wrap=malloc,--wrap=realloc,--wrap=calloc,--wrap=free,--wrap=memalign,--wrap=reallocalign" }

	configuration { "vs*", "not orbis" }
		linkoptions { "/ENTRY:rmemEntry" }

	configuration {}
end

function projectExtraConfigExecutable_manual()
	configuration { "windows", "gmake" }
		if not (_OPTIONS["gcc"] == "orbis") then
			linkoptions { "-Wl,--export-all-symbols" }
		end
		if "mingw" == _OPTIONS["gcc"] then
			links { "psapi" }
		end
	configuration {}		
end

function projectAdd_rmem() 
	addProject_lib("rmem")
end
