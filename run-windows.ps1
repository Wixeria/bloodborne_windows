# Native Windows game renderer (-Gpu), CPU diagnostics and Vulkan checks.
[CmdletBinding(DefaultParameterSetName='Game')]
param(
    [Parameter(ParameterSetName='Game')]
    [string]$GameDirectory = $env:BB_GAME_DIR,
    [Parameter(ParameterSetName='Game')]
    [string]$PatchDirectory,
    [Parameter(ParameterSetName='Game')]
    [switch]$BaseOnly,
    [Parameter(ParameterSetName='Game')]
    [string]$ModsConfig,
    [Parameter(Mandatory,ParameterSetName='Boot')][string]$BootImage,
    [Parameter(Mandatory,ParameterSetName='Vulkan')][switch]$VulkanOnly,
    [Parameter(Mandatory,ParameterSetName='Window')][switch]$VulkanWindow,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$UserDirectory,
    [uint32]$Timeout = 10,
    [switch]$StrictImports,
    [switch]$Gpu,
    [switch]$Lto,
    [ValidateSet('off','fsr3','fsr4','fsr411','taa','dlss')][string]$Upscaler,
    [ValidateSet('native','quality','balanced','performance','ultra')][string]$UpscalePreset,
    [Parameter(ParameterSetName='Game')]
    [ValidatePattern('^\d{3,4}x\d{3,4}$')][string]$Resolution,
    [ValidateSet('1280x720','1920x1080','2560x1440','3840x2160')][string]$OutputResolution,
    [ValidateSet('1280x720','1920x1080','2560x1440','3840x2160')][string]$WindowResolution,
    [ValidateSet('windowed','borderless','fullscreen')][string]$WindowMode,
    [ValidateSet('startup','live','auto')][string]$ResolutionMode,
    [ValidateSet('Mailbox','Immediate','Fifo')][string]$PresentMode,
    [ValidateRange(-1,480)][int]$FpsLimit = -1,
    [string]$DlssDirectory,
    [ValidateSet('off','on','boost')][string]$Reflex,
    [ValidateSet('auto','old','new')][string]$MemoryModel,
    [ValidateSet('off','dlssg','dlssg-sm86','menu','menu-sm86')][string]$FrameGeneration,
    [string]$StreamlineDirectory,
    [Parameter(ParameterSetName='Game')]
    [ValidateSet('preserve','60','90','uncap')][string]$FpsPatch = 'preserve',
    [Parameter(ParameterSetName='Game')][switch]$SkipIntro,
    [Parameter(ParameterSetName='Game')][switch]$PrepareOnly,
    [switch]$ShowFps,
    # Native game logs for a GUI session, bypassing PowerShell's stream pipeline.
    [string]$GameLogPrefix,
    [switch]$SkipBuild,
    [switch]$Bootstrap
)
$ErrorActionPreference = 'Stop'
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'out\windows' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot 'out\windows-game' }
if (-not $UserDirectory) { $UserDirectory = Join-Path $PSScriptRoot 'user' }
$portSettingsPath = Join-Path $OutputDirectory 'bbport.ini'
$hadPortSettings = if ($env:BB_CONFIG) { Test-Path -LiteralPath $env:BB_CONFIG -PathType Leaf } else { Test-Path -LiteralPath $portSettingsPath -PathType Leaf }
$previousPath = $env:PATH
$previousWindowMode = $env:BB_WINDOW_MODE
$previousGpuUser = $env:BB_GPU_USER_DIR
$previousConfig = $env:BB_CONFIG
$previousUpscaler = $env:BB_UPSCALER
$previousShowFps = $env:BB_SHOW_FPS
$previousPreset = $env:BB_UPSCALE_PRESET
$previousDlssDirectory = $env:BB_DLSS_DIR
$previousFrameGeneration = $env:BB_FRAME_GENERATION
$previousReflex = $env:BB_REFLEX
$previousMemoryModel = $env:BB_PC_MODEL
$previousStreamlineDirectory = $env:BB_STREAMLINE_DIR
$previousDlssgSm86 = $env:BB_DLSSG_SM86
$previousVblank = $env:BB_VBLANK_HZ
$previousFpsLimit = $env:BB_FPS_LIMIT
$previousRenderResolution = $env:BB_RENDER_RES
$previousOutputResolution = $env:BB_OUTPUT_RES
$previousWindowResolution = $env:BB_WINDOW_RES
$previousDirectMemory = $env:BB_DMEM_MB
$previousPresentMode = $env:BB_PRESENT_MODE
$previousLiveResolution = $env:BB_LIVE_RES
$previousRestartLauncher = $env:BB_WINDOWS_RESTART
try {
    if ($MemoryModel) {
        $env:BB_PC_MODEL = if ($MemoryModel -eq 'old') { '0' } else { '1' }
        Write-Host "Windows memory model: $MemoryModel (auto uses the new model)."
    }
    if ($Reflex) { $env:BB_REFLEX = $Reflex }
    # A prepared runtime can load only Reflex/PCL when FG is off; no SM86 proxy is needed.
    if ($Gpu -and -not $StreamlineDirectory -and (Test-Path -LiteralPath (Join-Path $BuildDirectory 'sl.interposer.dll'))) {
        $env:BB_STREAMLINE_DIR = (Resolve-Path -LiteralPath $BuildDirectory).Path
    } elseif ($Gpu -and $StreamlineDirectory) {
        $env:BB_STREAMLINE_DIR = (Resolve-Path -LiteralPath $StreamlineDirectory).Path
    }
    if ($FrameGeneration) {
        if ($FrameGeneration -eq 'off') { $env:BB_FRAME_GENERATION = 'off'; $env:BB_DLSSG_SM86 = $null }
        else {
            if (-not $Gpu -or -not $SkipBuild) { throw 'Experimental FG requires -Gpu -SkipBuild and a separately prepared runtime; see docs/WINDOWS_FRAME_GENERATION.md.' }
            if (-not $StreamlineDirectory) { $StreamlineDirectory = $BuildDirectory }
            $env:BB_STREAMLINE_DIR = (Resolve-Path -LiteralPath $StreamlineDirectory).Path
            if (-not (Test-Path -LiteralPath (Join-Path $StreamlineDirectory 'sl.interposer.dll'))) { throw 'Prepare the experimental runtime with tools/windows/prepare_dlssg_game.py first.' }
            if ($FrameGeneration.EndsWith('-sm86')) {
                if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory '.bbport-dlssg-game'))) { throw 'SM86 must run from an isolated directory prepared by prepare_dlssg_game.py.' }
                $env:BB_DLSSG_SM86 = '1'
            } else { $env:BB_DLSSG_SM86 = $null }
            $env:BB_FRAME_GENERATION = if ($FrameGeneration.StartsWith('menu')) { 'menu' } else { 'on' }
            if (-not $PresentMode) { $PresentMode = 'Immediate' }
            if ($PresentMode -ne 'Immediate') { throw 'This FG experiment requires -PresentMode Immediate.' }
        }
    }
    if ($OutputResolution) { $env:BB_OUTPUT_RES = $OutputResolution }
    if ($WindowResolution) { $env:BB_WINDOW_RES = $WindowResolution }
    if ($WindowMode) { $env:BB_WINDOW_MODE = $WindowMode.ToLowerInvariant() }
    if ($PresentMode) { $env:BB_PRESENT_MODE = $PresentMode }
    if ($ResolutionMode) { $env:BB_LIVE_RES = @{ startup = '0'; live = '1'; auto = 'auto' }[$ResolutionMode] }
    if ($Resolution) {
        $resolutionParts = $Resolution -split 'x'
        $renderWidth = [int]$resolutionParts[0]
        $renderHeight = [int]$resolutionParts[1]
        if ($renderWidth -lt 320 -or $renderWidth -gt 7680 -or
            $renderHeight -lt 240 -or $renderHeight -gt 4320 -or
            ($renderWidth % 2) -ne 0 -or ($renderHeight % 2) -ne 0) {
            throw '-Resolution needs even dimensions from 320x240 through 7680x4320.'
        }
        $Resolution = "${renderWidth}x${renderHeight}"
        $env:BB_RENDER_RES = $Resolution
    }
    if ($ShowFps) { $env:BB_SHOW_FPS = '1' }
    if ($Upscaler) { $env:BB_UPSCALER = $Upscaler }
    if ($UpscalePreset) {
        $env:BB_UPSCALE_PRESET = [string](@('native','quality','balanced','performance','ultra').IndexOf($UpscalePreset))
    }
    if ($DlssDirectory) { $env:BB_DLSS_DIR = (Resolve-Path -LiteralPath $DlssDirectory).Path }
    $game = $null
    if ($PSCmdlet.ParameterSetName -eq 'Game') {
        if ($BaseOnly -and $PatchDirectory) { throw '-BaseOnly and -PatchDirectory cannot be combined.' }
        $localConfig = Join-Path $PSScriptRoot 'out\windows-local.json'
        if (-not $GameDirectory -and (Test-Path -LiteralPath $localConfig)) {
            $GameDirectory = (Get-Content -LiteralPath $localConfig -Raw | ConvertFrom-Json).gameDirectory
        }
        if (-not $GameDirectory) { $GameDirectory = Join-Path $PSScriptRoot '..\CUSA03173' }
        if (-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'eboot.bin') -PathType Leaf)) {
            throw "No eboot.bin in '$GameDirectory'. Supply -GameDirectory <your decrypted CUSA03173 v1.09 dump>."
        }
        $game = (Resolve-Path -LiteralPath $GameDirectory).Path
        if (Test-Path -LiteralPath $localConfig) {
            $patchPreferences = Get-Content -LiteralPath $localConfig -Raw | ConvertFrom-Json
            if ($patchPreferences.gameDirectory -and [System.IO.Path]::GetFullPath($patchPreferences.gameDirectory) -eq $game) {
                if (-not $PSBoundParameters.ContainsKey('FpsPatch') -and $patchPreferences.fpsPatch) {
                    if ($patchPreferences.fpsPatch -notin @('preserve','60','90','uncap')) { throw 'Invalid fpsPatch in windows-local.json.' }
                    $FpsPatch = $patchPreferences.fpsPatch
                }
                if (-not $hadPortSettings -and -not $PSBoundParameters.ContainsKey('SkipIntro') -and $patchPreferences.skipIntro -eq $true) {
                    $SkipIntro = $true
                }
            }
        }
        if (-not $BaseOnly -and -not $PatchDirectory -and (Test-Path -LiteralPath $localConfig)) {
            $local = Get-Content -LiteralPath $localConfig -Raw | ConvertFrom-Json
            if ($local.gameDirectory -and [System.IO.Path]::GetFullPath($local.gameDirectory) -eq $game) {
                $configuredPatchSfo = if ($local.patchDirectory) {
                    Join-Path $local.patchDirectory 'sce_sys\param.sfo'
                }
                if ($configuredPatchSfo -and (Test-Path -LiteralPath $configuredPatchSfo -PathType Leaf)) {
                    $PatchDirectory = $local.patchDirectory
                } elseif ($local.patchDirectory) {
                    Write-Host "Ignoring configured update directory without sce_sys\param.sfo: $($local.patchDirectory)"
                    Write-Host 'Using the game directory directly (it may already contain the merged update).'
                }
            }
        }
    } elseif ($BootImage) {
        $BootImage = (Resolve-Path -LiteralPath $BootImage).Path
    }
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'build-windows.ps1') -BuildDirectory $BuildDirectory -Media -Gpu:$Gpu -Lto:$Lto -Bootstrap:$Bootstrap
    }
    $probe = Join-Path $BuildDirectory 'bb-probe.exe'
    if (-not (Test-Path -LiteralPath $probe)) { throw 'bb-probe.exe not found; build with .\build-windows.ps1 -Media first.' }
    # Build output contains its runtime DLLs; retain a scoped fallback for older builds.
    $env:PATH = (Join-Path $PSScriptRoot 'out\toolchains\llvm-mingw-20260922-ucrt-x86_64\bin') + ';' +
        (Join-Path $PSScriptRoot 'out\toolchains\SDL3-3.4.18\x86_64-w64-mingw32\bin') + ';' + $previousPath
    if ($VulkanOnly) {
        & $probe --vulkan-only
        exit $LASTEXITCODE
    }
    if ($VulkanWindow) {
        $windowProbe = Join-Path $BuildDirectory 'bb-vulkan-window.exe'
        if (-not (Test-Path -LiteralPath $windowProbe)) { throw 'Native presentation check requires the pinned Vulkan-Headers checkout; run .\build-windows.ps1 -Bootstrap -Media.' }
        & $windowProbe
        exit $LASTEXITCODE
    }
    if ($Gpu) {
        $probe = Join-Path $BuildDirectory 'bb-game.exe'
        if (-not (Test-Path -LiteralPath $probe)) { throw 'bb-game.exe not found; build with .\build-windows.ps1 -Gpu -Bootstrap first.' }
        $env:BB_GPU_USER_DIR = [System.IO.Path]::GetFullPath($UserDirectory)
        if (-not $PSBoundParameters.ContainsKey('Timeout')) { $Timeout = 90 }
    }
    $probeOptions = @('--timeout', "$Timeout")
    if (-not $Gpu) { $probeOptions += '--cpu-only' }
    if ($StrictImports) { $probeOptions += '--strict-imports' }
    if ($game) {
        $bundledPython = Join-Path $PSScriptRoot 'runtime\python\python.exe'
        $python = if ($env:PYTHON) { $env:PYTHON }
                  elseif (Test-Path -LiteralPath $bundledPython -PathType Leaf) { $bundledPython }
                  else { (Get-Command python -ErrorAction Stop).Source }
        New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
        $output = (Resolve-Path -LiteralPath $OutputDirectory).Path
        if ($PatchDirectory) {
            $mount = & $python -X utf8 (Join-Path $PSScriptRoot 'scripts\game_overlay.py') $game $PatchDirectory --out (Join-Path $output 'mounts')
            if ($LASTEXITCODE -ne 0) { throw "Official update overlay failed ($LASTEXITCODE)." }
            $game = (Resolve-Path -LiteralPath $mount).Path
        }
        if ($ModsConfig) {
            $modMount = & $python -X utf8 (Join-Path $PSScriptRoot 'scripts\windows_mods.py') $game --config $ModsConfig --out (Join-Path $output 'mod-mounts')
            if ($LASTEXITCODE -ne 0) { throw "Mod overlay failed ($LASTEXITCODE)." }
            $game = (Resolve-Path -LiteralPath $modMount).Path
        }
        $steps = @('prepare.py', 'link_libc.py', 'link_modules.py', 'content_profile.py')
        foreach ($step in $steps) {
            & $python -X utf8 (Join-Path $PSScriptRoot "scripts\$step") $game --out $output
            if ($LASTEXITCODE -ne 0) { throw "$step failed ($LASTEXITCODE)." }
        }
        # Match optional patches to the detected executable version.
        $analysis = Get-Content -LiteralPath (Join-Path $output 'analysis.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $appVersion = $analysis.sfo.APP_VER
        if (-not $appVersion) { throw 'Game metadata is missing APP_VER.' }
        if (($SkipIntro -or $FpsPatch -ne 'preserve') -and $appVersion -ne '01.09') {
            throw "Skip Intro/FPS++ patches require APP_VER=01.09; detected $appVersion."
        }
        $runtimeSettings = [System.IO.Path]::GetFullPath($(if ($previousConfig) { $previousConfig } else { Join-Path $output 'bbport.ini' }))
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $runtimeSettings) | Out-Null
        if (-not (Test-Path -LiteralPath $runtimeSettings -PathType Leaf)) {
            $legacySettings = Join-Path $output 'diagnostic-settings.ini'
            if (-not $previousConfig -and (Test-Path -LiteralPath $legacySettings -PathType Leaf)) {
                Copy-Item -LiteralPath $legacySettings -Destination $runtimeSettings
                Write-Host "Migrated saved overlay settings to $runtimeSettings."
            } else {
                Set-Content -LiteralPath $runtimeSettings -Value '' -Encoding ASCII
            }
        }
        if ($PSBoundParameters.ContainsKey('SkipIntro')) {
            $settingsLines = @(Get-Content -LiteralPath $runtimeSettings -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '^\s*skip_intro\s*=' })
            $settingsLines += "skip_intro=$([int]$SkipIntro.IsPresent)"
            Set-Content -LiteralPath $runtimeSettings -Value $settingsLines -Encoding ASCII
        } elseif (-not (Select-String -LiteralPath $runtimeSettings -Pattern '^\s*skip_intro\s*=' -Quiet)) {
            Add-Content -LiteralPath $runtimeSettings -Value "skip_intro=$([int]$SkipIntro.IsPresent)" -Encoding ASCII
        }
        if (-not (Select-String -LiteralPath $runtimeSettings -Pattern '^\s*upscaler\s*=' -Quiet)) {
            Add-Content -LiteralPath $runtimeSettings -Value 'upscaler=off' -Encoding ASCII
        }
        $env:BB_CONFIG = $runtimeSettings
        $patchFps = if ($FpsPatch -eq 'preserve') { '30' } else { $FpsPatch }
        if ($FpsPatch -ne 'preserve') {
            # Uncap both the guest and present pacing; 0 vblank selects the existing 480 Hz mode.
            $env:BB_VBLANK_HZ = '0'
            $env:BB_FPS_LIMIT = if ($FpsPatch -eq 'uncap') { '0' } else { $FpsPatch }
        }
        if ($FpsLimit -ge 0) { $env:BB_FPS_LIMIT = [string]$FpsLimit }
        if ($Gpu) {
            $env:BB_WINDOWS_RESTART = '1'
        }
        Write-Host "Game metadata: $($analysis.sfo.TITLE_ID), APP_VER=$appVersion; FPS patch=$FpsPatch; settings=$runtimeSettings."
        $basePatchArguments = @('--out', $output, '--fps', $patchFps, '--game-dir', $game, '--app-version', $appVersion, '--settings', $runtimeSettings)
        $requestedRender = if ($Resolution) { $Resolution } else { $previousRenderResolution }
        $requestedOutput = if ($OutputResolution) { $OutputResolution } else { $previousOutputResolution }
        function Update-GamePatches {
            $planArguments = @('--print-launch-plan', '--settings', $runtimeSettings)
            if ($requestedRender) { $planArguments += @('--render-res', $requestedRender) }
            if ($requestedOutput) { $planArguments += @('--output-res', $requestedOutput) }
            $planJson = & $python -X utf8 (Join-Path $PSScriptRoot 'scripts\patches.py') @planArguments
            if ($LASTEXITCODE -ne 0) { throw "Resolution planning failed ($LASTEXITCODE)." }
            $plan = $planJson | ConvertFrom-Json
            $env:BB_RENDER_RES = if ($plan.render) { $plan.render } else { $null }
            # Live sessions read output_res from the overlay every frame. Only an
            # explicit output override remains fixed; automatic startup sizes are recomputed.
            $env:BB_OUTPUT_RES = if ($plan.mode -eq 'startup') { $plan.output } else { $requestedOutput }
            $env:BB_DMEM_MB = if ($previousDirectMemory) { $previousDirectMemory } elseif ($plan.direct_memory_mb) { [string]$plan.direct_memory_mb } else { $null }
            Write-Host "Resolution plan: $($plan.mode); scene=$(if ($plan.render) { $plan.render } else { 'dynamic' }); output=$($plan.output)."
            $patchArguments = @($basePatchArguments)
            if ($plan.render) { $patchArguments += @('--render-res', $plan.render) }
            & $python -X utf8 (Join-Path $PSScriptRoot 'scripts\patches.py') @patchArguments
            if ($LASTEXITCODE -ne 0) { throw "patches.py failed ($LASTEXITCODE)." }
        }
        Update-GamePatches
        if ($PrepareOnly) {
            Write-Host "Prepared $output; source game files unchanged."
            return
        }
        $BootImage = Join-Path $output 'boot-linked.bin'
        $probeOptions += @('--app0', $game, '--user', $UserDirectory,
            '--content-profile', (Join-Path $output 'content.bin'), '--patches', (Join-Path $output 'patches.bin'))
    }
    if ($Gpu) { Write-Host 'Running the native guest with the Windows game renderer.' }
    else { Write-Host 'Running native guest CPU diagnostics. Use -Gpu to enable the game renderer.' }
    $gameAttempt = 0
    while ($true) {
        if ($GameLogPrefix) {
            # Start-Process passes one Windows command line to the executable.
            # Quote each literal argument using the Windows CRT escaping rules.
            $quoted = @(@($BootImage) + $probeOptions | ForEach-Object {
                $value = [regex]::Replace([string]$_, '(\\*)"', '$1$1\"')
                '"' + [regex]::Replace($value, '(\\+)$', '$1$1') + '"'
            })
            $gameAttempt++
            $stdoutLog = "${GameLogPrefix}-${gameAttempt}-stdout.log"
            $stderrLog = "${GameLogPrefix}-${gameAttempt}-stderr.log"
            Write-Host "Native game logs: $stdoutLog; $stderrLog"
            $native = Start-Process -FilePath $probe -ArgumentList ($quoted -join ' ') -NoNewWindow -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
            try {
                # Retain the handle before waiting so ExitCode stays available.
                $null = $native.Handle
                $native.WaitForExit()
                $gameExitCode = $native.ExitCode
            } finally { $native.Dispose() }
        } else {
            & $probe $BootImage @probeOptions
            $gameExitCode = $LASTEXITCODE
        }
        if ($gameExitCode -ne 75) { exit $gameExitCode }
        Write-Host 'Overlay requested a restart; rebuilding runtime patches from saved settings.'
        # The saved overlay choices take precedence over launch-time UI flags on a restart.
        if ($Upscaler) { $env:BB_UPSCALER = $null }
        if ($UpscalePreset) { $env:BB_UPSCALE_PRESET = $null }
        if ($ShowFps) { $env:BB_SHOW_FPS = $null }
        if ($OutputResolution) { $requestedOutput = $null }
        if ($ResolutionMode) { $env:BB_LIVE_RES = $null }
        if ($WindowMode) { $env:BB_WINDOW_MODE = $null }
        if ($game) {
            Update-GamePatches
        }
    }
} finally {
    $env:PATH = $previousPath
    $env:BB_WINDOW_MODE = $previousWindowMode
    $env:BB_GPU_USER_DIR = $previousGpuUser
    $env:BB_CONFIG = $previousConfig
    $env:BB_UPSCALER = $previousUpscaler
    $env:BB_SHOW_FPS = $previousShowFps
    $env:BB_UPSCALE_PRESET = $previousPreset
    $env:BB_DLSS_DIR = $previousDlssDirectory
    $env:BB_FRAME_GENERATION = $previousFrameGeneration
    $env:BB_REFLEX = $previousReflex
    $env:BB_PC_MODEL = $previousMemoryModel
    $env:BB_STREAMLINE_DIR = $previousStreamlineDirectory
    $env:BB_DLSSG_SM86 = $previousDlssgSm86
    $env:BB_VBLANK_HZ = $previousVblank
    $env:BB_FPS_LIMIT = $previousFpsLimit
    $env:BB_RENDER_RES = $previousRenderResolution
    $env:BB_OUTPUT_RES = $previousOutputResolution
    $env:BB_WINDOW_RES = $previousWindowResolution
    $env:BB_DMEM_MB = $previousDirectMemory
    $env:BB_PRESENT_MODE = $previousPresentMode
    $env:BB_LIVE_RES = $previousLiveResolution
    $env:BB_WINDOWS_RESTART = $previousRestartLauncher
}
