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
        "-NoExit",
        "-NoProfile",
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
        if ($line -match '^(?<busid>\d+-\d+)\s+\S+\s+(?<device>.+?)\s{2,}(?<state>Attached)$') {
            $candidates += [PSCustomObject]@{
                BusId  = $Matches["busid"]
                Device = $Matches["device"].Trim()
            }
        }
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0].BusId
    }

    if ($candidates.Count -eq 0) {
        return $null
    }

    Write-Host "Multiple attached USB devices found:" -ForegroundColor Yellow
    $candidates | ForEach-Object {
        Write-Host ("  {0}  {1}" -f $_.BusId, $_.Device)
    }
    throw "Please rerun with -BusId <busid>."
}

function Get-SharedBusIds {
    $usbipd = Get-UsbipdPath
    $lines = & $usbipd list
    $candidates = @()

    foreach ($line in $lines) {
        if ($line -match '^(?<busid>\d+-\d+)\s+\S+\s+(?<device>.+?)\s{2,}(?<state>Shared(?: \(forced\))?)$') {
            $candidates += [PSCustomObject]@{
                BusId  = $Matches["busid"]
                Device = $Matches["device"].Trim()
                State  = $Matches["state"]
            }
        }
    }

    return $candidates
}

function Umount-InWsl {
    $escapedMountPoint = $MountPoint
    $umountCommand = "if mountpoint -q '$escapedMountPoint'; then umount '$escapedMountPoint' && echo 'Unmounted $escapedMountPoint'; else echo 'Not mounted: $escapedMountPoint'; fi"

    & wsl.exe -d $Distribution -u root -- bash -lc $umountCommand
    if ($LASTEXITCODE -ne 0) {
        throw "WSL umount failed."
    }
}

if (-not (Test-IsAdmin)) {
    Write-Host "Relaunching as Administrator..." -ForegroundColor Yellow
    Restart-Elevated
    exit 0
}

Umount-InWsl

$usbipd = Get-UsbipdPath

if (-not $BusId) {
    $BusId = Get-AutoBusId
}

if ($BusId) {
    Write-Host ("Detaching BUSID: {0}" -f $BusId) -ForegroundColor Cyan
    & $usbipd detach --busid $BusId
    if ($LASTEXITCODE -ne 0) {
        throw ("usbipd detach failed for {0}" -f $BusId)
    }
    Write-Host ("Unbinding BUSID: {0}" -f $BusId) -ForegroundColor Cyan
    & $usbipd unbind --busid $BusId
    if ($LASTEXITCODE -ne 0) {
        throw ("usbipd unbind failed for {0}" -f $BusId)
    }
} else {
    Write-Host "No attached USB device found in WSL. The reader may already be back on the Windows side." -ForegroundColor Yellow
    $sharedDevices = @(Get-SharedBusIds)
    if ($sharedDevices.Count -eq 1) {
        $sharedBusId = $sharedDevices[0].BusId
        Write-Host ("Unbinding shared BUSID: {0}" -f $sharedBusId) -ForegroundColor Cyan
        & $usbipd unbind --busid $sharedBusId
        if ($LASTEXITCODE -ne 0) {
            throw ("usbipd unbind failed for {0}" -f $sharedBusId)
        }
    } elseif ($sharedDevices.Count -gt 1) {
        Write-Host "Multiple shared USB devices found:" -ForegroundColor Yellow
        $sharedDevices | ForEach-Object {
            Write-Host ("  {0}  {1}  [{2}]" -f $_.BusId, $_.Device, $_.State)
        }
        throw "Please rerun with -BusId <busid>."
    }
}

Start-Sleep -Seconds 2
Write-Host ""
Write-Host "Current usbipd device list:" -ForegroundColor Cyan
& $usbipd list
Write-Host ""
Write-Host "Opening Windows Disk Management..." -ForegroundColor Cyan
Start-Process "diskmgmt.msc" | Out-Null
Write-Host "If the SD card uses ext4, Explorer may still not assign a drive letter. Disk Management should show the physical disk." -ForegroundColor Yellow
Write-Host "Detached SD card from WSL." -ForegroundColor Green
