@echo off
chcp 65001 >nul 2>&1
echo.
echo RXSDR - Compilar versao Windows 7/8/10/11
echo   (sem Qt - CRT estatico + copia de DLLs RTL-SDR)
echo =====================================================
echo.

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set VS_PATH=%%i

if not defined VS_PATH (
  echo [ERRO] Visual Studio / Build Tools nao encontrado!
  echo Instale o Visual Studio 2019/2022 ou Build Tools com componente C++.
  pause
  exit /b 1
)

echo Usando: %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d "%~dp0project-win7"

if not exist build-win7 mkdir build-win7
cd build-win7

echo Configurando CMake para Win7 (sem Qt)...
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if errorlevel 1 (
  echo [ERRO] CMake configure falhou!
  pause
  exit /b 1
)

echo Compilando...
cmake --build . --config Release
if errorlevel 1 (
  echo [ERRO] Compilacao falhou!
  pause
  exit /b 1
)

echo.
echo [OK] Compilacao concluida!
echo Executavel: %~dp0project-win7\build-win7\RXSDR.exe
echo.

REM =========================================================
REM  Copia DLLs RTL-SDR e SDRplay do build Qt6 para build-win7
REM  O instalador .iss ja aponta para project\build\ diretamente,
REM  mas copiar aqui deixa o build-win7 completo e auto-suficiente.
REM =========================================================

set QTBUILD=%~dp0project\build
set WIN7BUILD=%~dp0project-win7\build-win7

if not exist "%QTBUILD%\librtlsdr.dll" (
  echo [AVISO] DLLs RTL-SDR nao encontradas em project\build\
  echo   Execute COMPILAR.bat antes para gerar o build Qt6 com as DLLs.
  echo   O instalador Win7 usara o caminho direto do .iss mesmo assim.
  echo.
  goto :fim
)

echo Copiando DLLs RTL-SDR para build-win7...
for %%f in (librtlsdr.dll rtlsdr.dll libusb-1.0.dll pthreadVC2.dll msvcr100.dll sdrplay_api.dll) do (
  if exist "%QTBUILD%\%%f" (
    copy /Y "%QTBUILD%\%%f" "%WIN7BUILD%\%%f" >nul
    echo   Copiado: %%f
  ) else (
    echo   [AVISO] Nao encontrado em project\build\: %%f
  )
)

echo [OK] DLLs copiadas para build-win7\
echo.

if exist "%QTBUILD%\decoders" (
  echo Copiando decoders externos para build-win7...
  xcopy /Y /S /I "%QTBUILD%\decoders" "%WIN7BUILD%\decoders" >nul
  echo [OK] Decoders copiados para build-win7\decoders\
) else (
  echo [AVISO] Pasta decoders nao encontrada em project\build\
)
echo.

if exist "%~dp0project\web" (
  echo Copiando arquivos web para build-win7...
  xcopy /Y /S /I "%~dp0project\web" "%WIN7BUILD%\web" >nul
  echo [OK] Arquivos web copiados para build-win7\web\
) else (
  echo [AVISO] Pasta web nao encontrada em project\web\
)
echo.

:fim
echo =====================================================
echo   Build Win7 concluido!
echo   Executavel: %WIN7BUILD%\RXSDR.exe
echo   Execute GERAR_INSTALADOR_WIN7.bat para o instalador.
echo =====================================================
echo.
pause
