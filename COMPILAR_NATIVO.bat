@echo off
chcp 65001 >nul 2>&1
echo.
echo RXSDR Nativo - compilar (roda no Windows 7 ate 11, sem navegador)
echo ==================================================================
echo.
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set VS_PATH=%%i
if not defined VS_PATH (
  echo [ERRO] Visual Studio / Build Tools nao encontrado!
  pause
  exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0project-nativo"
if not exist build mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo [ERRO] CMake falhou & pause & exit /b 1 )
cmake --build . --config Release
if errorlevel 1 ( echo [ERRO] Compilacao falhou & pause & exit /b 1 )

rem --- pasta pronta para usar/levar: RXSDR.exe + DLLs do dongle ---
set PRONTO=%~dp0project-nativo\RXSDR_Nativo
if not exist "%PRONTO%" mkdir "%PRONTO%"
copy /y RXSDR.exe "%PRONTO%\" >nul
for %%d in (librtlsdr.dll rtlsdr.dll libusb-1.0.dll pthreadVC2.dll msvcr100.dll) do (
  if exist "%~dp0project\build\%%d" copy /y "%~dp0project\build\%%d" "%PRONTO%\" >nul
)
if not exist "%PRONTO%\bookmarks.json" if exist "%~dp0bookmarks.json" copy /y "%~dp0bookmarks.json" "%PRONTO%\" >nul
for %%f in (LICENSE.txt LICENSE-DSD.txt) do if exist "%~dp0%%f" copy /y "%~dp0%%f" "%PRONTO%\" >nul
for %%f in (LEIA-ME.txt TERCEIROS.txt) do if exist "%~dp0project-nativo\%%f" copy /y "%~dp0project-nativo\%%f" "%PRONTO%\" >nul
rem --- DMR: dsd-fme e as DLLs dele (lista em project-nativo\dsd_arquivos.txt) ---
if not exist "%PRONTO%\decoders" mkdir "%PRONTO%\decoders"
for /f "usebackq eol=# delims=" %%f in ("%~dp0project-nativo\dsd_arquivos.txt") do (
  if not exist "%PRONTO%\decoders\%%f" if exist "%~dp0project\build\decoders\%%f" copy /y "%~dp0project\build\decoders\%%f" "%PRONTO%\decoders\" >nul
)
rem --- AIS: AIS-catcher em decoders\ais, sozinho com as DLLs x64 dele (e o runtime do VC) ---
if not exist "%PRONTO%\decoders\ais" mkdir "%PRONTO%\decoders\ais"
if exist "%~dp0project\build\decoders\ais\AIS-catcher.exe" xcopy /y /q "%~dp0project\build\decoders\ais\*" "%PRONTO%\decoders\ais\" >nul
for %%d in (msvcp140.dll vcruntime140.dll vcruntime140_1.dll) do (
  if not exist "%PRONTO%\decoders\ais\%%d" if exist "%SystemRoot%\System32\%%d" copy /y "%SystemRoot%\System32\%%d" "%PRONTO%\decoders\ais\" >nul
)
rem --- VDL2: dumpvdl2 compilado no MSYS2 (project-nativo\decoders_corrigidos) ---
if exist "%~dp0project-nativo\decoders_corrigidos\dumpvdl2.exe" copy /y "%~dp0project-nativo\decoders_corrigidos\dumpvdl2.exe" "%PRONTO%\decoders\" >nul
rem --- TETRA: tetra-rx corrigido (WSAStartup - sem ele o TETMON e a voz nao saem) ---
if exist "%~dp0project-nativo\decoders_corrigidos\tetra-rx.exe" copy /y "%~dp0project-nativo\decoders_corrigidos\tetra-rx.exe" "%PRONTO%\decoders\" >nul
rem --- DRM: dream.exe (Dream 2.x de console, xHE-AAC) em decoders\drm, com as DLLs dele ---
if exist "%~dp0project-nativo\decoders_corrigidos\drm\dream.exe" xcopy /y /q /i "%~dp0project-nativo\decoders_corrigidos\drm\*" "%PRONTO%\decoders\drm\" >nul
echo.
echo [OK] Pronto: %PRONTO%
echo      E so copiar essa pasta para qualquer PC e rodar o RXSDR.exe
echo.
if /i not "%1"=="nopause" pause
