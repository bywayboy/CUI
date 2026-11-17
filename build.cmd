@ECHO OFF
REM ============================================================================
REM CUI demo build (MSVC x64, 0CRT, Direct2D/DComp)
REM
REM   build.cmd            增量构建 (只重编有变动的文件)
REM   build.cmd clean      清理对象与产物
REM
REM 构建图在 build.mk (NMAKE)。本脚本只负责: 载入 VS 环境、生成头文件
REM 清单 build\headers.mk、调用 nmake、校验 0CRT 并列出产物。
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

SET "FXC=%WindowsSdkVerBinPath%x64\fxc.exe"
IF NOT EXIST "%FXC%" (
    ECHO [cui] fxc.exe not found - install Windows SDK
    EXIT /B 1
)

IF /I "%~1"=="clean" (
    nmake /NOLOGO /f build.mk clean || GOTO :err
    EXIT /B 0
)

REM 头文件清单: 头文件变动时 nmake 会重编全部对象
SET "HDRS="
FOR /R src %%H IN (*.h *.hpp) DO SET "HDRS=!HDRS! %%H"
> "build\headers.mk" ECHO HEADERS =!HDRS!

nmake /NOLOGO /f build.mk "FXC=%FXC%" || GOTO :err

ECHO.
FOR %%E IN (bin\cui_demo.exe bin\cui_minimal.exe bin\cui_advanced.exe bin\cui_multi.exe bin\cui_mvvm.exe bin\cui_scroll.exe bin\cui_tabs.exe bin\cui_icons.exe bin\cui_controls.exe bin\cui_overlays.exe bin\cui_textarea.exe bin\cui_datepicker.exe bin\cui_net.exe bin\cui_net_test.exe bin\cui_image.exe bin\cui_ws_server.exe bin\cui_gfx_rich_test.exe bin\cui_md_test.exe bin\cui_md_ui_test.exe bin\cui_md_math_test.exe bin\cui_gfx_math_test.exe bin\cui_markdown.exe bin\cui_nav.exe bin\cui_nav_test.exe bin\cui_pagination.exe bin\cui_pagination_test.exe bin\cui_layout_test.exe bin\cui_grid.exe bin\cui_grid_test.exe bin\cui_theme_editor.exe bin\cui_theme_edit_test.exe bin\cui_gallery.exe) DO (
    dumpbin /imports %%E | findstr /i "msvcrt ucrtbase api-ms-win-crt" >NUL
    IF ERRORLEVEL 1 (
        ECHO [OK] no CRT dependency: %%E
    ) ELSE (
        ECHO [cui] CRT dependency detected - 0CRT build broken: %%E
        GOTO :err
    )
)
ECHO === BUILD OK ===
FOR %%I IN (bin\cui_demo.exe bin\cui_minimal.exe bin\cui_advanced.exe bin\cui_multi.exe bin\cui_mvvm.exe bin\cui_scroll.exe bin\cui_tabs.exe bin\cui_icons.exe bin\cui_controls.exe bin\cui_overlays.exe bin\cui_textarea.exe bin\cui_datepicker.exe bin\cui_net.exe bin\cui_net_test.exe bin\cui_image.exe bin\cui_ws_server.exe bin\cui_gfx_rich_test.exe bin\cui_md_test.exe bin\cui_md_ui_test.exe bin\cui_md_math_test.exe bin\cui_gfx_math_test.exe bin\cui_markdown.exe bin\cui_nav.exe bin\cui_nav_test.exe bin\cui_pagination.exe bin\cui_pagination_test.exe bin\cui_layout_test.exe bin\cui_grid.exe bin\cui_grid_test.exe bin\cui_theme_editor.exe bin\cui_theme_edit_test.exe bin\cui_gallery.exe) DO ECHO Output: %%~fI (%%~zI bytes)
EXIT /B 0

:err
ECHO === BUILD FAILED ===
EXIT /B 1
