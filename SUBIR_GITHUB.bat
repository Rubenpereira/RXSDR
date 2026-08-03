@echo off
chcp 65001 >nul
setlocal
title RXSDR - Enviar para o GitHub
cd /d "%~dp0"

set "REPO=RXSDR"
set "CONTA=Rubenpereira"

echo ==============================================================
echo   RXSDR - envio para github.com/%CONTA%/%REPO%
echo ==============================================================
echo.

where git >nul 2>nul
if errorlevel 1 goto SEMGIT

if not exist ".gitignore" goto SEMIGNORE

if not exist ".git" git init -b main
git config user.name  >nul 2>nul || git config user.name  "Ruben Pereira PU1XTB"
git config user.email >nul 2>nul || git config user.email "pu1xtb@gmail.com"

echo  Selecionando arquivos conforme o .gitignore...
git add -A

rem ============================================================
rem  TRAVA DE SEGURANCA - roda ANTES de qualquer envio.
rem  Se algo sensivel entrou na lista, para tudo aqui.
rem ============================================================
echo  Conferindo se nao entrou nada sigiloso...

git diff --cached --name-only > "%TEMP%\rxsdr_lista.txt"

findstr /i /c:"_ssh_agent" "%TEMP%\rxsdr_lista.txt" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE
findstr /i /c:"id_ed25519" "%TEMP%\rxsdr_lista.txt" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE
findstr /i /c:"id_rsa" "%TEMP%\rxsdr_lista.txt" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE
findstr /i /c:".keystore" "%TEMP%\rxsdr_lista.txt" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE

rem Senha de root dos TV box e chaves privadas dentro do CONTEUDO dos
rem arquivos. A senha e montada em duas partes de proposito: assim ela
rem nunca aparece inteira neste .bat, que tambem vai para o GitHub.
rem O ":(exclude)" tira este proprio arquivo da busca - sem isso o
rem verificador encontra os textos que ele mesmo procura e acusa em falso.
set "EU=:(exclude)SUBIR_GITHUB.bat"
set "S1=rcp"
set "S2=mxq"
git grep -I --cached -l -e "%S1%%S2%" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUSENHA
git grep -I --cached -l -e "BEGIN OPENSSH PRIVATE KEY" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE
git grep -I --cached -l -e "BEGIN RSA PRIVATE KEY" -- . "%EU%" >nul 2>nul
if not errorlevel 1 goto ACHOUCHAVE

echo  [OK] Nada sigiloso na lista.
echo.

rem Conta os arquivos e mostra so uma amostra. Sem "more": o paginador
rem trava a tela pedindo tecla a cada pagina, e a lista tem centenas.
for /f %%N in ('git diff --cached --name-only --diff-filter=ACMR ^| find /c /v ""') do set "QTD=%%N"
echo  Serao enviados %QTD% arquivos. Os primeiros:
echo  --------------------------------------------------------------
git diff --cached --name-only --diff-filter=ACMR > "%TEMP%\rxsdr_envio.txt"
set /a LIN=0
for /f "usebackq delims=" %%F in ("%TEMP%\rxsdr_envio.txt") do call :MOSTRA "%%F"
echo  --------------------------------------------------------------
echo  Lista completa: %TEMP%\rxsdr_envio.txt
echo.
set "OK="
set /p "OK=Confirma o envio? [S/N]: "
if /I not "%OK%"=="S" goto CANCELADO

git commit -m "Atualizacao %date% %time%"
git remote remove origin >nul 2>nul
git remote add origin https://github.com/%CONTA%/%REPO%.git

echo.
echo  Enviando...
git push -u origin main
if errorlevel 1 goto FALHAPUSH

echo.
echo ==============================================================
echo  PRONTO! Publicado em:
echo  https://github.com/%CONTA%/%REPO%
echo ==============================================================
goto FIM

rem ---- mostra so as 15 primeiras linhas da lista ----
:MOSTRA
set /a LIN+=1
if %LIN% leq 15 echo    %~1
if %LIN%==16 echo    ... e mais arquivos ^(veja a lista completa no arquivo acima^)
goto :eof

:SEMGIT
echo  ERRO: Git nao encontrado no PATH.
echo  Instale o "Git for Windows": https://git-scm.com/download/win
goto FIM

:SEMIGNORE
echo  ERRO: o arquivo .gitignore nao existe nesta pasta.
echo  Sem ele o envio subiria a pasta inteira, incluindo chaves,
echo  senhas dos TV box e centenas de MB de compilacao.
echo  Envio cancelado por seguranca.
goto FIM

:ACHOUCHAVE
echo.
echo ==============================================================
echo  PAROU POR SEGURANCA
echo  Uma chave ou credencial entrou na lista de envio.
echo  Confira o .gitignore antes de tentar de novo.
echo  Lista completa em: %TEMP%\rxsdr_lista.txt
echo ==============================================================
git reset >nul 2>nul
goto FIM

:ACHOUSENHA
echo.
echo ==============================================================
echo  PAROU POR SEGURANCA
echo  Algum arquivo da lista contem a senha de root dos TV box.
echo  Confira o .gitignore antes de tentar de novo.
echo ==============================================================
git reset >nul 2>nul
goto FIM

:CANCELADO
echo  Envio cancelado por voce. Nada foi enviado.
git reset >nul 2>nul
goto FIM

:FALHAPUSH
echo.
echo  ERRO no envio. Causas comuns:
echo   - o repositorio ainda nao foi criado em https://github.com/new
echo   - o login no navegador foi cancelado
echo   - o repositorio ja tem conteudo diferente; nesse caso rode:
echo       git pull --rebase origin main
echo     e depois este .bat de novo

:FIM
echo.
pause
endlocal
