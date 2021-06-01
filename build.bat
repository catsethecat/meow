@echo off
	
if not defined DevEnvDir ( call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 )
	
set CompilerFlags= -MT -nologo -Oi -O2 -W4 -wd4204
set LinkerFlags= -INCREMENTAL:NO -opt:ref user32.lib Ws2_32.lib Dsound.lib res.res
	
pushd bin
rc -nologo /fo res.res ..\res\res.rc 
cl %CompilerFlags% ..\src\main.c /link %LinkerFlags% /out:meow.exe
del main.obj
del res.res
popd

pause