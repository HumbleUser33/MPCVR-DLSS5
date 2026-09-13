@ECHO OFF
REM Builds the DLSS 5 NR probe and the caller shim it needs.
REM The shim source is shared with the NgxShim project -- there is only one copy.

SETLOCAL
PUSHD %~dp0

FOR /F "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) DO SET VSPATH=%%i
IF "%VSPATH%"=="" (
  ECHO Visual Studio not found.
  EXIT /B 1
)
CALL "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo || EXIT /B 1

ECHO Building the caller shim (nvngx.dll)...
cl /nologo /EHsc /std:c++20 /O2 /MT /LD "..\..\NgxShim\ngxshim.cpp" /Fe:nvngx.dll ^
   /link /DEF:"..\..\NgxShim\ngxshim.def" || EXIT /B 1

ECHO Building the probe...
SET MH=..\..\external\minhook\src
cl /nologo /EHsc /std:c++20 /O2 /MT probe.cpp ^
   "%MH%\hook.c" "%MH%\buffer.c" "%MH%\trampoline.c" "%MH%\hde\hde64.c" ^
   /Fe:dlssnr_probe.exe || EXIT /B 1

ECHO Building the harness (compiles the filter's own DLSS code)...
SET SRC=..\..\Source
cl /nologo /EHsc /std:c++20 /O2 /MT /DNOMINMAX /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 ^
   /DUNICODE /D_UNICODE /I"%SRC%" harness.cpp ^
   "%SRC%\DLSS\DlssNR.cpp" "%SRC%\DX11Helper.cpp" ^
   "%MH%\hook.c" "%MH%\buffer.c" "%MH%\trampoline.c" "%MH%\hde\hde64.c" ^
   /Fe:dlssnr_harness.exe || EXIT /B 1

ECHO Building the video processor rebuild test (the paused green frame)...
cl /nologo /EHsc /std:c++20 /O2 /MT /DNOMINMAX /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 ^
   /DUNICODE /D_UNICODE /I"%SRC%" vp_rebuild_test.cpp ^
   "%SRC%\D3D11VP.cpp" "%SRC%\DX11Helper.cpp" ^
   /Fe:vp_rebuild_test.exe /link strmiids.lib || EXIT /B 1

DEL /Q *.obj *.exp 2>NUL

ECHO.
ECHO Run dlssnr_harness.exe and vp_rebuild_test.exe before putting a build in the player.
ECHO.
ECHO Done. Usage:
ECHO   dlssnr_probe.exe                  report only, nothing altered
ECHO   dlssnr_probe.exe --spoof-auto     report the architecture the DLL asks for
ECHO   dlssnr_probe.exe --spoof 1B0      report a specific architecture id
ECHO.
ECHO   other flags: --dll ^<path^>  --w 1920  --h 1080  --no-shim
POPD
