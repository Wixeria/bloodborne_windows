[CmdletBinding()]
param(
    [string]$Toolchain,
    [string]$BuildDirectory,
    [string]$SDL3Directory,
    [string]$VulkanHeaders,
    [string]$FmtHeaders,
    [string]$StreamlineHeaders,
    [switch]$Bootstrap,
    [switch]$Media,
    [switch]$Gpu,
    [switch]$Lto,
    [ValidateRange(1,64)][int]$Jobs = 3,
    [switch]$Test,
    [switch]$TestVulkan
)
$ErrorActionPreference = 'Stop'
if (-not $Toolchain) { $Toolchain = Join-Path $PSScriptRoot 'out\toolchains\llvm-mingw-20260922-ucrt-x86_64' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'out\windows' }
if (-not $SDL3Directory) { $SDL3Directory = Join-Path $PSScriptRoot 'out\toolchains\SDL3-3.4.18\x86_64-w64-mingw32' }
if (-not $VulkanHeaders) { $VulkanHeaders = Join-Path $PSScriptRoot 'out\toolchains\Vulkan-Headers' }
if (-not $FmtHeaders) { $FmtHeaders = Join-Path $PSScriptRoot 'out\toolchains\fmt' }
if ($Gpu) { $Media = $true }
if ($TestVulkan -and -not $Media) { throw '-TestVulkan requires -Media.' }
if ($Bootstrap) { & (Join-Path $PSScriptRoot 'tools\windows\bootstrap.ps1') -Media:$Media }
if ($Bootstrap -and $Gpu) { & (Join-Path $PSScriptRoot 'tools\windows\bootstrap-gpu.ps1') }
$compiler = Join-Path $Toolchain 'bin\clang.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw 'LLVM-MinGW not found. Run .\build-windows.ps1 -Bootstrap -Test or supply -Toolchain <directory>.'
}
$target = & $compiler -dumpmachine
if ($LASTEXITCODE -ne 0 -or $target -notmatch '^x86_64-.*(mingw|windows-gnu)') {
    throw "An x86_64 Windows GNU toolchain is required; found '$target'."
}

function Find-BuildTool([string]$Name, [string]$VisualStudioPath) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $candidates = @(& $vswhere -latest -products '*' -find $VisualStudioPath)
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    throw "$Name not found. Install CMake and Ninja, or the Visual Studio C++ CMake tools."
}
$cmake = Find-BuildTool 'cmake.exe' 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Find-BuildTool 'ninja.exe' 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
if ($Test -and -not (Test-Path -LiteralPath $ctest)) { throw "CTest not found beside $cmake." }
$previousPath = $env:PATH
try {
    # Required by the generated EXEs for libwinpthread-1.dll; scoped to this build.
    $env:PATH = (Join-Path $Toolchain 'bin') + ';' + $env:PATH
    $mediaOptions = @('-DBB_BUILD_MEDIA_RUNTIME=OFF')
    if ($Media) {
        $sdlConfig = Join-Path $SDL3Directory 'lib\cmake\SDL3'
        if (-not (Test-Path -LiteralPath (Join-Path $sdlConfig 'SDL3Config.cmake'))) {
            throw 'SDL3 not found. Run .\build-windows.ps1 -Bootstrap -Media -Test or supply -SDL3Directory <prefix>.'
        }
        $env:PATH = (Join-Path $SDL3Directory 'bin') + ';' + $env:PATH
        $mediaOptions = @('-DBB_BUILD_MEDIA_RUNTIME=ON', "-DSDL3_DIR=$sdlConfig")
    }
    if ($StreamlineHeaders) { $StreamlineHeaders = (Resolve-Path -LiteralPath $StreamlineHeaders).Path }
    & $cmake -S $PSScriptRoot -B $BuildDirectory -G Ninja "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_CXX_COMPILER=$(Join-Path $Toolchain 'bin\clang++.exe')" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DBB_VULKAN_HEADERS=$VulkanHeaders" "-DBB_FMT_HEADERS=$FmtHeaders" "-DBB_STREAMLINE_HEADERS=$StreamlineHeaders" "-DBB_BUILD_GPU=$($Gpu.IsPresent)" "-DBB_LTO=$($Lto.IsPresent)" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON @mediaOptions
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
    & $cmake --build $BuildDirectory --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Windows runtime build failed ($LASTEXITCODE)." }
    # Make the generated executables runnable without a permanent PATH change.
    foreach ($dll in @('libwinpthread-1.dll', 'libc++.dll', 'libunwind.dll')) {
        $source = Join-Path $Toolchain "bin\$dll"
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $BuildDirectory -Force }
    }
    if ($Media) { Copy-Item -LiteralPath (Join-Path $SDL3Directory 'bin\SDL3.dll') -Destination $BuildDirectory -Force }
    if ($Gpu) {
        Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'out\toolchains\ffmpeg-n8.1.3-14-g330caae0c1-win64-lgpl-shared-8.1\bin') -Filter '*.dll' |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $BuildDirectory -Force }
    }
    if ($Test) {
        & $ctest --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Windows runtime tests failed ($LASTEXITCODE)." }
    }
    if ($TestVulkan) {
        & (Join-Path $BuildDirectory 'bb-probe.exe') --vulkan-only
        if ($LASTEXITCODE -ne 0) { throw "Vulkan command/readback test failed ($LASTEXITCODE)." }
        $windowProbe = Join-Path $BuildDirectory 'bb-vulkan-window.exe'
        if (-not (Test-Path -LiteralPath $windowProbe)) { throw 'Vulkan presentation test requires -Bootstrap -Media or -VulkanHeaders <checkout>.' }
        & $windowProbe
        if ($LASTEXITCODE -ne 0) { throw "Vulkan presentation test failed ($LASTEXITCODE)." }
    }
    if ($Gpu) { Write-Host 'Built bb-game.exe with the complete Windows renderer, and bb-probe.exe for CPU diagnostics.' }
    else { Write-Host 'Built the Windows runtime. With -Media, bb-probe.exe provides CPU loader diagnostics; use -Gpu for the full renderer.' }
} finally {
    $env:PATH = $previousPath
}
