@echo off
chcp 65001 >nul 2>&1
rem ---------------------------------------------------------------------------
rem  RXSDR Nativo - gera o pacote PORTATIL (.zip) para publicar
rem  Nao precisa de instalador: quem baixa descompacta e roda o RXSDR.exe.
rem  Antes: rode COMPILAR_NATIVO.bat.
rem ---------------------------------------------------------------------------
cd /d "%~dp0"
set PRONTO=%~dp0project-nativo\RXSDR_Nativo
if not exist "%PRONTO%\RXSDR.exe" (
  echo [ERRO] Nao achei %PRONTO%\RXSDR.exe - rode COMPILAR_NATIVO.bat antes.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$v = (Select-String -Path 'project-nativo\src\ui\Recursos.h' -Pattern 'RXSDR_VERSAO\s+\"([0-9.]+)\"').Matches[0].Groups[1].Value;" ^
  "$tmp = Join-Path $env:TEMP ('RXSDR_Nativo_' + $v); if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force };" ^
  "New-Item -ItemType Directory $tmp | Out-Null;" ^
  "Get-ChildItem 'project-nativo\RXSDR_Nativo' -File | Where-Object { $_.Extension -in '.exe','.dll' } | Copy-Item -Destination $tmp;" ^
  "Copy-Item 'project-nativo\LEIA-ME.txt' $tmp;" ^
  "Copy-Item 'project-nativo\TERCEIROS.txt' $tmp; Copy-Item 'LICENSE.txt' $tmp; Copy-Item 'LICENSE-DSD.txt' $tmp;" ^
  "if (Test-Path 'project-nativo\RXSDR_Nativo\decoders') { Copy-Item 'project-nativo\RXSDR_Nativo\decoders' $tmp -Recurse };" ^
  "if (Test-Path 'bookmarks.json') { Copy-Item 'bookmarks.json' $tmp };" ^
  "New-Item -ItemType Directory 'project-nativo\Output' -Force | Out-Null;" ^
  "$zip = 'project-nativo\Output\RXSDR_Nativo_' + $v + '.zip'; if (Test-Path $zip) { Remove-Item $zip };" ^
  "Compress-Archive -Path $tmp -DestinationPath $zip; Remove-Item $tmp -Recurse -Force;" ^
  "Write-Host ''; Write-Host ('[OK] ' + (Resolve-Path $zip)) -ForegroundColor Green;" ^
  "Get-ChildItem $zip | Select-Object Name, @{n='MB';e={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize"
echo   Sem RXSDR.ini nem run.log dentro: cada um comeca com a sua configuracao.
echo.
if /i not "%1"=="nopause" pause
