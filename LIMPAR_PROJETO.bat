@echo off
REM ============================================================================
REM RXSDR - Limpa o PROJETO (PC) de lixo de desenvolvimento:
REM   - arquivos de backup (*.bak_*)
REM   - scripts de diagnostico/teste desta sessao
REM   - documentos de plano/sessao anterior
REM Mantem: codigo-fonte, build, gerar_cartao, instaladores, licencas,
REM         documentos de arquitetura e os scripts de provisionamento do box.
REM ============================================================================
setlocal
cd /d "%~dp0"
echo ============================================================
echo  Limpando o projeto RXSDR (lixo de desenvolvimento)
echo ============================================================

echo.
echo [1/3] Removendo backups (*.bak_*) em todo o projeto...
del /s /q "%~dp0*.bak_*" 2>nul
del /s /q "%~dp0*.bak" 2>nul

echo [2/3] Removendo scripts de diagnostico/teste desta sessao...
for %%F in (
  diag_dsd_box.sh diag_box_source.sh diag_dsd_live_box.sh diag_stdin_dsd_box.sh
  diag_tetra_runner_box.sh teste_dsd_rtl_box.sh capturar_dsd_box.sh
  verificar_tetra_box.sh corrigir_perm_tetra_box.sh
) do if exist "%~dp0%%F" del /q "%~dp0%%F" & echo    - %%F

echo [3/3] Removendo docs de plano / sessao anterior...
for %%F in (
  PROXIMO_PASSO.txt GUIA_DECODERS_CARTAO.md README_DECODERS_CARTAO.md
  corrigir_decoders.sh corrigir_decoders_tvbox.py diagnostico_tvbox.sh
  scratch_radio_switch.py atualizar_os_fase1.sh atualizar_os_fase2.sh
) do if exist "%~dp0%%F" del /q "%~dp0%%F" & echo    - %%F

echo.
echo ============================================================
echo  Limpeza concluida.
echo  Mantidos: fonte, build, gerar_cartao.*, COMPILAR/GERAR_INSTALADOR,
echo  RXSDR.iss, LICENSE*, docs de arquitetura e scripts do box.
echo ============================================================
pause
endlocal
