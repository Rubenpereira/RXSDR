<#
.SYNOPSIS
Downloads and extracts Direwolf for RXSDR.
#>
$ErrorActionPreference = 'Stop'

# Requires TLS 1.2 for GitHub API
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host "Obtendo a URL do Direwolf mais recente no GitHub..."
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/wb2osz/direwolf/releases/latest"
$asset = $release.assets | Where-Object { $_.name -like "*x86_64.zip" -or $_.name -like "*win-x64.zip" } | Select-Object -First 1

if (-not $asset) {
    Write-Host "Erro: Nao foi possivel encontrar o ZIP do Direwolf para Windows na ultima versao."
    exit 1
}

$Url = $asset.browser_download_url
$ZipFile = $asset.name
$ExtractDir = "direwolf_temp"
$DecodersBuildDir = "..\build\decoders"
$DecodersInstallerDir = "..\installer\decoders"

Write-Host "Baixando Direwolf $($release.tag_name)..."
Invoke-WebRequest -Uri $Url -OutFile $ZipFile

Write-Host "Extraindo $ZipFile..."
if (Test-Path $ExtractDir) { Remove-Item -Recurse -Force $ExtractDir }
Expand-Archive -Path $ZipFile -DestinationPath $ExtractDir -Force

if (-not (Test-Path $DecodersBuildDir)) {
    New-Item -ItemType Directory -Force -Path $DecodersBuildDir | Out-Null
}
if (-not (Test-Path $DecodersInstallerDir)) {
    New-Item -ItemType Directory -Force -Path $DecodersInstallerDir | Out-Null
}

Write-Host "Copiando direwolf.exe e DLLs para decoders..."
# O Direwolf as vezes extrai os arquivos soltos, as vezes dentro de uma pasta.
$exePath = Get-ChildItem -Path $ExtractDir -Filter "direwolf.exe" -Recurse | Select-Object -First 1

if (-not $exePath) {
    Write-Host "Erro: direwolf.exe nao encontrado no ZIP extraido."
    exit 1
}

$srcDir = $exePath.Directory.FullName
Copy-Item $exePath.FullName -Destination $DecodersBuildDir -Force
Copy-Item $exePath.FullName -Destination $DecodersInstallerDir -Force

Get-ChildItem -Path $srcDir -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName -Destination $DecodersBuildDir -Force
    Copy-Item $_.FullName -Destination $DecodersInstallerDir -Force
}

Write-Host "Limpando..."
Remove-Item -Force $ZipFile
Remove-Item -Recurse -Force $ExtractDir

Write-Host "Direwolf instalado com sucesso em $DecodersBuildDir e $DecodersInstallerDir!"
