<img height="81" src="https://rudji.com/rudji_games_logo_bright.svg"/>

[![Build status](https://ci.appveyor.com/api/projects/status/cbymgif55qmpa4tq?svg=true)](https://ci.appveyor.com/project/milostosic/rmem-f7tyu)
[![License](https://img.shields.io/badge/license-BSD--2%20clause-blue.svg)](https://github.com/RudjiGames/rmem/blob/master/LICENSE)

**rmem** is an SDK library for [**MTuner**](https://github.com/milostosic/MTuner), providing additional features and cross-platform support.

Source Code
======

You can get the latest source code by cloning it from github:

      git clone https://github.com/RudjiGames/rmem.git 

Build
======

**rmem** (MTuner SDK) GENie script can generate Microsoft Visual Studio solution or 
makefiles for a GCC based compiler or cross compiler. Generating a MSVC
solution is done using the following command:

      genie vs2015

The generated solution will be located in the following folder:
   .build/projects/vs2015

Generating makefiles for a GCC based toolchain is done in a similar way, for
example:

      MinGW :   genie --gcc=mingw gmake
      PS3   :   genie --gcc=ps3   gmake
      PS4   :   genie --gcc=ps4   gmake

Note that MinGW toolchain requires a MINGW environment variable pointint to
install location of MinGW, for example:

      set MINGW=C:\TDM-GCC-64

Generated makefiles will be located in the following folder:

      .build/projects/gmake/$(GCC_TOOLCHAIN)

All of the generated solutions/makefiles provide a Debug and Release configuration,
in both 32bit and 64bit versions. Some platforms support only one word size, for 
example PS3 works with debug64/release64 configurations.

To build a GCC based library use the following command lines:

      Debug   :  make -R config=debug32
      Release :  make -R config=release32

Note that certain platforms require 64bit builds, for example to build on PS4:

      Release :  make -R config=release64

License (BSD 2-clause)
======

<a href="http://opensource.org/licenses/BSD-2-Clause" target="_blank">
<img align="right" src="https://opensource.org/wp-content/uploads/2022/10/osi-badge-dark.svg" width="100" height="137">
</a>

	Copyright 2025 Milos Tosic. All rights reserved.
	
	https://github.com/RudjiGames/rmem
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	
	   1. Redistributions of source code must retain the above copyright notice,
	      this list of conditions and the following disclaimer.
	
	   2. Redistributions in binary form must reproduce the above copyright
	      notice, this list of conditions and the following disclaimer in the
	      documentation and/or other materials provided with the distribution.
	
	THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDER ``AS IS'' AND ANY EXPRESS OR
	IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
	MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
	EVENT SHALL COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
	INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
	THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
