@echo off
chcp 65001 >nul
setlocal
title RXSDR Nativo - Publicar release no GitHub
cd /d "%~dp0"

echo ==============================================================
echo   RXSDR Nativo - publicar release no GitHub
echo   (a versao web ficou travada na 1.0.69 e nao e mais publicada)
echo ==============================================================
echo.
echo   A versao vem do NOME do pacote RXSDR_Nativo_x.x.x.zip, e as
echo   novidades vem do CHANGELOG.md. Nada e digitado a mao - foi
echo   assim que a release 1.0.41 acabou anunciando os arquivos
echo   1.0.35 e 1.0.34.
echo.
echo   Antes de rodar: GERAR_PACOTE_NATIVO.bat e o CHANGELOG atualizado.
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
