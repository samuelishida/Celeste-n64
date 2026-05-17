$ErrorActionPreference = "Stop"

function Escape-BashSingleQuotes {
    param([string]$Value)
    return $Value -replace "'", "'\\''"
}

$repoRoot = (Resolve-Path -Path (Join-Path $PSScriptRoot ".")).Path

$distroList = & wsl -l -v 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "WSL not available. Install WSL and Ubuntu, then retry."
}

$lines = $distroList | ForEach-Object {
    $s = "$($_)"
    $s = $s -replace "`0", ""
    $s = $s -replace "`r", ""
    $s = $s -replace "`n", ""
    $s.Trim()
} | Where-Object { $_ -and $_ -notmatch '^NAME\s+STATE\s+VERSION' }

function Get-DistroNameFromLine {
    param([string]$Line)
    $lineTrimmed = $Line.Trim()
    $parts = $lineTrimmed -split '\s+'
    if ($parts.Count -eq 0) {
        return $null
    }
    if ($parts[0] -eq '*') {
        return $parts[1]
    }
    return $parts[0]
}

$defaultLine = $lines | Where-Object { $_ -match '^\*' } | Select-Object -First 1
$defaultDistro = if ($defaultLine) { Get-DistroNameFromLine $defaultLine } else { $null }

$ubuntuLine = $lines | Where-Object { $_ -match 'Ubuntu' } | Select-Object -First 1
$ubuntuDistro = if ($ubuntuLine) { Get-DistroNameFromLine $ubuntuLine } else { $null }

$firstLine = $lines | Select-Object -First 1
$firstDistro = if ($firstLine) { Get-DistroNameFromLine $firstLine } else { $null }

$distro = if ($ubuntuDistro) { $ubuntuDistro } elseif ($defaultDistro) { $defaultDistro } else { $firstDistro }

if (-not $distro) {
    throw "No WSL distributions found. Install Ubuntu and retry."
}

$linuxPath = & wsl -d $distro -- wslpath -a "$repoRoot" 2>$null
if ($LASTEXITCODE -ne 0 -or -not $linuxPath) {
    $drive = $repoRoot.Substring(0, 1).ToLower()
    $rest = $repoRoot.Substring(2) -replace '\\', '/'
    $linuxPath = "/mnt/$drive$rest"
}
if (-not $linuxPath) {
    throw "Failed to map repo path to WSL path."
}

$argString = ($args | ForEach-Object { "'" + (Escape-BashSingleQuotes $_) + "'" }) -join " "
$cmd = "cd '$linuxPath'; ./compile-rom.sh"
if ($argString) {
    $cmd = "$cmd $argString"
}

& wsl -d $distro -e bash -lc $cmd
exit $LASTEXITCODE
