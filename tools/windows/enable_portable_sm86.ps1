[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath (Join-Path $root 'package-manifest.json') -PathType Leaf)) {
    throw 'Use this helper inside the extracted portable sharing package.'
}
$source = Join-Path $root 'out\windows'
$proxy = Join-Path $root 'optional\sm86'
$target = Join-Path $root 'out\windows-fg-game'
if ((Test-Path -LiteralPath $target) -and
    -not (Test-Path -LiteralPath (Join-Path $target '.bbport-dlssg-game'))) {
    throw 'Destination already exists without the managed SM86 marker.'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'bb-game.exe')) -or
    -not (Test-Path -LiteralPath (Join-Path $proxy 'version.dll'))) {
    throw 'The portable runtime or optional SM86 files are missing.'
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
Set-Content -LiteralPath (Join-Path $target '.bbport-dlssg-game') -Encoding ASCII -Value 'isolated experimental portable runtime'
Get-ChildItem -LiteralPath $source -File | Where-Object {
    $_.Extension -in @('.exe', '.dll', '.txt')
} | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $target -Force }
Get-ChildItem -LiteralPath $proxy -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $target -Force
}
$path = Join-Path $root 'out\windows-launcher\settings.json'
$prefs = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
$prefs.build = 'out/windows-fg-game'
$prefs.engine = 'menu-sm86'
$prefs.present = 'Immediate'
$temporary = "$path.sm86.tmp"
[System.IO.File]::WriteAllText($temporary, ($prefs | ConvertTo-Json -Depth 20), [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporary -Destination $path -Force
Write-Host 'SM86 runtime ready. Open launcher-windows.cmd, select DLSS and enable frame generation in Graphics or Insert.'
Write-Host 'For the standard/official runtime: set Runtime folder to out/windows and choose Standard Windows or Official NVIDIA FG.'
