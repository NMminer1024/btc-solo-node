param(
    [string]$BusId = "",
    [string]$Distribution = "Ubuntu-22.04",
    [string]$MountPoint = "/mnt/sdcard"
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

    if ($MountPoint) {
        $argList += @("-MountPoint", $MountPoint)
    }

    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argList | Out-Null
}

function Get-AutoBusId {
    $usbipd = Get-UsbipdPath
    $lines = & $usbipd list
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
        throw "No USB mass storage candidate found. Use -BusId explicitly."
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

function Mount-InWsl {
    $escapedMountPoint = $MountPoint.Replace("'", "'\"'\"'")
    $mountScript = @'
set -euo pipefail
mount_point='__MOUNT_POINT__'
mkdir -p "$mount_point"
if mountpoint -q "$mount_point"; then
    findmnt -no SOURCE,TARGET,FSTYPE,OPTIONS "$mount_point"
    exit 0
fi
device=$(lsblk -prno NAME,FSTYPE,MOUNTPOINT | awk '
    $1 ~ /^\/dev\/sd[a-z][0-9]+$/ &&
    $2 == "ext4" &&
    $3 != "/" &&
    $3 !~ /^\/mnt\/wsl/ &&
    $3 !~ /^\/home\// &&
    $3 != "__MOUNT_POINT__" {
        print $1
        exit
    }
')
[ -n "$device" ] || {
    echo "No ext4 SD partition candidate found in WSL." >&2
    exit 1
}
mount -o ro "$device" "$mount_point"
findmnt -no SOURCE,TARGET,FSTYPE,OPTIONS "$mount_point"
'@
    $mountScript = $mountScript.Replace("__MOUNT_POINT__", $escapedMountPoint)

    & wsl.exe -d $Distribution -u root -- bash -lc $mountScript
    if ($LASTEXITCODE -ne 0) {
        throw "WSL mount failed."
    }
}

if (-not (Test-IsAdmin)) {
    Write-Host "Relaunching as Administrator..." -ForegroundColor Yellow
    Restart-Elevated
    exit 0
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

Start-Sleep -Seconds 2
Mount-InWsl

Write-Host ""
Write-Host ("Mounted SD card to WSL at {0}" -f $MountPoint) -ForegroundColor Green
