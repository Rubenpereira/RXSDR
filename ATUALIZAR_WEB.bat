@echo off
setlocal

echo Copiando arquivos web para os builds...

rem Copia arquivos extras para a pasta web fonte
copy /Y "%~dp0LICENSE.txt" "%~dp0project\web\LICENSE.txt" >nul 2>&1
copy /Y "%~dp0INFO.txt"    "%~dp0project\web\INFO.txt"    >nul 2>&1

rem Build Qt6 (projeto principal)
xcopy /Y /S /I /EXCLUDE:%~dp0project\web_sem_backup.txt "%~dp0project\web" "%~dp0project\build\web" >nul
if errorlevel 1 (
    echo [ERRO] Falha ao copiar para project\build\web
    pause
    exit /b 1
)
echo [OK] project\build\web atualizado

rem Decoders externos (runners Python / .bat) — sincroniza sem recompilar
xcopy /Y /S /I "%~dp0project\decoders" "%~dp0project\build\decoders" >nul 2>&1
echo [OK] project\build\decoders atualizado

rem Build Win7 (sem Qt)
if exist "%~dp0project-win7\build-win7" (
    xcopy /Y /S /I /EXCLUDE:%~dp0project\web_sem_backup.txt "%~dp0project\web" "%~dp0project-win7\build-win7\web" >nul
    if errorlevel 1 (
        echo [AVISO] Falha ao copiar para project-win7\build-win7\web
    ) else (
        echo [OK] project-win7\build-win7\web atualizado
    )
) else (
    echo [AVISO] Pasta Win7 nao encontrada, pulando: project-win7\build-win7
)

echo.
set /p ABRIR="Deseja abrir o RXSDR agora? [S/N]: "
if /I "%ABRIR%"=="S" goto :abrir
if /I "%ABRIR%"=="SIM" goto :abrir
goto :fim

:abrir
if exist "%~dp0project\build\Release\RXSDR.exe" (
    start "" "%~dp0project\build\Release\RXSDR.exe"
) else if exist "%~dp0project\build\RXSDR.exe" (
    start "" "%~dp0project\build\RXSDR.exe"
) else (
    echo [AVISO] Executavel Qt6 nao encontrado.
)

:fim
endlocal
