@echo off
REM Converts Mixamo FBX downloads into the GLB files the game loads.
REM
REM Put these files in guard-escape\incoming\ first:
REM   student.fbx  guard.fbx  walk.fbx  run.fbx  idle.fbx  (look.fbx optional)
REM Then double-click this file. The game uses the new characters next time it starts;
REM if any file is missing it keeps using the bundled characters.
cd /d "%~dp0.."
set IN=incoming
set OUT=assets\characters
set TOOL=tools\FBX2glTF.exe
set TMP=tools\convert_tmp
if not exist "%TMP%" mkdir "%TMP%"

set MISSING=
for %%F in (student guard walk run idle) do if not exist "%IN%\%%F.fbx" set MISSING=1& echo Missing: %IN%\%%F.fbx
if defined MISSING (
    echo.
    echo Rename your Mixamo downloads to the names above and put them in the incoming folder.
    pause
    exit /b 1
)

call :convert student student
call :convert guard guard
call :convert walk anim_walk
call :convert run anim_run
call :convert idle anim_idle
if exist "%IN%\look.fbx" call :convert look anim_look
if exist "%TMP%" rmdir /s /q "%TMP%"

echo.
echo Done. Start GuardEscape.exe to see the new characters.
pause
exit /b 0

:convert
echo Converting %1.fbx ...
REM raylib reads 4 bone weights per vertex, so keep the 4 strongest
"%TOOL%" --binary --skinning-weights 4 --input "%IN%\%1.fbx" --output "%TMP%\%2" >nul
if not exist "%TMP%\%2.glb" (
    echo   FAILED to convert %1.fbx
    exit /b 1
)
move /y "%TMP%\%2.glb" "%OUT%\%2.glb" >nul
REM merge per-clothing skins into one skeleton (raylib reads only one skin)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\unify_skins.ps1 -Glb "%CD%\%OUT%\%2.glb"
exit /b 0
