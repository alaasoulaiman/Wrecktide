# look_shots.ps1 (2026-09-06, lane L2 LOOK): capture the six lighting spots in ONE run.
#
# Why this exists and does not just loop launch_visual.ps1: a -PhotoAt run registers no probe, so
# nothing calls QuitGame and launch_visual.ps1 sits in a 60 s WaitForExit grace after every shot.
# Six spots before plus six after is then 20 minutes of pure waiting. This driver reuses the exact
# same capture path (PrintWindow with PW_RENDERFULLCONTENT on the resolved game-sized window, same
# -game -windowed -resx=1280 -resy=720 command line, so the PNG is the same 1296x759 the measure
# scripts crop) and kills the process the moment the shot is on disk.
#
#   $env:WRECKTIDE_REPO="...\_wt\swim"; powershell -File Tools\look_shots.ps1 -Suffix before
#
# Output: Saved\Captures\look_<spot>_<Suffix>_view1.png (+ _view2.png; WRECKTIDE_LOG_DIR overrides the folder) and one LOOKSHOT line each.
param(
    [string]$Suffix = "before",
    # -PhotoAt teleports on an 8s WORLD timer, and this box needs 20 to 30s of real time to reach
    # BeginPlay, so a fixed wall-clock delay is a coin flip: at 24s all six shots came back as the
    # same PlayerStart frame (upper=0.64 for every spot, teleport had not fired yet). Wait for the
    # WRECKTIDE-PHOTO marker in the log instead, then settle. The log is deleted before each launch so
    # a stale marker cannot fake it, exactly like run_probe.ps1 does.
    [int]$SettleAfterPhotoS = 6,
    [int]$SecondCaptureDelayS = 6,
    [int]$PhotoWaitS = 180,
    [string]$Only = "",              # optional comma list of spot names to shoot
    # One or more console commands to run at startup, comma separated, with ~ standing for a space
    # (the same convention run_probe.ps1 uses for switch values). Example:
    #   -Exec "ShowFlag.SkyLighting~0"
    #   -Exec "r.Water.SingleLayer.Reflection~0,r.Water.WaterMesh.ShowWireframe~1"
    [string]$Exec = ""
)

$ErrorActionPreference = "Stop"
$ue = "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe"
$repo = if ($env:WRECKTIDE_REPO) { $env:WRECKTIDE_REPO } else { Split-Path $PSScriptRoot -Parent }
$proj = "$repo\Wrecktide.uproject"
$logDir = if ($env:WRECKTIDE_LOG_DIR) { $env:WRECKTIDE_LOG_DIR } else { Join-Path $repo "Saved\Captures" }
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$gameLog = "$repo\Saved\Logs\Wrecktide.log"

# The six spots. X,Y,Z,Yaw for -PhotoAt; Z is the tier top from island_dress.ground(x,y) plus the
# 60 uu capsule offset the PlayerStart uses, so the pirate stands rather than falls into frame.
$spots = @(
    @{ name = "camp";  at = "0,-5200,60,90" }      # PlayerStart, looking inland (north)
    @{ name = "pier";  at = "0,-8000,20,90" }      # on the pier deck (top z=-40), looking back at the island
    @{ name = "fort";  at = "2600,2600,100,45" }   # hill tier (top z=+40), looking at the fort
    @{ name = "wreck"; at = "5300,-2400,20,135" }  # beach ring (top z=-40) by the wreck chest
    # Moved 400 uu clear of the graveyard props on 09-06 round 2. At (-3300,100) the camera boom sat
    # INSIDE an unlit prop, so the frame was a black foreground silhouette and luma_measure was
    # reading the prop, not the lighting (34.9% black before AND after any change).
    @{ name = "grave"; at = "-3300,500,60,20" }    # field tier (top z=0), 400 uu clear of the graves
    @{ name = "navy";  at = "-2800,0,60,0" }       # field tier, the west spawner ring
    # SPEC C5 asks for one shot with the camera about 30 uu above the surface looking along it.
    # Water is at Z=-120, so the pawn goes in at -90 and swims; the boom's swim lift does the rest.
    # Measured with water_measure.py, not luma_measure.py.
    @{ name = "sea";   at = "0,-9800,-90,180" }    # open water south of the pier, looking out to sea
)
if ($Only) {
    $keep = $Only.Split(",") | ForEach-Object { $_.Trim() }
    $spots = @($spots | Where-Object { $keep -contains $_.name })
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32CapL {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

function Resolve-GameHwnd([System.Diagnostics.Process]$p, [int]$minWidth = 600, [int]$retries = 20) {
    for ($i = 0; $i -lt $retries; $i++) {
        $p.Refresh()
        $h = $p.MainWindowHandle
        if ($h -ne [IntPtr]::Zero) {
            $r = New-Object Win32CapL+RECT
            [void][Win32CapL]::GetWindowRect($h, [ref]$r)
            if (($r.Right - $r.Left) -ge $minWidth) { return $h }
        }
        Start-Sleep -Seconds 1
    }
    return [IntPtr]::Zero
}

function Capture-Hwnd([IntPtr]$hwnd, [string]$path) {
    $rect = New-Object Win32CapL+RECT
    [void][Win32CapL]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
    if ($w -le 0 -or $h -le 0) { throw "window rect empty ($w x $h)" }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Win32CapL]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $small = New-Object System.Drawing.Bitmap $bmp, 64, 64
    $dark = 0
    for ($x = 0; $x -lt 64; $x++) { for ($y = 0; $y -lt 64; $y++) {
        $p = $small.GetPixel($x, $y)
        if (($p.R + $p.G + $p.B) -lt 45) { $dark++ }
    } }
    $small.Dispose(); $bmp.Dispose()
    return [math]::Round($dark / 4096.0, 3)
}

foreach ($s in $spots) {
    # never run two engines: wait out any other lane's process before launching
    while (@(Get-Process -Name "UnrealEditor", "UnrealEditor-Cmd" -ErrorAction SilentlyContinue).Count -gt 0) {
        Start-Sleep -Seconds 10
    }
    $label = "look_$($s.name)_$Suffix"
    Remove-Item -LiteralPath $gameLog -Force -ErrorAction SilentlyContinue
    $argsLine = "`"$proj`" -game -windowed -resx=1280 -resy=720 -nosplash -PhotoAt=$($s.at)"
    if ($Exec) { $argsLine += " -ExecCmds=`"$($Exec.Replace('~', ' '))`"" }
    $proc = Start-Process -FilePath $ue -ArgumentList $argsLine -PassThru
    $deadline = (Get-Date).AddSeconds(120)
    $hwnd = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        if ($proc.HasExited) { break }
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle; break }
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Output "LOOKSHOT $label VERDICT=NO-WINDOW"
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
        continue
    }
    # block on the teleport marker, not the clock
    $photoDeadline = (Get-Date).AddSeconds($PhotoWaitS)
    $sawPhoto = $false
    while ((Get-Date) -lt $photoDeadline) {
        Start-Sleep -Seconds 2
        if ($proc.HasExited) { break }
        if (Test-Path -LiteralPath $gameLog) {
            $hit = Select-String -LiteralPath $gameLog -Pattern "WRECKTIDE-PHOTO at=" -SimpleMatch -ErrorAction SilentlyContinue
            if ($hit) { $sawPhoto = $true; break }
        }
    }
    if (-not $sawPhoto) {
        Write-Output "LOOKSHOT $label VERDICT=NO-PHOTO-MARKER"
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
        Start-Sleep -Seconds 3
        continue
    }
    Start-Sleep -Seconds $SettleAfterPhotoS
    $p1 = Join-Path $logDir "${label}_view1.png"
    $p2 = Join-Path $logDir "${label}_view2.png"
    $hwnd = Resolve-GameHwnd $proc
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Output "LOOKSHOT $label VERDICT=NO-GAME-SIZED-WINDOW"
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
        continue
    }
    $d1 = Capture-Hwnd $hwnd $p1
    Start-Sleep -Seconds $SecondCaptureDelayS
    $d2 = $d1
    try { if (-not $proc.HasExited) { $d2 = Capture-Hwnd $hwnd $p2 } } catch { Write-Output "NOTE capture2 failed: $_" }
    $verdict = if ($d1 -lt 0.85 -and $d2 -lt 0.85) { "RENDERED" } elseif ($d1 -lt 0.98 -or $d2 -lt 0.98) { "PARTIAL" } else { "BLACK" }
    Write-Output "LOOKSHOT $label at=$($s.at) exec=`"$Exec`" dark1=$d1 dark2=$d2 VERDICT=$verdict $p1"
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Start-Sleep -Seconds 3   # let the process die before the next launch
}
Write-Output "LOOKSHOTS DONE suffix=$Suffix"
