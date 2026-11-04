@echo off
REM Windows wrapper for the Amiga build, as SWOS-United's build.cmd does it:
REM the m68k toolchain lives inside the amiga-debug VS Code extension.
REM   build.cmd            build out\SWOS-DLC
REM   build.cmd clean
REM Update AMIGA_BIN if the extension version changes.
if "%AMIGA_BIN%"=="" set AMIGA_BIN=%USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32
set PATH=%AMIGA_BIN%\opt\bin;%AMIGA_BIN%;%PATH%
cd /d "%~dp0"
"%AMIGA_BIN%\gnumake.exe" %*
