This directory contains source code for a command line wrapper of the NAL NL2
DLL.  Users should install the wrapper as documented in file 
https://github.com/HoerTech-gGmbH/openMHA/blob/master/README_NALNL2.md. This
file documents how to recompile the wrapper in case of changes.

## COMPILATION

Requirements:
* Windows operating system
* msys2:  https://www.msys2.org/
* In msys2: packages "make" and "mingw32/mingw-w64-i686-gcc"
* From NAL: files "NAL-NL2.lib" and "NAL-NL2.dll"

To compile, add NAL-NL2.dll and NAL-NL2.lib files from the NAL NL2 Developer
Kit to this directory, start an msys2 MinGW 32 bit terminal, and in this
terminal change to this directory and invoke "make".
