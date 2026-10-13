# Windows Ares capture; refuses existing sessions and stops only its own process.
[CmdletBinding()]
param(
    [string]$Rom = '',
    [ValidateRange(1, 3600)][int]$WallSeconds = 120,
    [ValidatePattern('^[a-zA-Z0-9_-]*$')][string]$Label = '',
    [string]$Ares = 'C:/Users/samue/Documents/ares-v147/ares.exe',
    [string]$OutDir = '',
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
if (-not $Rom) { $Rom = Join-Path $PSScriptRoot '../madeline_cube_rom.z64' }
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '../build' }
if (Get-Process -Name ares -ErrorAction SilentlyContinue) {
    throw 'Ares is already running; refusing a second instance. Close the existing session first.'
}
$romPath = (Resolve-Path -LiteralPath $Rom).ProviderPath
$aresPath = (Resolve-Path -LiteralPath $Ares).ProviderPath
$romHash = (Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($romHash -notmatch '^[a-f0-9]{64}$') { throw 'Invalid artifact SHA-256' }
$romSize = (Get-Item -LiteralPath $romPath).Length
$null = New-Item -ItemType Directory -Force -Path $OutDir
$captureDir = (Resolve-Path -LiteralPath $OutDir).ProviderPath
$suffix = if ($Label) { "-$Label" } else { '' }
$name = "baseline$suffix-$(Get-Date -Format yyyyMMdd-HHmmss)-$PID"
$out = Join-Path $captureDir "$name.txt"
$raw = Join-Path $captureDir "raw-$name.log"
$stderr = Join-Path $captureDir "stderr-$name.log"
Write-Host "[capture_baseline] rom=$romPath"
Write-Host "[capture_baseline] rom_sha256=$romHash size_bytes=$romSize"
Write-Host "[capture_baseline] wall_seconds=$WallSeconds ares=$aresPath"
# Start-Process joins ArgumentList into one Windows command line. Quote the
# observed ROM path explicitly so UNC paths and project names with spaces work.
if ($romPath.Contains('"')) { throw 'ROM path contains an unsupported quote' }
$windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
$ownedProcess = $null
try {
    $ownedProcess = Start-Process -FilePath $aresPath -ArgumentList @('--no-file-prompt', ('"' + $romPath + '"')) -WindowStyle $windowStyle -RedirectStandardOutput $raw -RedirectStandardError $stderr -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($WallSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $ownedProcess.Refresh()
        if ($ownedProcess.HasExited) { Write-Host '[capture_baseline] Ares exited early'; break }
        Start-Sleep -Milliseconds 500
    }
} finally {
    if ($ownedProcess) {
        $ownedProcess.Refresh()
        if (-not $ownedProcess.HasExited) {
            $ownedProcess.Kill()
            $ownedProcess.WaitForExit()
        }
        $ownedProcess.Dispose()
    }
}
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $romHash) {
    throw 'ROM changed during capture; artifact evidence is invalid'
}
# Preserve stderr separately as well as appending it to the raw diagnostic log.
if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr | Add-Content -LiteralPath $raw }
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("[artifact] rom=$romPath sha256=$romHash size_bytes=$romSize")
$lineNumber = 0
Get-Content -LiteralPath $raw | ForEach-Object {
    $lineNumber++
    if ($_ -match '\[(build|init|mappack|map_runtime|matcat|texroom|lvlroom|profiler|render-phases|counters|memory|budget|phys-test|phys|move|climb-window|climb|input-trace|input)\]') {
        $report.Add("${lineNumber}:$_")
    }
}
[System.IO.File]::WriteAllLines($out, $report, [System.Text.UTF8Encoding]::new($false))
Write-Host "[capture_baseline] wrote $out"
Write-Output $out
