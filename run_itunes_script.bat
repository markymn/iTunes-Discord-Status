@echo off

start "" /min "PATH TO\pythonw.exe" "PATH TO\itunes_presence.py"

start "" /wait "PATH TO\iTunes.exe"

taskkill /f /im iTunes.exe

taskkill /f /im pythonw.exe

exit