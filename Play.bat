@echo off
rem Launch TEMPER windowed (build first; see README).
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0Temper.uproject" -game -ResX=1920 -ResY=1080 -windowed -log
