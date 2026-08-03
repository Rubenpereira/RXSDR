@echo off
rem Sem setlocal: preserva ambiente MSVC/Qt apos configure.
set "NO_PAUSE=%1"

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
call "%ROOT%\configure-msvc.bat"
if errorlevel 1 (
  echo [ERRO] Configure falhou.
  if /I not "%NO_PAUSE%"=="--no-pause" pause
  exit /b 1
)

cmake --build "%ROOT%\build" --config Release
if errorlevel 1 (
  echo [ERRO] Falha no build.
  if /I not "%NO_PAUSE%"=="--no-pause" pause
  exit /b 1
)

echo [OK] Build concluido.
echo Executavel esperado em:
echo   "%ROOT%\build\RXSDR.exe"
echo ou (multi-config):
echo   "%ROOT%\build\Release\RXSDR.exe"
if /I not "%NO_PAUSE%"=="--no-pause" pause
exit /b 0
