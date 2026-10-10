# Pinned renderer dependencies, installed only inside the workspace.
[CmdletBinding()]
param([string]$Destination)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if (-not $Destination) { $Destination = Join-Path $PSScriptRoot '..\..\out\toolchains' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$repositories = @(
    @('Vulkan-Headers-GPU','KhronosGroup/Vulkan-Headers','v1.4.350','8864cdc896bbc2a9b6eb36b3218fc9ef57908d77'),
    @('magic_enum','Neargye/magic_enum','v0.9.7','e046b69a3736d314fad813e159b1c192eaef92cd'),
    @('robin-map','Tessil/robin-map','v1.3.0','188c45569cc2a5dd768077c193830b51d33a5020'),
    @('VulkanMemoryAllocator','GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator','v3.2.1','c788c52156f3ef7bc7ab769cb03c110a53ac8fcb'),
    @('xxHash','Cyan4973/xxHash','v0.8.3','e626a72bc2321cd320e953a0ccf1584cad60f363'),
    @('xbyak','herumi/xbyak','v7.31','7baceeeabf832258c890ec2c031ff00b4eeac8a4'),
    @('miniz-3.1.2','richgel999/miniz','3.1.2','77d0dce8627735138c51770d1799a1ef48f2117d'),
    @('zydis','zyantific/zydis','v4.1.1','a2278f1d254e492f6a6b39f6cb5d1f5d515659dc'),
    @('glslang','KhronosGroup/glslang','15.2.0','99ee11b0106b48f06fb9e2a324ba928b8316cc33')
)
foreach ($repo in $repositories) {
    $installed = Join-Path $Destination $repo[0]
    if (-not (Test-Path -LiteralPath (Join-Path $installed '.git'))) {
        & git clone --depth 1 --branch $repo[2] "https://github.com/$($repo[1]).git" $installed
        if ($LASTEXITCODE -ne 0) { throw "$($repo[0]) checkout failed." }
    }
    $commit = & git -C $installed rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $commit -ne $repo[3]) { throw "$($repo[0]) does not match its pinned commit." }
    if ($repo[0] -eq 'zydis') {
        & git -C $installed submodule update --init --recursive --depth 1
        if ($LASTEXITCODE -ne 0) { throw 'Zydis dependency checkout failed.' }
    }
    Write-Host "Verified renderer dependency: $($repo[0])"
}
function Get-VerifiedArchive([string]$Name, [string]$Url, [string]$Hash) {
    $archive = Join-Path $Destination $Name
    if (-not (Test-Path -LiteralPath $archive)) {
        & curl.exe -fL --retry 3 --silent --show-error $Url -o $archive
        if ($LASTEXITCODE -ne 0) { throw "$Name download failed." }
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Hash) { throw "SHA-256 mismatch: $archive" }
    return $archive
}
$boost = Get-VerifiedArchive 'boost_1_88_0.tar.gz' 'https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz' '3621533e820dcab1e8012afd583c0c73cf0f77694952b81352bf38c1488f9cb4'
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'boost_1_88_0\boost\version.hpp'))) {
    & tar.exe -xzf $boost -C $Destination 'boost_1_88_0/boost'
    if ($LASTEXITCODE -ne 0) { throw 'Boost extraction failed.' }
}
# FFmpeg itself distributes source; this LGPL shared build is from the Windows
# builder linked by ffmpeg.org/download.html. Pin both the release and SHA-256.
$ffmpegName = 'ffmpeg-n8.1.3-14-g330caae0c1-win64-lgpl-shared-8.1'
$ffmpeg = Get-VerifiedArchive "$ffmpegName.zip" "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-03-18-14/$ffmpegName.zip" '11a4b44bc69721274909619a779d82544c8b83a6557c5b1be92dce9a41a968be'
if (-not (Test-Path -LiteralPath (Join-Path $Destination "$ffmpegName\include\libavcodec\avcodec.h"))) {
    Expand-Archive -LiteralPath $ffmpeg -DestinationPath $Destination
}
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\gpu\third_party\fsr-vulkan\CMakeLists.txt')) -or
    -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\gpu\third_party\imgui\imgui.cpp'))) {
    & git -C (Join-Path $PSScriptRoot '..\..') submodule update --init --recursive gpu/third_party/fsr-vulkan gpu/third_party/imgui
    if ($LASTEXITCODE -ne 0) { throw 'Renderer submodule initialization failed.' }
}
Write-Host 'Verified Boost, FFmpeg and pinned renderer submodules.'
