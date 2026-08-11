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
    $fallbackCandidates = @()

    foreach ($line in $lines) {
        if ($line -match '^(?<busid>\d+-\d+)\s+(?<vidpid>[0-9a-fA-F]{4}:[0-9a-fA-F]{4})\s+(?<device>.+?)\s{2,}(?<state>Not shared|Shared(?: \(forced\))?|Attached)$') {
            $device = $Matches["device"].Trim()
            $candidate = [PSCustomObject]@{
                BusId  = $Matches["busid"]
                VidPid = $Matches["vidpid"]
                Device = $device
                State  = $Matches["state"]
            }

            if ($device -match 'Mass Storage|Storage|Reader|Card|SD|Flash Disk|USB DISK' -or $candidate.VidPid -match '^(05e3:0747)$') {
                $candidates += $candidate
                continue
            }

            if ($device -notmatch 'Bluetooth|Camera|Webcam|Audio|Display|Ethernet|Network|GbE|Controller|Keyboard|Mouse') {
                $fallbackCandidates += $candidate
            }
        }
    }

    $candidates = @($candidates)
    if ($candidates.Count -eq 0) {
        $candidates = @($fallbackCandidates)
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0].BusId
    }

    if ($candidates.Count -eq 0) {
        Write-Host "Current usbipd device list:" -ForegroundColor Cyan
        $lines | ForEach-Object { Write-Host $_ }
        throw "No USB mass storage candidate found. Run with -ListOnly or pass -BusId explicitly."
    }

    Write-Host "Multiple USB storage candidates found:" -ForegroundColor Yellow
    $candidates | ForEach-Object {
        Write-Host ("  {0}  {1}  {2}  [{3}]" -f $_.BusId, $_.VidPid, $_.Device, $_.State)
    }
    throw "Please rerun with -BusId <busid>."
}

function Invoke-UsbipdSafe {
    param(
        [string[]]$Arguments,
        [string]$IgnorePattern = ""
    )

    $usbipd = Get-UsbipdPath
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $usbipd @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }

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

$attachArgs = @("attach", "--wsl")
if ($Distribution) {
    $attachArgs += $Distribution
}
$attachArgs += @("--busid", $BusId)
Invoke-UsbipdSafe -Arguments $attachArgs -IgnorePattern "already attached"

Write-Host ""
Write-Host "Device attached to WSL." -ForegroundColor Green
Write-Host "Next step inside WSL:"
Write-Host "  sudo tools/mount-sdcard-in-wsl.sh"
