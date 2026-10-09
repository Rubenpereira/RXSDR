# ============================================================================
#  publicar_release.ps1 - cria (ou corrige) a release do RXSDR NATIVO no GitHub
#
#  So o RXSDR Nativo e publicado: a versao web ficou TRAVADA na 1.0.69
#  (09/10/2026) e as releases dela que ja estao no GitHub ficam como estao.
#
#  Nada aqui e digitado a mao. O numero da versao vem do NOME do pacote
#  RXSDR_Nativo_x.x.x.zip que existe na pasta, e as novidades vem do CHANGELOG.md. Foi assim que a release
#  1.0.41 acabou anunciando os arquivos 1.0.35 e 1.0.34: o texto era fixo e
#  ninguem lembrou de trocar.
#
#  Chamado pelo PUBLICAR_RELEASE.bat.
# ============================================================================

$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $raiz

function Erro($msg) { Write-Host ""; Write-Host "  [ERRO] $msg" -ForegroundColor Red; Write-Host ""; exit 1 }
function Info($msg) { Write-Host "  $msg" }

# ---- 1) o gh esta instalado e autenticado? ---------------------------------
# Depois de instalar o gh, a janela do PowerShell que ja estava aberta continua
# com o PATH antigo e nao acha o programa. Em vez de mandar o usuario fechar e
# abrir tudo de novo, recarregamos o PATH aqui e, se ainda assim nao achar,
# procuramos nos lugares onde o instalador costuma por.
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    $env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" +
                [Environment]::GetEnvironmentVariable("Path","User")
}
$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if (-not $gh) {
    $candidatos = @(
        "$env:ProgramFiles\GitHub CLI\gh.exe",
        "${env:ProgramFiles(x86)}\GitHub CLI\gh.exe",
        "$env:LOCALAPPDATA\Programs\GitHub CLI\gh.exe"
    )
    $gh = $candidatos | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $gh) {
    Write-Host ""
    Write-Host "  O GitHub CLI (gh) nao foi encontrado." -ForegroundColor Yellow
    Write-Host "  Instale com:  winget install --id GitHub.cli"
    Write-Host "  Depois FECHE e ABRA o PowerShell e rode:  gh auth login"
    Write-Host ""
    exit 1
}
Set-Alias gh $gh -Scope Script
gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "  Voce ainda nao entrou na sua conta do GitHub." -ForegroundColor Yellow
    Write-Host "  Rode:  gh auth login"
    Write-Host ""
    exit 1
}

# ---- 2) acha o pacote do Nativo e tira a versao do nome ---------------------
$zip = Get-ChildItem "project-nativo\Output\RXSDR_Nativo_*.zip" -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $zip) { Erro "Nao achei o pacote do RXSDR Nativo em project-nativo\Output. Rode GERAR_PACOTE_NATIVO.bat antes." }
if ($zip.Name -notmatch 'RXSDR_Nativo_(\d+\.\d+\.\d+)\.zip') { Erro "Nao consegui ler a versao do nome '$($zip.Name)'." }
$ver = $Matches[1]

# Tag com prefixo proprio: o Nativo tem numeracao propria (1.0.x) e a versao
# web ja usou tags como v1.0.4 la atras - sem o prefixo as duas se misturariam.
$tag = "nativo-v$ver"
$titulo = "RXSDR Nativo $ver"
Info "Versao detectada: $ver  (tag $tag)"
Info "  anexo: $($zip.Name)"

# ---- 3) monta o texto: download + novidades do CHANGELOG ----------------------
# As novidades vem de uma secao "## Nativo <versao>" no CHANGELOG.md (o jeito
# daqui em diante). Se ela nao existir, junta os paragrafos que comecam com
# "RXSDR Nativo <versao>:" ou "RXSDR e RXSDR Nativo <versao>:" (como estava ate
# a 1.0.4, dentro das secoes da versao web).
if (-not (Test-Path "CHANGELOG.md")) { Erro "CHANGELOG.md nao encontrado." }
$chg = Get-Content "CHANGELOG.md" -Raw -Encoding UTF8
$vEsc = [regex]::Escape($ver)

$novidades = $null
$m = [regex]::Match($chg, "(?ms)^##\s+(RXSDR\s+)?Nativo\s+$vEsc\s*\r?\n(.*?)(?=^##\s|\z)")
if ($m.Success) {
    $novidades = $m.Groups[2].Value.Trim()
} else {
    $pars = [regex]::Matches($chg, "(?m)^RXSDR (e RXSDR )?Nativo $vEsc\s*:\s*(.+)$")
    if ($pars.Count -gt 0) {
        $novidades = ($pars | ForEach-Object { "- " + $_.Groups[2].Value.Trim() }) -join "`r`n`r`n"
    } else {
        Info "Aviso: o CHANGELOG nao tem '## Nativo $ver' nem linhas 'RXSDR Nativo $($ver):'. A release ira sem a lista de novidades."
        $novidades = "_Veja o CHANGELOG.md para o hist$([char]0xF3)rico completo._"
    }
}

$linhas = @()
$linhas += "## Download"
$linhas += ""
$linhas += "| Arquivo | Windows |"
$linhas += "| --- | --- |"
$linhas += "| **$($zip.Name)** | 7 SP1, 8, 10 e 11 |"
$linhas += ""
$linhas += "RXSDR Nativo: roda direto no Windows, sem navegador e sem instalar. Descompacte a pasta e rode o RXSDR.exe. Em portugu$([char]0xEA)s e ingl$([char]0xEA)s / Portuguese and English."
$linhas += ""
$linhas += "## Novidades desta vers$([char]0xE3)o"
$linhas += ""
$linhas += $novidades
$linhas += ""
$linhas += "Hist$([char]0xF3)rico completo em [CHANGELOG.md](https://github.com/Rubenpereira/RXSDR/blob/main/CHANGELOG.md)."
$linhas += ""
$linhas += "## Hardware suportado"
$linhas += ""
$linhas += "RTL-SDR, RTL-TCP e SDRplay. Para SDRplay $([char]0xE9) preciso instalar $([char]0xE0) parte a"
$linhas += "[API oficial](https://www.sdrplay.com/api/)."

$corpo = Join-Path $env:TEMP "rxsdr_nativo_release_$ver.md"
# UTF8 sem BOM: com BOM o GitHub mostra um caractere estranho na primeira linha
[IO.File]::WriteAllText($corpo, ($linhas -join "`r`n"), (New-Object Text.UTF8Encoding $false))

Write-Host ""
Write-Host "  ---------------- previa do texto ----------------" -ForegroundColor DarkGray
# -Encoding UTF8 e obrigatorio: sem ele o Windows PowerShell le o arquivo como
# ANSI e a previa mostra os acentos trocados (o texto enviado esta certo).
Get-Content $corpo -Encoding UTF8 | Select-Object -First 16 | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
Write-Host "  ..." -ForegroundColor DarkGray
Write-Host "  ------------------------------------------------" -ForegroundColor DarkGray
Write-Host ""

$ok = Read-Host "  Publicar a release $tag ? [s/N]"
if ($ok -ne 's' -and $ok -ne 'S') { Write-Host "  Cancelado. Nada foi enviado."; exit 0 }

# ---- 4) cria ou atualiza -----------------------------------------------------
# Esta consulta FALHA de proposito quando a release ainda nao existe - e o que
# se quer saber. Com ErrorActionPreference = 'Stop', no PowerShell 5.1 a saida
# de erro de um programa externo derruba o script; por isso a preferencia e
# afrouxada so nesta linha e a existencia e decidida pelo codigo de saida.
$prefAnterior = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
gh release view $tag 2>&1 | Out-Null
$existe = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prefAnterior

if ($existe) {
    Info "A release $tag ja existe - atualizando o texto e o anexo."
    gh release edit $tag --title $titulo --notes-file $corpo
    if ($LASTEXITCODE -ne 0) { Erro "falha ao atualizar o texto." }
    # --clobber troca o anexo se ja houver um com o mesmo nome
    gh release upload $tag $zip.FullName --clobber
    if ($LASTEXITCODE -ne 0) { Erro "falha ao enviar o anexo." }
    # anexo de outro numero que tenha ficado nesta release sai daqui
    $antigos = gh release view $tag --json assets -q '.assets[].name' |
               Where-Object { $_ -ne $zip.Name }
    foreach ($a in $antigos) {
        Info "Removendo anexo antigo: $a"
        gh release delete-asset $tag $a -y
    }
} else {
    Info "Criando a release $tag."
    gh release create $tag $zip.FullName --title $titulo --notes-file $corpo --latest
    if ($LASTEXITCODE -ne 0) { Erro "falha ao criar a release." }
}

Write-Host ""
Write-Host "  PRONTO: https://github.com/Rubenpereira/RXSDR/releases/tag/$tag" -ForegroundColor Green
Write-Host ""
