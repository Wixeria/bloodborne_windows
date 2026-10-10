# Local, pinned LLVM-MinGW toolchain; no installer, administrator rights or PATH changes.
[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$Media
)
$ErrorActionPreference = 'Stop'
if (-not $Destination) { $Destination = Join-Path $PSScriptRoot '..\..\out\toolchains' }
$ProgressPreference = 'SilentlyContinue'
$version = '20260922'
$name = "llvm-mingw-$version-ucrt-x86_64.zip"
$release = Invoke-RestMethod "https://api.github.com/repos/mstorsjo/llvm-mingw/releases/tags/$version"
$asset = @($release.assets | Where-Object { $_.name -eq $name })
if ($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:([0-9a-f]{64})$') {
    throw 'The pinned official release has no verifiable SHA-256 asset digest.'
}
$expectedHash = $Matches[1]
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$archive = Join-Path $Destination $name
if (-not (Test-Path -LiteralPath $archive)) {
    Write-Host "Downloading $name from the official LLVM-MinGW release..."
    Invoke-WebRequest $asset[0].browser_download_url -OutFile $archive
}
$actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) { throw "SHA-256 mismatch for $archive. Remove this archive and retry." }
$installed = Join-Path $Destination "llvm-mingw-$version-ucrt-x86_64"
if (-not (Test-Path -LiteralPath (Join-Path $installed 'bin\clang.exe'))) {
    Expand-Archive -LiteralPath $archive -DestinationPath $Destination
}
Write-Host "Verified toolchain: $installed"
if ($Media) {
    $sdlVersion = '3.4.18'
    $sdlName = "SDL3-devel-$sdlVersion-mingw.zip"
    $sdlRelease = Invoke-RestMethod "https://api.github.com/repos/libsdl-org/SDL/releases/tags/release-$sdlVersion"
    $sdlAsset = @($sdlRelease.assets | Where-Object { $_.name -eq $sdlName })
    if ($sdlAsset.Count -ne 1 -or $sdlAsset[0].digest -notmatch '^sha256:([0-9a-f]{64})$') {
        throw 'The pinned official SDL3 release has no verifiable SHA-256 asset digest.'
    }
    $sdlHash = $Matches[1]
    $sdlArchive = Join-Path $Destination $sdlName
    if (-not (Test-Path -LiteralPath $sdlArchive)) {
        Invoke-WebRequest $sdlAsset[0].browser_download_url -OutFile $sdlArchive
    }
    if ((Get-FileHash -LiteralPath $sdlArchive -Algorithm SHA256).Hash -ne $sdlHash) {
        throw "SHA-256 mismatch for $sdlArchive. Remove this archive and retry."
    }
    $sdlInstalled = Join-Path $Destination "SDL3-$sdlVersion"
    if (-not (Test-Path -LiteralPath (Join-Path $sdlInstalled 'x86_64-w64-mingw32\bin\SDL3.dll'))) {
        Expand-Archive -LiteralPath $sdlArchive -DestinationPath $Destination
    }
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\third_party\LibAtrac9\C\src\libatrac9.h'))) {
        & git -C (Join-Path $PSScriptRoot '..\..') submodule update --init third_party/LibAtrac9
        if ($LASTEXITCODE -ne 0) { throw 'ATRAC9 submodule initialization failed.' }
    }
    Write-Host "Verified SDL3: $sdlInstalled"
    $vulkanInstalled = Join-Path $Destination 'Vulkan-Headers'
    $vulkanCommit = '952f776f6573aafbb62ea717d871cd1d6816c387' # Khronos v1.4.309
    if (-not (Test-Path -LiteralPath (Join-Path $vulkanInstalled '.git'))) {
        & git clone --depth 1 --branch v1.4.309 https://github.com/KhronosGroup/Vulkan-Headers.git $vulkanInstalled
        if ($LASTEXITCODE -ne 0) { throw 'Vulkan-Headers checkout failed.' }
    }
    $actualCommit = & git -C $vulkanInstalled rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $vulkanCommit) { throw 'Vulkan-Headers does not match the pinned official commit.' }
    Write-Host "Verified Vulkan-Headers: $vulkanInstalled"
    $fmtInstalled = Join-Path $Destination 'fmt'
    $fmtCommit = '40626af88bd7df9a5fb80be7b25ac85b122d6c21' # fmt 11.2.0
    if (-not (Test-Path -LiteralPath (Join-Path $fmtInstalled '.git'))) {
        & git clone --depth 1 --branch 11.2.0 https://github.com/fmtlib/fmt.git $fmtInstalled
        if ($LASTEXITCODE -ne 0) { throw 'fmt checkout failed.' }
    }
    $actualFmtCommit = & git -C $fmtInstalled rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actualFmtCommit -ne $fmtCommit) { throw 'fmt does not match the pinned official commit.' }
    Write-Host "Verified fmt headers: $fmtInstalled"
}
