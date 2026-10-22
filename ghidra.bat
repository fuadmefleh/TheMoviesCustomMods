@echo off
rem Runs a Ghidra script headless on the analysed MoviesSE.exe copy.
rem Usage: ghidra.bat <Script.java> <args...>   (output paths are relative to re\)
rem Set GPROJ=ghidra_a (a copy of the ghidra folder) to run several at once.
setlocal
set G=%USERPROFILE%\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat
set M=%~dp0
if not defined GPROJ set GPROJ=ghidra
set S=%1
shift
set A=
:loop
if "%~1"=="" goto run
set A=%A% %1
shift
goto loop
:run
call "%G%" "%M%%GPROJ%" MoviesSE -process MoviesSE.exe -noanalysis -readOnly -scriptPath "%M%ghidra_scripts" -postScript %S% %A% > "%M%re\%GPROJ%_last.log" 2>&1
