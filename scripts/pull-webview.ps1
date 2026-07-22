param(
    [string]$Tag = "0.12.0",
    [string]$Destination = "external/webview",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$target = Join-Path $repoRoot $Destination
$parent = Split-Path $target -Parent

New-Item -ItemType Directory -Force -Path $parent | Out-Null

if (Test-Path $target) {
    if (-not $Force) {
        Write-Host "webview already exists at $target"
        Write-Host "Use -Force to replace it."
        exit 0
    }

    Remove-Item -LiteralPath $target -Recurse -Force
}

$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    Write-Host "Cloning webview/webview@$Tag..."
    git clone --depth 1 --branch $Tag https://github.com/webview/webview.git $target
    exit $LASTEXITCODE
}

Write-Host "git was not found; downloading source archive..."
$zipPath = Join-Path ([System.IO.Path]::GetTempPath()) "webview-$Tag.zip"
$extractPath = Join-Path ([System.IO.Path]::GetTempPath()) "webview-$Tag"
$url = "https://codeload.github.com/webview/webview/zip/refs/tags/$Tag"

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
if (Test-Path $extractPath) {
    Remove-Item -LiteralPath $extractPath -Recurse -Force
}

Invoke-WebRequest -Uri $url -OutFile $zipPath
Expand-Archive -Path $zipPath -DestinationPath $extractPath

$expanded = Get-ChildItem -Path $extractPath -Directory | Select-Object -First 1
Move-Item -LiteralPath $expanded.FullName -Destination $target

Write-Host "webview source is ready at $target"
