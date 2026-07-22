param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1")
}

$env:PYTHONPATH = "$repoRoot\src;$env:PYTHONPATH"
python examples\starter_app.py
