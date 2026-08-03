@echo off
rem Sem setlocal: o call vcvars64 precisa manter PATH/cl/nmake no CMD pai.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Busca manual em caminhos comuns (caso vswhere nao liste nada).
set "VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    goto :vcvars_found
  )
  if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    goto :vcvars_found
  )
)

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat 2^>nul`) do (
    set "VCVARS=%%i"
  )
  if not defined VCVARS (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvars64.bat 2^>nul`) do (
      set "VCVARS=%%i"
    )
  )
)

:vcvars_found
if not defined VCVARS goto :sem_msvc

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [ERRO] Falha ao executar vcvars64.bat
  exit /b 1
)

where cl >nul 2>nul
if errorlevel 1 (
  echo [ERRO] cl.exe nao encontrado apos vcvars64.
  exit /b 1
)
where nmake >nul 2>nul
if errorlevel 1 (
  echo [ERRO] nmake.exe nao encontrado apos vcvars64.
  exit /b 1
)

rem Configuracao salva pelo usuario (CONFIGURAR-QT.bat)
if exist "%~dp0..\qt-root.local.bat" call "%~dp0..\qt-root.local.bat"

if not defined QT_ROOT call :find_qt_msvc

:qt_found
if not defined QT_ROOT (
  echo [ERRO] Qt6 msvc2022_64 nao encontrado.
  echo.
  echo Voce pode ter Qt no MSYS2 ^(C:\msys64^) - isso e MinGW e nao serve aqui.
  echo.
  echo Faca um destes:
  echo   1^) Rode INSTALAR-QT-MSVC.bat  ^(instalador oficial Qt^)
  echo   2^) Rode CONFIGURAR-QT.bat     ^(apontar pasta msvc2022_64^)
  echo   3^) Defina manualmente:
  echo        set QT_ROOT=C:\Qt\6.8.2\msvc2022_64
  exit /b 1
)

if not exist "%QT_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo [ERRO] QT_ROOT invalido: "%QT_ROOT%"
  exit /b 1
)

echo [OK] MSVC pronto: %VCVARS%
echo [OK] QT_ROOT=%QT_ROOT%
exit /b 0

:sem_msvc
echo.
echo [ERRO] Compilador MSVC nao encontrado (vcvars64.bat).
echo.
echo IMPORTANTE:
echo   Visual Studio CODE nao e o compilador C++.
echo   O caminho que voce citou e apenas o editor VS Code.
echo.
echo Voce precisa instalar um destes:
echo   - Visual Studio 2022 Build Tools  (recomendado, mais leve)
echo   - Visual Studio 2022 Community  (com carga C++)
echo.
echo No instalador, marque:
echo   "Desenvolvimento para Desktop com C++"
echo   (Desktop development with C++)
echo.
echo Depois de instalar, rode novamente: COMPILAR.bat
echo.
echo Atalho: execute na raiz do projeto:
echo   INSTALAR-MSVC.bat
echo.
exit /b 1

:find_qt_msvc
for %%D in (C D E F) do (
  if exist "%%D:\Qt\" (
    for /f "delims=" %%v in ('dir /b /ad /o-n "%%D:\Qt\6.*" 2^>nul') do (
      if exist "%%D:\Qt\%%v\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
        set "QT_ROOT=%%D:\Qt\%%v\msvc2022_64"
        goto :eof
      )
      if exist "%%D:\Qt\%%v\msvc2019_64\lib\cmake\Qt6\Qt6Config.cmake" (
        set "QT_ROOT=%%D:\Qt\%%v\msvc2019_64"
        goto :eof
      )
    )
    if exist "%%D:\Qt\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
      set "QT_ROOT=%%D:\Qt\msvc2022_64"
      goto :eof
    )
  )
)
if exist "%LOCALAPPDATA%\Qt\" (
  for /f "delims=" %%v in ('dir /b /ad /o-n "%LOCALAPPDATA%\Qt\6.*" 2^>nul') do (
    if exist "%LOCALAPPDATA%\Qt\%%v\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
      set "QT_ROOT=%LOCALAPPDATA%\Qt\%%v\msvc2022_64"
      goto :eof
    )
  )
)
goto :eof
