@echo off
setlocal

:: ────────────────────────────────────────────────────────────
::  ABRIR.bat — Inicia o RXSDR sem recompilar
::  Use este script sempre que quiser reabrir o software
::  sem precisar passar pelo COMPILAR.bat novamente.
:: ────────────────────────────────────────────────────────────

set "EXE_RELEASE=%~dp0project\build\Release\RXSDR.exe"
set "EXE_BUILD=%~dp0project\build\RXSDR.exe"

:: Fecha instância anterior (se houver)
taskkill /F /IM RXSDR.exe >nul 2>&1
timeout /t 1 /nobreak >nul

:: Localiza o executável
if exist "%EXE_RELEASE%" (
    set "EXE=%EXE_RELEASE%"
) else if exist "%EXE_BUILD%" (
    set "EXE=%EXE_BUILD%"
) else (
    echo.
    echo ============================================================
    echo  [ERRO] RXSDR.exe nao encontrado em project\build.
    echo.
    echo  A build principal ainda nao foi gerada ^(ou foi limpa^).
    echo  ^>^>^>  Rode o COMPILAR.bat uma vez para gerar o executavel.  ^<^<^<
    echo  Depois o ABRIR.bat volta a funcionar normalmente.
    echo ============================================================
    echo.
    pause
    exit /b 1
)

echo.
echo Iniciando RXSDR...
echo   %EXE%
echo.
echo O navegador abrira automaticamente em alguns segundos.
echo Use o icone na bandeja do sistema para reabrir a qualquer momento.
echo.

start "" "%EXE%"

:: Pequena pausa para o usuario ler a mensagem
timeout /t 3 /nobreak >nul

endlocal
