# Build in Debian, capture with Windows Ares, then apply the existing strict verifier.
[CmdletBinding()]
param(
    [ValidateRange(1, 3600)][int]$WallSeconds = 30,
    [ValidatePattern('^[a-zA-Z0-9_-]*$')][string]$Label = '',
    [ValidateSet('walljump', 'mantle', 'corner')][string]$Profile = 'walljump',
    [string]$Ares = 'C:/Users/samue/Documents/ares-v147/ares.exe',
    [switch]$Fps60,
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name ares -ErrorAction SilentlyContinue) { throw 'Ares is already running; refusing a second instance' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
if ($repo -notmatch '^\\\\wsl(?:\.localhost|\$)\\Debian\\(.+)$') { throw 'Run this helper from the Debian UNC checkout' }
$linuxRoot = '/' + $Matches[1].Replace('\', '/')
function Quote-Bash([string]$value) { $sq = [string][char]39; $dq = [string][char]34; return $sq + $value.Replace($sq, ($sq + $dq + $sq + $dq + $sq)) + $sq }
$target = switch ($Profile) {
    'walljump' { 'physics-autoplay-rom' }
    'mantle' { 'physics-mantle-autoplay-rom' }
    'corner' { 'physics-corner-autoplay-rom' }
}
$romName = switch ($Profile) {
    'walljump' { 'physics_autoplay.z64' }
    'mantle' { 'physics_mantle_autoplay.z64' }
    'corner' { 'physics_corner_autoplay.z64' }
}
if ($Fps60) {
    if ($Profile -ne 'walljump') { throw 'FPS60 benchmark currently uses the walljump route' }
    $target = 'physics-fps60-autoplay-rom'
    $romName = 'physics_fps60_autoplay.z64'
}
if (-not $Label) { $Label = "physics-$Profile-autoplay" }
$buildLog = Join-Path $repo "build/windows-$Label-build.log"
$null = New-Item -ItemType Directory -Force -Path (Join-Path $repo 'build')
$savedErrorPreference = $ErrorActionPreference
try {
    # Windows PowerShell converts native stderr (including GCC warnings) to
    # error records. Judge the build by its exit code while retaining the log.
    $ErrorActionPreference = 'Continue'
    & wsl.exe -d Debian -- bash -lc ("cd " + (Quote-Bash $linuxRoot) + " && ./compile-rom.sh $target") *> $buildLog
    $buildExit = $LASTEXITCODE
} finally { $ErrorActionPreference = $savedErrorPreference }
if ($buildExit -ne 0) { throw "ROM build failed; see $buildLog" }
$romPath = Join-Path $repo $romName
$expectedHash = (Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant()
$capture = & (Join-Path $PSScriptRoot 'capture_baseline_windows.ps1') -Rom $romPath -WallSeconds $WallSeconds -Label $Label -Ares $Ares -Visible:$Visible
$captureName = Split-Path -Leaf $capture
$linuxCapture = "$linuxRoot/build/$captureName"
& wsl.exe -d Debian -- python3 "$linuxRoot/tools/verify_physics_capture.py" $linuxCapture --expected-sha256 $expectedHash --profile $Profile
if ($LASTEXITCODE -ne 0) { throw "Physics capture did not qualify; see $capture" }
