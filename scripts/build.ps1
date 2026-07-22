param(
    [string]$Configuration = "Release",
    [string]$Generator = "Ninja",
    [switch]$SkipPull
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

if (-not $SkipPull) {
    & (Join-Path $PSScriptRoot "pull-webview.ps1")
}

$buildDir = Join-Path $repoRoot "build"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Configuring PyVerse..."
Invoke-Checked { cmake -S . -B $buildDir -G $Generator -D CMAKE_BUILD_TYPE=$Configuration }

Write-Host "Building PyVerse native extension..."
Invoke-Checked { cmake --build $buildDir --config $Configuration }

Write-Host ""
Write-Host "Build complete."
Write-Host "Try it with:"
Write-Host "  python examples\starter_app.py"
