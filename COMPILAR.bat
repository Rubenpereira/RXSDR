@echo off
setlocal

echo Fechando RXSDR se estiver aberto...
taskkill /F /IM RXSDR.exe >nul 2>&1

echo.
echo Compilando RXSDR...
cd /D "%~dp0project"
call build-msvc.bat
if errorlevel 1 (
    echo.
    echo [ERRO] Ocorreu um problema na compilacao.
    pause
    exit /b 1
)

:: Garante que a pasta web atualizada seja copiada sempre (mesmo se o .exe nao mudar)
echo Copiando arquivos web atualizados...
copy /Y "%~dp0LICENSE.txt" "%~dp0project\web\LICENSE.txt" >nul
copy /Y "%~dp0INFO.txt" "%~dp0project\web\INFO.txt" >nul
xcopy /Y /S /I "%~dp0project\web" "%~dp0project\build\web" >nul

REM As memorias ficam AO LADO do executavel, nao dentro de web: e o usuario
REM quem edita esse arquivo, e ele nao pode ser sobrescrito a cada compilacao.
REM Por isso so copiamos quando ainda nao existe no destino.
if not exist "%~dp0project\build\bookmarks.json" (
    if exist "%~dp0bookmarks.json" copy /Y "%~dp0bookmarks.json" "%~dp0project\build\bookmarks.json" >nul
)

REM  A pasta extio nasce vazia (so o LEIAME): e o usuario quem poe a DLL dele.
REM  Nunca sobrescrevemos o que ja estiver la dentro.
if not exist "%~dp0project\build\extio" mkdir "%~dp0project\build\extio"
copy /Y "%~dp0project\extio\LEIAME.txt" "%~dp0project\build\extio\LEIAME.txt" >nul 2>&1

echo Copiando decoders externos atualizados...
xcopy /Y /S /I "%~dp0project\decoders" "%~dp0project\build\decoders" >nul

echo Copiando dependencias do SDRplay...
if not exist "%~dp0project\build\Release" mkdir "%~dp0project\build\Release" >nul 2>&1
for %%F in (sdrplay_api.dll libusb-1.0.dll) do (
    if exist "%~dp0sdrpp_windows_x64\%%F" (
        copy /Y "%~dp0sdrpp_windows_x64\%%F" "%~dp0project\build\" >nul
        copy /Y "%~dp0sdrpp_windows_x64\%%F" "%~dp0project\build\Release\" >nul
    )
)

echo.
echo [OK] Compilacao concluida com sucesso!
echo.

:: Localiza o executavel gerado
set "EXE_RELEASE=%~dp0project\build\Release\RXSDR.exe"
set "EXE_BUILD=%~dp0project\build\RXSDR.exe"

if exist "%EXE_RELEASE%" (
    set "EXE=%EXE_RELEASE%"
) else if exist "%EXE_BUILD%" (
    set "EXE=%EXE_BUILD%"
) else (
    echo [AVISO] Executavel nao encontrado apos build.
    pause
    exit /b 1
)
echo Executavel: %EXE%
echo.

:: Executa windeployqt para copiar as DLLs do Qt necessárias
call "%~dp0project\setup-msvc-env.bat" >nul 2>&1
if defined QT_ROOT (
    echo Instalando DLLs do Qt junto ao executavel...
    "%QT_ROOT%\bin\windeployqt.exe" "%EXE%" >nul
)
echo.


set /p ABRIR="Deseja abrir o RXSDR agora? [S/N]: "
echo.
if /I "%ABRIR%"=="S"   goto :abrir
if /I "%ABRIR%"=="SIM" goto :abrir
echo Pronto. Para abrir depois, use o ABRIR.bat
goto :fim

:abrir
echo Iniciando RXSDR...
:: Fecha a instancia antiga ANTES de subir a nova. Sem isto, a versao que
:: estava aberta durante a compilacao continua viva e as duas disputam a
:: mesma conexao com o rtl-tcp: o radio liga e cai a cada 4 segundos, sem
:: nenhuma mensagem de erro. O ABRIR.bat ja fazia isso; o COMPILAR.bat nao.
taskkill /F /IM RXSDR.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start "" "%EXE%"
echo O navegador abrira em alguns segundos.
echo Feche esta janela — o software roda em segundo plano (icone na bandeja).
timeout /t 4 /nobreak >nul

:fim
endlocal
