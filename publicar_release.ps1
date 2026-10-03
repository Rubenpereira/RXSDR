# ============================================================================
#  publicar_release.ps1 - cria (ou corrige) a release do RXSDR no GitHub
#
#  Nada aqui e digitado a mao. O numero da versao vem do NOME do instalador que
#  existe na pasta, e as novidades vem do CHANGELOG.md. Foi assim que a release
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

# ---- 2) acha os instaladores e tira a versao do nome -----------------------
$win = Get-ChildItem "project\installer\output\RXSDR_Setup_*.exe" -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -notmatch '_Win7' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
# RXSDR Nativo (Windows 7 a 11, sem navegador): pacote .zip portatil, com
# numeracao propria (comecou em 1.0.0). Substituiu o instalador "Win7".
$win7 = Get-ChildItem "project-nativo\Output\RXSDR_Nativo_*.zip" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $win) { Erro "Nao achei o instalador do Windows 10/11 em project\installer\output. Rode GERAR_INSTALADOR.bat antes." }

function VersaoDe($arquivo) {
    if ($arquivo.Name -match 'RXSDR_Setup_(\d+\.\d+\.\d+)') { return $Matches[1] }
    return $null
}
$ver = VersaoDe $win
if (-not $ver) { Erro "Nao consegui ler a versao do nome '$($win.Name)'." }

$anexos = @($win.FullName)
if ($win7) {
    # O Nativo tem numeracao propria - nao precisa bater com a do 10/11.
    $anexos += $win7.FullName
} else {
    Info "Aviso: nao achei o pacote do RXSDR Nativo (rode GERAR_PACOTE_NATIVO.bat). A release ira so com o de 10/11."
}

$tag = "v$ver"
Info "Versao detectada: $ver"
foreach ($a in $anexos) { Info "  anexo: $(Split-Path $a -Leaf)" }

# ---- 3) monta o texto: tabela de downloads + trecho do CHANGELOG -----------
if (-not (Test-Path "CHANGELOG.md")) { Erro "CHANGELOG.md nao encontrado." }
$chg = Get-Content "CHANGELOG.md" -Raw -Encoding UTF8

# Pega a secao "## <versao>" ate a proxima "## "
$novidades = $null
$m = [regex]::Match($chg, "(?ms)^##\s+$([regex]::Escape($ver))\s*\r?\n(.*?)(?=^##\s|\z)")
if ($m.Success) {
    $novidades = $m.Groups[1].Value.Trim()
} else {
    Info "Aviso: o CHANGELOG nao tem uma secao '## $ver'. A release ira sem a lista de novidades."
    $novidades = "_Veja o CHANGELOG.md para o historico completo._"
}

$linhas = @()
$linhas += "## Downloads"
$linhas += ""
$linhas += "| Arquivo | Windows |"
$linhas += "| --- | --- |"
$linhas += "| **$($win.Name)** | 10 e 11 |"
if ($win7) { $linhas += "| **$($win7.Name)** | 7 SP1, 8, 10 e 11 - **RXSDR Nativo**: sem navegador e sem instalar (descompacte e rode o RXSDR.exe) |" }
$linhas += ""
if ($win7) { $linhas += "Baixe apenas um dos dois. Ambos já trazem todas as DLLs necessárias. O RXSDR Nativo é o indicado para PCs antigos ou mais fracos." }
else       { $linhas += "O instalador já traz todas as DLLs necessárias." }
$linhas += ""
$linhas += "## Novidades desta versão"
$linhas += ""
$linhas += $novidades
$linhas += ""
$linhas += "Histórico completo em [CHANGELOG.md](https://github.com/Rubenpereira/RXSDR/blob/main/CHANGELOG.md)."
$linhas += ""
$linhas += "## Hardware suportado"
$linhas += ""
$linhas += "RTL-SDR, RTL-TCP e SDRplay. Para SDRplay é preciso instalar à parte a"
$linhas += "[API oficial](https://www.sdrplay.com/api/)."
$linhas += ""
$linhas += "## Instalação"
$linhas += ""
$linhas += "Windows 10/11: baixe, execute e siga o assistente; o painel aparece no navegador. RXSDR Nativo: descompacte a pasta e rode o RXSDR.exe."

$corpo = Join-Path $env:TEMP "rxsdr_release_$ver.md"
# UTF8 sem BOM: com BOM o GitHub mostra um caractere estranho na primeira linha
[IO.File]::WriteAllText($corpo, ($linhas -join "`r`n"), (New-Object Text.UTF8Encoding $false))

Write-Host ""
Write-Host "  ---------------- previa do texto ----------------" -ForegroundColor DarkGray
# -Encoding UTF8 e obrigatorio: sem ele o Windows PowerShell le o arquivo como
# ANSI e a previa mostra "portuguÃªs" no lugar de "portugues". O texto enviado
# esta certo, mas quem ve a previa acha que quebrou.
Get-Content $corpo -Encoding UTF8 | Select-Object -First 14 | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
Write-Host "  ..." -ForegroundColor DarkGray
Write-Host "  ------------------------------------------------" -ForegroundColor DarkGray
Write-Host ""

$ok = Read-Host "  Publicar a release $tag ? [s/N]"
if ($ok -ne 's' -and $ok -ne 'S') { Write-Host "  Cancelado. Nada foi enviado."; exit 0 }

# ---- 4) cria ou atualiza -----------------------------------------------------
# Esta consulta FALHA de proposito quando a release ainda nao existe - e o que
# se quer saber. Mas o script roda com ErrorActionPreference = 'Stop', e no
# PowerShell 5.1 redirecionar a saida de erro de um programa externo faz o texto
# virar um registro de erro; com 'Stop', o script morre ali. Resultado: a
# primeira publicacao de cada versao quebrava com "release not found", que na
# verdade era a resposta certa.
#
# Por isso a preferencia e afrouxada apenas nesta linha, e a existencia e
# decidida pelo codigo de saida, nao pelo texto.
$prefAnterior = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
gh release view $tag 2>&1 | Out-Null
$existe = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prefAnterior

if ($existe) {
    Info "A release $tag ja existe - atualizando o texto e os anexos."
    gh release edit $tag --title "RXSDR $ver" --notes-file $corpo
    if ($LASTEXITCODE -ne 0) { Erro "falha ao atualizar o texto." }
    # --clobber troca o anexo se ja houver um com o mesmo nome
    gh release upload $tag @anexos --clobber
    if ($LASTEXITCODE -ne 0) { Erro "falha ao enviar os anexos." }
    # A versao "Win7" foi substituida pelo RXSDR Nativo: se a release ainda
    # tiver o instalador antigo anexado, ele sai daqui para ninguem baixar.
    # O mesmo para um pacote do Nativo de numero anterior (ex.: 1.0.0 quando sobe o 1.0.1).
    $nomesNovos = $anexos | ForEach-Object { Split-Path $_ -Leaf }
    $antigos = gh release view $tag --json assets -q '.assets[].name' |
               Where-Object { $_ -match '_Win7' -or ($_ -match '^RXSDR_Nativo_' -and $nomesNovos -notcontains $_) }
    foreach ($a in $antigos) {
        Info "Removendo anexo antigo: $a"
        gh release delete-asset $tag $a -y
    }
} else {
    Info "Criando a release $tag."
    gh release create $tag @anexos --title "RXSDR $ver" --notes-file $corpo
    if ($LASTEXITCODE -ne 0) { Erro "falha ao criar a release." }
}

Write-Host ""
Write-Host "  PRONTO: https://github.com/Rubenpereira/RXSDR/releases/tag/$tag" -ForegroundColor Green
Write-Host ""
