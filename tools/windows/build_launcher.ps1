[CmdletBinding()]
param([string]$OutputDirectory, [switch]$Development, [switch]$Test, [switch]$UiValidation, [int]$Jobs = 3)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cargo = (Get-Command cargo.exe -ErrorAction Stop).Source
$manifest = Join-Path $projectRoot 'launcher\rust\Cargo.toml'
$target = Join-Path $projectRoot 'out\windows-launcher-rust-target'
$previousTarget = $env:CARGO_TARGET_DIR
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'out\windows-launcher' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = Join-Path $OutputDirectory 'BloodborneLauncher.exe'
try {
    $env:CARGO_TARGET_DIR = $target
    if ($Test) {
        & $cargo test --manifest-path $manifest --locked --no-default-features --test model -j $Jobs
        if ($LASTEXITCODE -ne 0) { throw "Launcher model tests failed ($LASTEXITCODE)." }
    }
    $cargoArguments = @('build', '--manifest-path', $manifest, '--locked', '-j', "$Jobs")
    if ($UiValidation) { $cargoArguments += @('--features', 'ui-validation') }
    if (-not $Development) { $cargoArguments += '--release' }
    & $cargo @cargoArguments
    if ($LASTEXITCODE -ne 0) { throw "Rust launcher build failed ($LASTEXITCODE)." }
    $profile = if ($Development) { 'debug' } else { 'release' }
    Copy-Item -LiteralPath (Join-Path $target "$profile\BloodborneLauncher.exe") -Destination $output -Force
    Write-Host "Rust GPUI launcher: $output"
} finally {
    $env:CARGO_TARGET_DIR = $previousTarget
}
