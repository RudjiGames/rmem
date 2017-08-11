--
-- Copyright (c) 2017 Milos Tosic. All rights reserved.
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

local scriptDir = path.getdirectory(debug.getinfo(2, "S").source:sub(2)) .. "/.."
local relativePath = scriptDir
for v=1,6,1 do
	relativePath = relativePath .. "/.."
	local globals_script = path.getabsolute(relativePath) .. "/build/globals.lua"
	if os.isfile(globals_script) then dofile (globals_script) break end
end

function rmemExtraConfig_linker()
	configuration { "gmake" }
		if "mingw" == _OPTIONS["gcc"] then -- on windows, we patch heap functions, no need to wrap malloc family of funcs
			linkoptions { "-Wl,--wrap=_malloc_init--export-all-symbols" }
			links { "psapi" }
		else 
			linkoptions { "-Wl,--wrap=_malloc_init,--wrap=malloc,--wrap=realloc,--wrap=calloc,--wrap=free,--wrap=memalign,--wrap=reallocalign,--wrap=_expand" }
			if not os.is("linux") then
				linkoptions { "--export-all-symbols" }
			end
		end
	configuration { "vs*" }
		linkoptions { "/ENTRY:rmemEntry" }
	configuration {}
end

function rmemExtraConfig_manual()
	configuration { "windows", "gmake" }
		linkoptions { "-Wl,--export-all-symbols" }
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

