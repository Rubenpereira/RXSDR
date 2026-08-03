@echo off
echo ============================================
echo   RXSDR - Gerando Instalador
echo ============================================
echo.

REM Caminho do Inno Setup 6
set "ISCC=%PROGRAMFILES(X86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%PROGRAMFILES%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

if not exist "%ISCC%" (
    echo ERRO: Inno Setup 6 nao encontrado.
    echo Instale em: https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)

REM Incrementar versao automaticamente
echo Incrementando versao do instalador...
powershell -ExecutionPolicy Bypass -File "%~dp0project\installer\increment_version.ps1"
if not %ERRORLEVEL% == 0 (
    echo.
    echo AVISO: Nao foi possivel incrementar a versao automaticamente.
    echo.
)
echo.

REM Ir para a pasta do script .iss
cd /d "%~dp0project\installer"

echo Compilando RXSDR.iss ...
"%ISCC%" RXSDR.iss

if %ERRORLEVEL% == 0 (
    echo.
    echo ============================================
    echo   SUCESSO! Instalador gerado em:
    echo   %~dp0project\installer\output\
    echo ============================================
    explorer "%~dp0project\installer\output"
) else (
    echo.
    echo ERRO na compilacao. Verifique as mensagens acima.
)

pause