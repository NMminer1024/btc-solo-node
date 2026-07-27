param(
    [string]$BusId = ""
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

    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argList | Out-Null
}

function Get-AutoBusId {
    $usbipd = Get-UsbipdPath
    $lines = & $usbipd list
    $candidates = @()

    foreach ($line in $lines) {
        if ($line -match '^(?<busid>\d+-\d+)\s+\S+\s+(?<device>.+?)\s{2,}(?<state>Attached)$') {
            $device = $Matches["device"].Trim()
            if ($device -match 'Mass Storage|Storage|Reader|Card|SD|大容量存储') {
                $candidates += [PSCustomObject]@{
                    BusId  = $Matches["busid"]
                    Device = $device
                }
            }
        }
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0].BusId
    }

    if ($candidates.Count -eq 0) {
        throw "No attached USB storage candidate found."
    }

    Write-Host "Multiple attached USB storage candidates found:" -ForegroundColor Yellow
    $candidates | ForEach-Object {
        Write-Host ("  {0}  {1}" -f $_.BusId, $_.Device)
    }
    throw "Please rerun with -BusId <busid>."
}

if (-not (Test-IsAdmin)) {
    Write-Host "Relaunching as Administrator..." -ForegroundColor Yellow
    Restart-Elevated
    exit 0
}

$usbipd = Get-UsbipdPath

if (-not $BusId) {
    $BusId = Get-AutoBusId
}

Write-Host ("Detaching BUSID: {0}" -f $BusId) -ForegroundColor Cyan
& $usbipd detach --busid $BusId
if ($LASTEXITCODE -ne 0) {
    throw ("usbipd detach failed for {0}" -f $BusId)
}

Write-Host "Device detached from WSL." -ForegroundColor Green
