@echo off
chcp 65001 >nul
title RXSDR - Gerando Instalador Win7

echo =====================================================
echo   RXSDR - Gerando Instalador Win7/8/10/11
echo =====================================================
echo.

REM --- Localizar Inno Setup 6 ---
set ISCC_PATH=%PROGRAMFILES(X86)%\Inno Setup 6\ISCC.exe
if not exist "%ISCC_PATH%" set ISCC_PATH=%PROGRAMFILES%\Inno Setup 6\ISCC.exe
if not exist "%ISCC_PATH%" set ISCC_PATH=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe

if not exist "%ISCC_PATH%" (
    echo ERRO: Inno Setup 6 nao encontrado.
    echo Instale em: https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)
echo Inno Setup: %ISCC_PATH%

REM --- Verificar executavel Win7 ---
if not exist "%~dp0project-win7\build-win7\RXSDR.exe" (
    echo ERRO: RXSDR.exe nao encontrado em project-win7\build-win7\
    echo Execute COMPILAR_WIN7.bat primeiro!
    pause
    exit /b 1
)
echo Executavel encontrado: OK

REM --- Verificar DLLs RTL-SDR ---
if not exist "%~dp0project\build\librtlsdr.dll" (
    if not exist "%~dp0project-win7\build-win7\librtlsdr.dll" (
        echo.
        echo AVISO: librtlsdr.dll nao encontrada!
        echo O instalador sera gerado SEM as DLLs RTL-SDR.
        echo Execute COMPILAR.bat antes de COMPILAR_WIN7.bat para incluir as DLLs.
        echo.
        pause
    )
) else (
    echo DLLs RTL-SDR: OK
)

REM --- Incrementar versao ---
echo.
echo Incrementando versao...
powershell -ExecutionPolicy Bypass -File "%~dp0project-win7\installer\increment_version.ps1"
echo.

REM --- Compilar instalador ---
cd /d "%~dp0project-win7\installer"

echo Compilando RXSDR_Win7.iss...
"%ISCC_PATH%" RXSDR_Win7.iss

if errorlevel 1 (
    echo.
    echo =====================================================
    echo   ERRO na compilacao do instalador!
    echo   Verifique as mensagens acima do Inno Setup.
    echo =====================================================
    pause
    exit /b 1
)

echo.
echo =====================================================
echo   SUCESSO!
echo   Instalador gerado em:
echo   %~dp0project-win7\installer\output\
echo =====================================================
explorer "%~dp0project-win7\installer\output"
pause
