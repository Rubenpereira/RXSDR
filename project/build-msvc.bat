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

REM  O que sai daqui tambem vai para C:\RXSDR\build_msvc.log.
REM  Sem isso, quando a compilacao falha so resta copiar pedaco de tela -
REM  e a mensagem que interessa costuma ter rolado para fora dela.
cmake --build "%ROOT%\build" --config Release > "%ROOT%\..\build_msvc.log" 2>&1
set "RC=%errorlevel%"
type "%ROOT%\..\build_msvc.log"
REM  O "type" acima zera o errorlevel, entao ele e devolvido aqui - senao
REM  o teste seguinte olharia o resultado do type e daria tudo por certo.
cmd /c exit /b %RC%
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
