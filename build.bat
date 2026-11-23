@echo off
REM Guard Escape build script (needs gcc / MinGW-w64)
REM   build.bat        - builds GuardEscape.exe
REM   build.bat run    - builds and starts the game
REM   build.bat test   - builds and runs the console data-structure tests
cd /d "%~dp0"

if /i "%1"=="test" (
    gcc -std=c99 -O2 -Wall -Wextra tests\test_ds.c src\maze.c src\queue.c src\bfs.c src\bst.c src\astar.c -o test_ds.exe -static || exit /b 1
    .\test_ds.exe
    exit /b %errorlevel%
)

set PROJ=%~dp0
set PROJ=%PROJ:\=/%
gcc -std=c99 -O2 -Wall -Wextra -Ivendor\raylib\include -DPROJECT_DIR="\"%PROJ%\"" ^
    src\main.c src\render3d.c src\retarget.c src\maze.c src\queue.c src\bfs.c src\bst.c src\audio.c src\astar.c ^
    -o GuardEscape.exe -Lvendor\raylib\lib -lraylib -lopengl32 -lgdi32 -lwinmm -static -mwindows || exit /b 1
echo Built GuardEscape.exe

if /i "%1"=="run" start "" .\GuardEscape.exe
