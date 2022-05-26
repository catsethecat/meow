@echo off

set vcvarsallpath="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
	
set CompilerFlags= -MT -nologo -O1 -W3 -GS- -Gs9999999
set LinkerFlags= /incremental:no /opt:icf /opt:ref res.res /subsystem:windows /NODEFAULTLIB /STACK:0x100000,0x100000 kernel32.lib user32.lib shell32.lib gdi32.lib dsound.lib ws2_32.lib secur32.lib
	
call %vcvarsallpath% x64
rc -nologo /fo res.res res.rc
cl %CompilerFlags% main.c /link %LinkerFlags% /out:Meow.exe
del main.obj
del res.res
pause