@echo off
setlocal
set "VNA_ROOT=%~dp0"
echo Starting Vector Network Analyzer
echo Web URL: http://127.0.0.1:8080/
echo Text log: "%VNA_ROOT%logs\vna.log"
echo Structured log: "%VNA_ROOT%logs\vna.jsonl"
"%VNA_ROOT%bin\vna-server.exe"
set "VNA_EXIT_CODE=%ERRORLEVEL%"
if not "%VNA_EXIT_CODE%"=="0" echo ERROR: Vector Network Analyzer exited with code %VNA_EXIT_CODE%. Text log: "%VNA_ROOT%logs\vna.log" 1>&2
exit /b %VNA_EXIT_CODE%
