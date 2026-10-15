# wardrobe_room_capture.ps1 (2026-09-06, lane L5): eyes on the change room.
# Launches the game with the title menu up and persistence SUPPRESSED (-WrecktideNoSave: no load, no
# autosave, and the process is killed before any EndPlay flush, so the player's slot is never touched),
# walks the title rows down to Wardrobe, presses Enter and captures the change room.
# The waits are deliberate: auto exposure is engine default and freezes with the world, so the shot
# needs real rendered frames after the screen opens (RECON F).
#   powershell -ExecutionPolicy Bypass -File Tools\wardrobe_room_capture.ps1 -Label wardrobe2_room
param([string]$Label = "wardrobe2_room", [int]$WaitMs = 45000, [int]$DownPresses = 2)
$repo = if ($env:WRECKTIDE_REPO) { $env:WRECKTIDE_REPO } else { Split-Path $PSScriptRoot -Parent }
$ue = "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe"
$out = if ($env:WRECKTIDE_LOG_DIR) { $env:WRECKTIDE_LOG_DIR } else { Join-Path $repo "Saved\Captures" }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Wrecktide -Name RoomCap -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
[DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
public struct RECT { public int Left, Top, Right, Bottom; }
"@
function Send-Key([byte]$vk, [byte]$scan) {
    [Wrecktide.RoomCap]::keybd_event($vk, $scan, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 90
    [Wrecktide.RoomCap]::keybd_event($vk, $scan, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400
}
function Capture-Now([IntPtr]$hwnd, [string]$path) {
    $rect = New-Object Wrecktide.RoomCap+RECT
    [void][Wrecktide.RoomCap]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
    if ($w -lt 600) { Write-Output "NOTE capture skipped, window ${w}px"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Wrecktide.RoomCap]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "CAPTURED $path"
}

# Engine gate: one heavy Unreal process at a time (the shared rule for this run).
$gateWaited = 0
while ($gateWaited -lt 2400) {
    $busy = @(Get-Process -Name "UnrealEditor", "UnrealEditor-Cmd" -ErrorAction SilentlyContinue)
    if ($busy.Count -eq 0) { break }
    if ($gateWaited -eq 0) { Write-Output ("engine gate: waiting for {0} Unreal process(es)" -f $busy.Count) }
    Start-Sleep -Seconds 10
    $gateWaited += 10
}

$proc = Start-Process $ue -ArgumentList @("`"$repo\Wrecktide.uproject`"", "-game", "-windowed", "-ResX=1280", "-ResY=720", "-nosplash", "-WrecktideNoSave") -PassThru
$deadline = (Get-Date).AddSeconds(60)
while ($proc.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250; $proc.Refresh() }
if ($proc.MainWindowHandle -eq 0) { Write-Output "VERDICT=FAIL (no window)"; $proc.Kill(); exit 1 }
$hwnd = $proc.MainWindowHandle
[void][Wrecktide.RoomCap]::ShowWindow($hwnd, 9); [void][Wrecktide.RoomCap]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds $WaitMs
Capture-Now $hwnd "$out\${Label}_title.png"

# Down to the Wardrobe row (no save file with -WrecktideNoSave, so: New Voyage, Load Voyage, Wardrobe)
for ($i = 0; $i -lt $DownPresses; $i++) { Send-Key 0x28 0x50 }
Capture-Now $hwnd "$out\${Label}_menu.png"
Send-Key 0x0D 0x1C
# The change room is a new screen on a frozen world: give it rendered frames before the shutter.
Start-Sleep -Milliseconds 4000
Capture-Now $hwnd "$out\${Label}_t1.png"
# Right once on nothing yet; Down to the Colourway row is too fragile to guess, so this is the second
# shot of the same framing after more frames, which is what catches a still-adapting exposure.
Start-Sleep -Milliseconds 3000
Capture-Now $hwnd "$out\${Label}_t2.png"
if (-not $proc.HasExited) { $proc.Kill() }
Write-Output "VERDICT=DONE ${Label}"
