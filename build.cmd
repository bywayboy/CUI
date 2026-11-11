@ECHO OFF
REM ============================================================================
REM CUI demo build (MSVC x64, 0CRT, Direct2D/DComp)
REM
REM   build.cmd            默认 Release 风格 (/O1)
REM   产物: bin\cui_demo.exe / cui_minimal.exe / cui_advanced.exe
REM
REM 依赖: Visual Studio 2019/2022/2026 (含 C++ 桌面工作负载)
REM ============================================================================
SETLOCAL EnableDelayedExpansion
CD /D "%~dp0"

SET "VCVARS="
FOR %%V IN (18,2022,2019) DO (
    IF NOT DEFINED VCVARS IF EXIST "C:\Program Files\Microsoft Visual Studio\%%V\Community\VC\Auxiliary\Build\vcvarsall.bat" SET "VCVARS=C:\Program Files\Microsoft Visual Studio\%%V\Community\VC\Auxiliary\Build\vcvarsall.bat"
    IF NOT DEFINED VCVARS IF EXIST "C:\Program Files\Microsoft Visual Studio\%%V\Professional\VC\Auxiliary\Build\vcvarsall.bat" SET "VCVARS=C:\Program Files\Microsoft Visual Studio\%%V\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    IF NOT DEFINED VCVARS IF EXIST "C:\Program Files\Microsoft Visual Studio\%%V\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" SET "VCVARS=C:\Program Files\Microsoft Visual Studio\%%V\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
)
IF NOT DEFINED VCVARS (
    ECHO [cui] vcvarsall.bat not found - install Visual Studio C++ tools
    EXIT /B 1
)
CALL "%VCVARS%" x64 >NUL
IF ERRORLEVEL 1 (
    ECHO [cui] failed to load VS environment
    EXIT /B 1
)

IF NOT EXIST bin MKDIR bin
IF NOT EXIST build MKDIR build
IF NOT EXIST build\obj MKDIR build\obj
del /q build\obj\*.obj bin\cui_demo.exe bin\cui_minimal.exe bin\cui_advanced.exe bin\cui_multi.exe bin\cui_mvvm.exe bin\cui_scroll.exe bin\cui_tabs.exe bin\cui_icons.exe bin\cui_controls.exe bin\cui_overlays.exe bin\cui_textarea.exe 2>nul

REM 自定义 D2D 效果的着色器: 离线编译成字节数组, 运行时不需要 d3dcompiler.dll
SET "FXC=%WindowsSdkVerBinPath%x64\fxc.exe"
IF NOT EXIST "%FXC%" (
    ECHO [cui] fxc.exe not found - install Windows SDK
    EXIT /B 1
)
ECHO [cui] src\gfx\grain.hlsl
"%FXC%" /nologo /T ps_4_0 /E main /Fh build\grain_cso.h /Vn g_grainCso src\gfx\grain.hlsl || GOTO :err

SET "CFLAGS=/nologo /c /std:c11 /O1 /Oi- /GS- /Gy /Gs32768 /W3 /utf-8 /wd4244 /wd4996 /DNO_CRT_NO_DEFAULTLIB /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /I. /Isrc /Isrc\ui /Isrc\gfx /Isrc\lib"
SET "CXXFLAGS=/nologo /c /std:c++17 /O1 /GS- /Gy /Gs32768 /W3 /utf-8 /wd4244 /wd4996 /EHs-c- /GR- /D_HAS_EXCEPTIONS=0 /DNO_CRT_NO_DEFAULTLIB /D_WIN32_WINNT=0x0A00 /DUNICODE /D_UNICODE /I. /Isrc /Isrc\ui /Isrc\gfx /Isrc\lib /Ibuild"
SET "LIBS=kernel32.lib user32.lib gdi32.lib d2d1.lib dwrite.lib d3d11.lib dxgi.lib dcomp.lib ole32.lib advapi32.lib shell32.lib imm32.lib winmm.lib"

SET "OBJS="
FOR %%F IN (src\edit.c src\log.c src\lib\crtstub.c src\ui\ui_anim.c src\ui\ui_context.c src\ui\ui_layout.c src\ui\ui_frame.c src\ui\ui_ime.c src\ui\ui_paint.c src\ui\ui_theme.c src\ui\ui_widgets.c src\ui\ui_icon.c src\ui\ui_popup.c src\ui\ui_tooltip.c src\ui\ui_controls.c src\ui\ui_select.c src\ui\ui_menu.c src\ui\ui_textarea.c src\ui\ui_button_group.c src\ui\ui_toast.c src\ui\ui_input.c src\ui\ui_app.c src\ui\ui_progress.c src\ui\ui_scroll.c src\ui\ui_tab.c) DO (
    ECHO [cui] %%F
    cl %CFLAGS% "%%F" /Fobuild\obj\%%~nF.obj || GOTO :err
    SET "OBJS=!OBJS! build\obj\%%~nF.obj"
)

ECHO [cui] src\gfx\gfx_d2d.cpp
cl %CXXFLAGS% src\gfx\gfx_d2d.cpp /Fobuild\obj\gfx_d2d.obj || GOTO :err
SET "OBJS=!OBJS! build\obj\gfx_d2d.obj"

ECHO [cui] src\lib\mem_avx2.c (AVX2)
cl %CFLAGS% /arch:AVX2 src\lib\mem_avx2.c /Fobuild\obj\mem_avx2.obj || GOTO :err
SET "OBJS=!OBJS! build\obj\mem_avx2.obj"

ECHO [cui] src\demo.c
cl %CFLAGS% src\demo.c /Fobuild\obj\demo.obj || GOTO :err
ECHO [cui] samples\minimal.c
cl %CFLAGS% samples\minimal.c /Fobuild\obj\minimal.obj || GOTO :err
ECHO [cui] samples\advanced.c
cl %CFLAGS% samples\advanced.c /Fobuild\obj\advanced.obj || GOTO :err
ECHO [cui] samples\multiwindow.c
cl %CFLAGS% samples\multiwindow.c /Fobuild\obj\multiwindow.obj || GOTO :err
ECHO [cui] samples\mvvm.c
cl %CFLAGS% samples\mvvm.c /Fobuild\obj\mvvm.obj || GOTO :err
ECHO [cui] samples\scrollview.c
cl %CFLAGS% samples\scrollview.c /Fobuild\obj\scrollview.obj || GOTO :err
ECHO [cui] samples\tabs.c
cl %CFLAGS% samples\tabs.c /Fobuild\obj\tabs.obj || GOTO :err
ECHO [cui] samples\icons.c
cl %CFLAGS% samples\icons.c /Fobuild\obj\icons.obj || GOTO :err
ECHO [cui] samples\controls.c
cl %CFLAGS% samples\controls.c /Fobuild\obj\controls.obj || GOTO :err
ECHO [cui] samples\overlays.c
cl %CFLAGS% samples\overlays.c /Fobuild\obj\overlays.obj || GOTO :err
ECHO [cui] samples\textarea.c
cl %CFLAGS% samples\textarea.c /Fobuild\obj\textarea.obj || GOTO :err

ECHO [cui] link demo
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_demo.exe !OBJS! build\obj\demo.obj %LIBS% || GOTO :err

ECHO [cui] link minimal
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_minimal.exe !OBJS! build\obj\minimal.obj %LIBS% || GOTO :err

ECHO [cui] link advanced
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_advanced.exe !OBJS! build\obj\advanced.obj %LIBS% || GOTO :err

ECHO [cui] link multiwindow
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_multi.exe !OBJS! build\obj\multiwindow.obj %LIBS% || GOTO :err

ECHO [cui] link mvvm
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_mvvm.exe !OBJS! build\obj\mvvm.obj %LIBS% || GOTO :err

ECHO [cui] link scrollview
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_scroll.exe !OBJS! build\obj\scrollview.obj %LIBS% || GOTO :err

ECHO [cui] link tabs
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_tabs.exe !OBJS! build\obj\tabs.obj %LIBS% || GOTO :err

ECHO [cui] link icons
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_icons.exe !OBJS! build\obj\icons.obj %LIBS% || GOTO :err

ECHO [cui] link controls
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_controls.exe !OBJS! build\obj\controls.obj %LIBS% || GOTO :err

ECHO [cui] link overlays
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_overlays.exe !OBJS! build\obj\overlays.obj %LIBS% || GOTO :err

ECHO [cui] link textarea
link /nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /NODEFAULTLIB /Brepro /ENTRY:startup /MANIFEST:NO ^
    /OUT:bin\cui_textarea.exe !OBJS! build\obj\textarea.obj %LIBS% || GOTO :err

ECHO.
FOR %%E IN (bin\cui_demo.exe bin\cui_minimal.exe bin\cui_advanced.exe bin\cui_multi.exe bin\cui_mvvm.exe bin\cui_scroll.exe bin\cui_tabs.exe bin\cui_icons.exe bin\cui_controls.exe bin\cui_overlays.exe bin\cui_textarea.exe) DO (
    dumpbin /imports %%E | findstr /i "msvcrt ucrtbase api-ms-win-crt" >NUL
    IF ERRORLEVEL 1 (
        ECHO [OK] no CRT dependency: %%E
    ) ELSE (
        ECHO [cui] CRT dependency detected - 0CRT build broken: %%E
        GOTO :err
    )
)
ECHO === BUILD OK ===
FOR %%I IN (bin\cui_demo.exe bin\cui_minimal.exe bin\cui_advanced.exe bin\cui_multi.exe bin\cui_mvvm.exe bin\cui_scroll.exe bin\cui_tabs.exe bin\cui_icons.exe bin\cui_controls.exe bin\cui_overlays.exe bin\cui_textarea.exe) DO ECHO Output: %%~fI (%%~zI bytes)
EXIT /B 0

:err
ECHO === BUILD FAILED ===
EXIT /B 1
