# ============================================================
#  increment_version.ps1
#  Le a versao atual do RXSDR.iss, incrementa o patch
#  e salva o arquivo atualizado.
#  Exemplo: 1.0.0 -> 1.0.1 -> 1.0.2 ...
# ============================================================

$issFile = Join-Path $PSScriptRoot "RXSDR.iss"

if (-not (Test-Path $issFile)) {
    Write-Host "ERRO: Arquivo nao encontrado: $issFile"
    exit 1
}

$content = Get-Content $issFile -Raw -Encoding UTF8

# Localiza a linha: #define MyAppVersion   "X.Y.Z"
if ($content -match '(?m)^#define MyAppVersion\s+"(\d+)\.(\d+)\.(\d+)"') {
    $major   = [int]$Matches[1]
    $minor   = [int]$Matches[2]
    $patch   = [int]$Matches[3]
    $oldVer  = "$major.$minor.$patch"
    $newVer  = "$major.$minor.$($patch + 1)"

    # Substitui somente a linha de versao
    $oldLine = "#define MyAppVersion   `"$oldVer`""
    $newLine = "#define MyAppVersion   `"$newVer`""
    $newContent = $content.Replace($oldLine, $newLine)

    # Salva em UTF-8 sem BOM
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($issFile, $newContent, $utf8NoBom)

    Write-Host "Versao atualizada: $oldVer  ->  $newVer"
    exit 0
} else {
    Write-Host "AVISO: Linha '#define MyAppVersion' nao encontrada no formato esperado."
    Write-Host "       Verifique se o arquivo contem: #define MyAppVersion   `"X.Y.Z`""
    exit 1
}
