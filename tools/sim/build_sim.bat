@echo off
rem Builds tools\sim\tvsim.exe: the TV code with stand-ins for the game (debug, asserts on).
setlocal
set "ROOT=%~dp0..\..\"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set "IG=%ROOT%third_party\imgui"
set "SRC=%ROOT%src"
if not exist "%~dp0obj" mkdir "%~dp0obj"
pushd "%~dp0obj"
cl /nologo /Od /Zi /MTd /EHa /std:c++17 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS ^
   /FI"%~dp0sim_shim.h" /I"%SRC%" /I"%IG%" /I"%~dp0." ^
   "%~dp0sim_main.cpp" "%~dp0sim_stubs.cpp" ^
   "%SRC%\tv.cpp" "%SRC%\tv_network.cpp" "%SRC%\tv_ui.cpp" "%SRC%\tv_story.cpp" "%SRC%\tv_paper.cpp" ^
   "%SRC%\tv_library.cpp" "%SRC%\tv_streaming.cpp" "%SRC%\tv_events.cpp" "%SRC%\tv_research.cpp" "%SRC%\tv_writers.cpp" "%SRC%\tv_native.cpp" ^
   "%IG%\imgui.cpp" "%IG%\imgui_draw.cpp" "%IG%\imgui_tables.cpp" "%IG%\imgui_widgets.cpp" ^
   /Fe:"%~dp0tvsim.exe" /link user32.lib dbghelp.lib
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
