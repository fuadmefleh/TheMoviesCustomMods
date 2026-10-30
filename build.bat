@echo off
rem Builds the 32-bit proxy dinput8.dll. Pass "install" to copy it into the game folder.
setlocal
set "ROOT=%~dp0"
set "GAME=C:\Program Files (x86)\Lionhead Studios Ltd\The Movies"
rem Any Visual Studio 2019+ with the C++ tools (found with vswhere, so any edition works).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with the C++ tools not found & exit /b 1)
rem vcvars32.bat runs vswhere itself, without a path.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

set "MH=%ROOT%third_party\minhook"
set "IG=%ROOT%third_party\imgui"

if not exist "%ROOT%build" mkdir "%ROOT%build"
pushd "%ROOT%build"
cl /nologo /O2 /MT /EHa /std:c++17 /W3 /LD /utf-8 ^
   /D_CRT_SECURE_NO_WARNINGS ^
   /I"%MH%\include" /I"%IG%" /I"%IG%\backends" ^
   "%ROOT%src\dllmain.cpp" "%ROOT%src\features.cpp" "%ROOT%src\game.cpp" ^
   "%ROOT%src\overlay.cpp" "%ROOT%src\inspector.cpp" "%ROOT%src\persist.cpp" ^
   "%ROOT%src\text.cpp" "%ROOT%src\tv.cpp" "%ROOT%src\tv_network.cpp" "%ROOT%src\tv_ui.cpp" ^
   "%ROOT%src\tv_story.cpp" "%ROOT%src\tv_paper.cpp" "%ROOT%src\tv_library.cpp" "%ROOT%src\tv_streaming.cpp" "%ROOT%src\tv_events.cpp" "%ROOT%src\tv_research.cpp" "%ROOT%src\tv_writers.cpp" "%ROOT%src\tv_shares.cpp" "%ROOT%src\tv_staff.cpp" "%ROOT%src\tv_animation.cpp" "%ROOT%src\tv_mascot.cpp" "%ROOT%src\tv_shorts.cpp" "%ROOT%src\tv_vault.cpp" "%ROOT%src\tv_ceremony.cpp" "%ROOT%src\tv_timeline.cpp" "%ROOT%src\careers.cpp" "%ROOT%src\projectlist.cpp" ^
   "%ROOT%src\diag.cpp" "%ROOT%src\rooms.cpp" "%ROOT%src\ratings.cpp" "%ROOT%src\news.cpp" "%ROOT%src\manager.cpp" "%ROOT%src\selfcheck.cpp" "%ROOT%src\nativeui.cpp" "%ROOT%src\debugshots.cpp" "%ROOT%src\tv_native.cpp" "%ROOT%src\moviemaker.cpp" "%ROOT%src\lists.cpp" "%ROOT%src\buildmenu.cpp" "%ROOT%src\production.cpp" "%ROOT%src\toon.cpp" "%ROOT%src\sandbox.cpp" ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde32.c" ^
   "%IG%\imgui.cpp" "%IG%\imgui_draw.cpp" "%IG%\imgui_tables.cpp" "%IG%\imgui_widgets.cpp" ^
   "%IG%\backends\imgui_impl_dx9.cpp" "%IG%\backends\imgui_impl_win32.cpp" ^
   /Fe:dinput8.dll /link /MAP /DEF:"%ROOT%src\exports.def" user32.lib d3d9.lib dxguid.lib
set ERR=%ERRORLEVEL%
popd
if not %ERR%==0 exit /b %ERR%

if /i "%1"=="install" copy /y "%ROOT%build\dinput8.dll" "%GAME%\dinput8.dll" >nul && echo Installed to %GAME%
if /i "%1"=="install" python "%ROOT%tools\extract_ui.py" "%GAME%"
endlocal
