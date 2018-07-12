--
-- Copyright (c) 2017 Milos Tosic. All rights reserved.
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

local currPath = path.getdirectory(debug.getinfo(2, "S").source:sub(2)) .. "/.."
while string.len(currPath) > 2 do 
	currPath = path.getabsolute(currPath .. "/..")
	if os.isfile(currPath .. "/build/build.lua") then dofile (currPath .. "/build/build.lua") break end
end

function rmemExtraConfig_linker()
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

function rmemExtraConfig_manual()
	configuration { "windows", "gmake" }
		if not (_OPTIONS["gcc"] == "orbis") then
			linkoptions { "-Wl,--export-all-symbols" }
		end
		if "mingw" == _OPTIONS["gcc"] then
			links { "psapi" }
		end
	configuration {}		
end

solution "rmem"
	configurations { "debug", "release", "retail" }
	setPlatforms()

	addProject_lib("rmem")
	addProject_lib_sample("rmem", "linker", false, rmemExtraConfig_linker)
	addProject_lib_sample("rmem", "manual", false, rmemExtraConfig_manual)

