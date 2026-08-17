@echo off
chcp 65001 >nul
setlocal
title RXSDR - Publicar release no GitHub
cd /d "%~dp0"

echo ==============================================================
echo   RXSDR - publicar release no GitHub
echo ==============================================================
echo.
echo   A versao vem do NOME do instalador que esta na pasta, e as
echo   novidades vem do CHANGELOG.md. Nada e digitado a mao - foi
echo   assim que a release 1.0.41 acabou anunciando os arquivos
echo   1.0.35 e 1.0.34.
echo.
echo   Antes de rodar: gere os instaladores e atualize o CHANGELOG.
echo.

if not exist "publicar_release.ps1" goto SEMSCRIPT

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publicar_release.ps1"
goto FIM

:SEMSCRIPT
echo  [ERRO] Falta o arquivo publicar_release.ps1 nesta pasta.

:FIM
echo.
pause
endlocal
