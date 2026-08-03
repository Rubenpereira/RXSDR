@echo off
rem Sem setlocal: preserva PATH do MSVC e QT_ROOT para o build seguinte.

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
pushd "%ROOT%"
call "%ROOT%\setup-msvc-env.bat"
if errorlevel 1 (
  popd
  exit /b 1
)

if not exist "%ROOT%\build" mkdir "%ROOT%\build"

cmake -S "%ROOT%" -B "%ROOT%\build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%"

if errorlevel 1 (
  echo [ERRO] Falha no configure CMake.
  popd
  exit /b 1
)

echo [OK] Configure concluido em "%ROOT%\build".
popd
exit /b 0
