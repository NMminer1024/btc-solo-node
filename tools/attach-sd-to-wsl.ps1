param(
    [string]$BusId = "",
    [string]$Distribution = "",
    [switch]$ListOnly
)

$ErrorActionPreference = "Stop"

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-UsbipdPath {
    $paths = @(
        "C:\Program Files\usbipd-win\usbipd.exe",
        "usbipd.exe"
    )

    foreach ($path in $paths) {
        if ($path -eq "usbipd.exe") {
            $cmd = Get-Command $path -ErrorAction SilentlyContinue
            if ($cmd) {
                return $cmd.Source
            }
        } elseif (Test-Path $path) {
            return $path
        }
    }

    throw "usbipd.exe not found. Install usbipd-win first."
}

function Restart-Elevated {
    $argList = @(
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`""
    )

    if ($BusId) {
        $argList += @("-BusId", $BusId)
    }

    if ($Distribution) {
        $argList += @("-Distribution", $Distribution)
    }

    if ($ListOnly) {
        $argList += "-ListOnly"
    }

    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argList | Out-Null
}

function Get-UsbipdListText {
    $usbipd = Get-UsbipdPath
    return & $usbipd list
}

function Get-AutoBusId {
    $lines = Get-UsbipdListText
    $candidates = @()

    foreach ($line in $lines) {
        if ($line -match '^(?<busid>\d+-\d+)\s+\S+\s+(?<device>.+?)\s{2,}(?<state>Not shared|Shared|Attached)$') {
            $device = $Matches["device"].Trim()
            if ($device -match 'Mass Storage|Storage|Reader|Card|SD|大容量存储') {
                $candidates += [PSCustomObject]@{
                    BusId  = $Matches["busid"]
                    Device = $device
                    State  = $Matches["state"]
                }
            }
        }
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0].BusId
    }

    if ($candidates.Count -eq 0) {
        throw "No USB mass storage candidate found. Run with -ListOnly or pass -BusId explicitly."
    }

    Write-Host "Multiple USB storage candidates found:" -ForegroundColor Yellow
    $candidates | ForEach-Object {
        Write-Host ("  {0}  {1}  [{2}]" -f $_.BusId, $_.Device, $_.State)
    }
    throw "Please rerun with -BusId <busid>."
}

function Invoke-UsbipdSafe {
    param(
        [string[]]$Arguments,
        [string]$IgnorePattern = ""
    )

    $usbipd = Get-UsbipdPath
    $output = & $usbipd @Arguments 2>&1
    $exitCode = $LASTEXITCODE

    if ($output) {
        $output | ForEach-Object { Write-Host $_ }
    }

    if ($exitCode -ne 0) {
        if ($IgnorePattern -and ($output -join "`n") -match $IgnorePattern) {
            return
        }
        throw ("usbipd {0} failed with exit code {1}" -f ($Arguments -join " "), $exitCode)
    }
}

if (-not (Test-IsAdmin)) {
    Write-Host "Relaunching as Administrator..." -ForegroundColor Yellow
    Restart-Elevated
    exit 0
}

$usbipd = Get-UsbipdPath

if ($ListOnly) {
    & $usbipd list
    exit $LASTEXITCODE
}

if (-not $BusId) {
    $BusId = Get-AutoBusId
}

Write-Host ("Using BUSID: {0}" -f $BusId) -ForegroundColor Cyan

Invoke-UsbipdSafe -Arguments @("bind", "--busid", $BusId, "--force") -IgnorePattern "already shared"

$attachArgs = @("attach", "--wsl", "--busid", $BusId)
if ($Distribution) {
    $attachArgs += @("--distribution", $Distribution)
}
Invoke-UsbipdSafe -Arguments $attachArgs -IgnorePattern "already attached"

Write-Host ""
Write-Host "Device attached to WSL." -ForegroundColor Green
Write-Host "Next step inside WSL:"
Write-Host "  sudo tools/mount-sdcard-in-wsl.sh"
